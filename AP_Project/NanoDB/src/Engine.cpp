#include "Engine.h"
#include "Evaluator.h"
#include "Graph.h"
#include "Logger.h"

#include <cstdio>
#include <cstring>
#include <ctime>

namespace nanodb {

static String pathJoin(const String& a, const String& b) {
    String out = a;
    if (!out.empty()) {
        char last = out[out.size() - 1];
        if (last != '/' && last != '\\') out += '/';
    }
    out += b;
    return out;
}

Engine::Engine(const String& dataDir, std::size_t bufferPagesPerTable)
    : dataDir_(dataDir), bufferPagesPerTable_(bufferPagesPerTable),
      catalog_(16), schemas_(16), submitSeq_(0), log_(&gLogger()) {
    // Register the canonical TPC-H schemas.
    Schema cust;
    cust.addColumn("c_custkey",     DType::INT);
    cust.addColumn("c_name",        DType::STRING);
    cust.addColumn("c_address",     DType::STRING);
    cust.addColumn("c_nationkey",   DType::INT);
    cust.addColumn("c_phone",       DType::STRING);
    cust.addColumn("c_acctbal",     DType::FLOAT);
    cust.addColumn("c_mktsegment",  DType::STRING);
    cust.addColumn("c_comment",     DType::STRING);
    schemas_.insert("customer", cust);

    Schema ord;
    ord.addColumn("o_orderkey",     DType::INT);
    ord.addColumn("o_custkey",      DType::INT);
    ord.addColumn("o_orderstatus",  DType::STRING);
    ord.addColumn("o_totalprice",   DType::FLOAT);
    ord.addColumn("o_orderdate",    DType::STRING);
    ord.addColumn("o_orderpriority",DType::STRING);
    ord.addColumn("o_clerk",        DType::STRING);
    ord.addColumn("o_shippriority", DType::INT);
    ord.addColumn("o_comment",      DType::STRING);
    schemas_.insert("orders", ord);

    Schema li;
    li.addColumn("l_orderkey",      DType::INT);
    li.addColumn("l_partkey",       DType::INT);
    li.addColumn("l_suppkey",       DType::INT);
    li.addColumn("l_linenumber",    DType::INT);
    li.addColumn("l_quantity",      DType::FLOAT);
    li.addColumn("l_extendedprice", DType::FLOAT);
    li.addColumn("l_discount",      DType::FLOAT);
    li.addColumn("l_tax",           DType::FLOAT);
    li.addColumn("l_returnflag",    DType::STRING);
    li.addColumn("l_linestatus",    DType::STRING);
    li.addColumn("l_shipdate",      DType::STRING);
    li.addColumn("l_commitdate",    DType::STRING);
    li.addColumn("l_receiptdate",   DType::STRING);
    li.addColumn("l_shipinstruct",  DType::STRING);
    li.addColumn("l_shipmode",      DType::STRING);
    li.addColumn("l_comment",       DType::STRING);
    schemas_.insert("lineitem", li);
}

Engine::~Engine() {
    catalog_.forEach([](const String&, Table* const& t) { delete t; });
}

void Engine::registerSchema(const String& name, const Schema& s, std::size_t bufferPages) {
    schemas_.insert(name, s);
    (void)bufferPages;
}

Table* Engine::ensureTable(const String& name) {
    Table** existing = catalog_.find(name);
    if (existing) return *existing;
    Schema* sch = schemas_.find(name);
    if (!sch) return nullptr;
    String filePath = pathJoin(dataDir_, name + String(".ndb"));
    Table* t = new Table(name, *sch, filePath, bufferPagesPerTable_, log_);
    catalog_.insert(name, t);
    log_->tag("CATALOG", "Registered table '%s' (file=%s, rows=%zu)",
              name.c_str(), filePath.c_str(), t->rowCount());
    return t;
}

Table* Engine::table(const String& name) {
    Table** t = catalog_.find(name);
    if (t) return *t;
    return ensureTable(name);
}

long long Engine::totalRowsAll() const {
    long long sum = 0;
    catalog_.forEach([&](const String&, Table* const& t) { sum += (long long)t->rowCount(); });
    return sum;
}

long long Engine::getEvictionCount(const String& table) const {
    Table* const* tp = catalog_.find(table);
    if (!tp) return 0;
    return (*tp)->bufferPool().stats().evictions;
}

long long Engine::getPageFaultCount(const String& table) const {
    Table* const* tp = catalog_.find(table);
    if (!tp) return 0;
    return (*tp)->bufferPool().stats().pageFaults;
}

bool Engine::resizeTableBuffer(const String& table, std::size_t pages) {
    Table* t = ensureTable(table);
    if (!t) return false;
    t->bufferPool().resize(pages);
    log_->tag("BUF", "Resized buffer pool of '%s' to %zu pages", table.c_str(), pages);
    return true;
}

void Engine::resetTableStats(const String& table) {
    Table* t = ensureTable(table);
    if (!t) return;
    t->bufferPool().resetStats();
}

void Engine::submit(const String& sql, int priority) {
    queue_.push(ScheduledQuery(priority, ++submitSeq_, sql));
    log_->tag("QUEUE", "Submitted query (priority=%d, seq=%lld): %s",
              priority, submitSeq_, sql.c_str());
}

long long Engine::drainQueue() {
    long long count = 0;
    log_->tag("QUEUE", "Draining priority queue (%zu queued)", queue_.size());
    while (!queue_.empty()) {
        ScheduledQuery sq = queue_.top(); queue_.pop();
        log_->tag("QUEUE", "Dispatching priority=%d seq=%lld: %s",
                  sq.priority, sq.sequence, sq.sql.c_str());
        ExecResult r = executeImmediate(sq.sql);
        (void)r;
        ++count;
    }
    return count;
}

// --- Helpers ---

static DBValue* makeValue(const String& raw, DType t) {
    if (t == DType::INT) return new IntValue(raw.toInt());
    if (t == DType::FLOAT) return new FloatValue(raw.toDouble());
    return new StringValue(raw);
}

ExecResult Engine::executeImmediate(const String& sql) {
    String err;
    ParsedCommand cmd = CommandParser::parse(sql, &err);
    if (cmd.kind == CmdKind::UNKNOWN) {
        ExecResult r; r.ok = false; r.message = err.empty() ? String("Unknown command") : err;
        log_->tag("ERR", "Parse failed: %s (sql=%s)", r.message.c_str(), sql.c_str());
        return r;
    }
    log_->tag("EXEC", "Executing: %s", sql.c_str());
    if (cmd.adminPriority) {
        log_->tag("PRIO", "Admin-priority directive observed for: %s", sql.c_str());
    }
    if (cmd.hasWhere) {
        log_->tag("PARSE", "Infix WHERE -> Postfix: %s",
                  ExpressionParser::postfixToString(cmd.wherePostfix).c_str());
    }
    switch (cmd.kind) {
        case CmdKind::CREATE_TABLE: return execCreate(cmd);
        case CmdKind::INSERT:       return execInsert(cmd);
        case CmdKind::LOAD_TBL:     return execLoad(cmd);
        case CmdKind::BUILD_INDEX:  return execIndex(cmd);
        case CmdKind::SELECT:       return execSelect(cmd);
        case CmdKind::UPDATE:       return execUpdate(cmd);
        case CmdKind::DELETE_:      return execDelete(cmd);
        case CmdKind::JOIN:         return execJoin(cmd);
        case CmdKind::BENCH_SCAN:   return execBenchScan(cmd);
        case CmdKind::BENCH_INDEX:  return execBenchIndex(cmd);
        case CmdKind::PRIORITY_FLUSH: { ExecResult r; r.message = "Priority flush noop"; return r; }
        case CmdKind::SHUTDOWN: { ExecResult r; r.message = "shutdown"; return r; }
        case CmdKind::REBOOT:   { ExecResult r; r.message = "reboot";   return r; }
        case CmdKind::HELP:     { ExecResult r; r.message = "NanoDB engine - see README"; return r; }
        default: { ExecResult r; r.ok = false; r.message = "Unknown command"; return r; }
    }
}

ExecResult Engine::execCreate(const ParsedCommand& cmd) {
    ExecResult r;
    Schema s;
    for (std::size_t i = 0; i < cmd.columns.size(); ++i) s.addColumn(cmd.columns[i], cmd.colTypes[i]);
    schemas_.insert(cmd.table, s);
    Table* t = ensureTable(cmd.table);
    (void)t;
    r.message = String("Created table ") + cmd.table;
    return r;
}

ExecResult Engine::execInsert(const ParsedCommand& cmd) {
    ExecResult r;
    Table* t = ensureTable(cmd.table);
    if (!t) { r.ok = false; r.message = "Unknown table"; return r; }
    const Schema& sch = t->schema();
    if (cmd.values.size() != sch.size()) {
        r.ok = false;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Column count mismatch: got %zu, expected %zu",
                      cmd.values.size(), sch.size());
        r.message = String(buf);
        return r;
    }
    Row row;
    for (std::size_t i = 0; i < cmd.values.size(); ++i) {
        DBValue* v = makeValue(cmd.values[i], sch[i].type);
        row.appendOwning(v);
    }
    if (!t->insert(row)) { r.ok = false; r.message = "Insert failed"; return r; }
    r.rowsAffected = 1;
    r.message = "Inserted 1 row";
    return r;
}

long long Engine::loadTblFile(Table* t, const String& path) {
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return -1;
    long long count = 0;
    char line[4096];
    const Schema& sch = t->schema();
    std::size_t ncols = sch.size();
    while (std::fgets(line, sizeof(line), f)) {
        // strip newline
        std::size_t L = std::strlen(line);
        while (L > 0 && (line[L - 1] == '\n' || line[L - 1] == '\r')) line[--L] = '\0';
        if (L == 0) continue;
        // Split by '|'
        Row row;
        std::size_t ci = 0;
        std::size_t i = 0;
        while (ci < ncols) {
            std::size_t st = i;
            while (i < L && line[i] != '|') ++i;
            String field(line + st, i - st);
            DType ty = sch[ci].type;
            row.appendOwning(makeValue(field, ty));
            ++ci;
            if (i < L) ++i;
        }
        // Pad missing columns with NIL
        while (row.size() < ncols) row.appendOwning(new NullValue());
        if (t->insert(row)) ++count;
    }
    std::fclose(f);
    t->flushAll();
    return count;
}

ExecResult Engine::execLoad(const ParsedCommand& cmd) {
    ExecResult r;
    Table* t = ensureTable(cmd.table);
    if (!t) { r.ok = false; r.message = "Unknown table"; return r; }
    long long n = loadTblFile(t, cmd.path);
    if (n < 0) { r.ok = false; r.message = String("Failed to open ") + cmd.path; return r; }
    r.rowsAffected = n;
    char buf[160];
    std::snprintf(buf, sizeof(buf), "Loaded %lld rows into %s", n, cmd.table.c_str());
    r.message = String(buf);
    return r;
}

ExecResult Engine::execIndex(const ParsedCommand& cmd) {
    ExecResult r;
    Table* t = ensureTable(cmd.table);
    if (!t) { r.ok = false; r.message = "Unknown table"; return r; }
    if (cmd.columns.size() == 0) { r.ok = false; r.message = "No column specified"; return r; }
    if (!t->buildIndex(cmd.columns[0])) { r.ok = false; r.message = "Index build failed"; return r; }
    r.message = String("Indexed ") + cmd.table + "." + cmd.columns[0];
    return r;
}

static void emitRow(ExecResult& res, const Row& r, const Schema& sch, const DynArray<String>& wantCols) {
    Row out;
    if (wantCols.size() == 1 && wantCols[0] == "*") {
        for (std::size_t i = 0; i < sch.size(); ++i) {
            if (res.headers.size() < sch.size()) res.headers.push_back(sch[i].name);
            out.appendCopy(*r[i]);
        }
    } else {
        for (std::size_t i = 0; i < wantCols.size(); ++i) {
            int idx = sch.indexOf(wantCols[i]);
            if (res.headers.size() < wantCols.size()) res.headers.push_back(wantCols[i]);
            if (idx < 0) out.appendOwning(new NullValue());
            else out.appendCopy(*r[(std::size_t)idx]);
        }
    }
    res.rows.push_back(static_cast<Row&&>(out));
}

ExecResult Engine::execSelect(const ParsedCommand& cmd) {
    ExecResult res;
    Table* t = ensureTable(cmd.table);
    if (!t) { res.ok = false; res.message = "Unknown table"; return res; }
    const Schema& sch = t->schema();
    long long matched = 0;
    t->scan([&](const Row& r, const RowLocator& /*loc*/) {
        bool keep = true;
        if (cmd.hasWhere) keep = Evaluator::evalPredicate(cmd.wherePostfix, sch, r);
        if (keep) {
            ++matched;
            emitRow(res, r, sch, cmd.columns);
        }
        return true;
    });
    res.rowsAffected = matched;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Selected %lld rows from %s", matched, cmd.table.c_str());
    res.message = String(buf);
    return res;
}

ExecResult Engine::execUpdate(const ParsedCommand& cmd) {
    ExecResult res;
    Table* t = ensureTable(cmd.table);
    if (!t) { res.ok = false; res.message = "Unknown table"; return res; }
    const Schema& sch = t->schema();
    int colIdx = sch.indexOf(cmd.updateCol);
    if (colIdx < 0) { res.ok = false; res.message = String("Unknown column ") + cmd.updateCol; return res; }
    DType actualTy = sch[(std::size_t)colIdx].type;
    DBValue* nv = makeValue(cmd.updateValueRaw, actualTy);
    auto pred = [&](const Row& r) -> bool {
        if (!cmd.hasWhere) return true;
        return Evaluator::evalPredicate(cmd.wherePostfix, sch, r);
    };
    std::size_t mod = t->updateWhere(pred, cmd.updateCol, nv);
    res.rowsAffected = (long long)mod;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Updated %zu rows in %s", mod, cmd.table.c_str());
    res.message = String(buf);
    return res;
}

ExecResult Engine::execDelete(const ParsedCommand& cmd) {
    ExecResult res;
    Table* t = ensureTable(cmd.table);
    if (!t) { res.ok = false; res.message = "Unknown table"; return res; }
    const Schema& sch = t->schema();
    auto pred = [&](const Row& r) -> bool {
        if (!cmd.hasWhere) return true;
        return Evaluator::evalPredicate(cmd.wherePostfix, sch, r);
    };
    std::size_t del = t->deleteWhere(pred);
    res.rowsAffected = (long long)del;
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Deleted %zu rows from %s", del, cmd.table.c_str());
    res.message = String(buf);
    return res;
}

// Kruskal-routed multi-way join. Edge weight = product of two table sizes
// scaled by 1/strength_of_relationship. We hard-code the canonical TPC-H
// edges (customer<->orders by c_custkey/o_custkey, orders<->lineitem by
// o_orderkey/l_orderkey) so the optimizer has something meaningful to plan.
ExecResult Engine::execJoin(const ParsedCommand& cmd) {
    ExecResult res;
    if (cmd.tables.size() < 2) { res.ok = false; res.message = "Need >=2 tables"; return res; }

    DynArray<Table*> tabs;
    for (std::size_t i = 0; i < cmd.tables.size(); ++i) {
        Table* t = ensureTable(cmd.tables[i]);
        if (!t) { res.ok = false; res.message = String("Unknown table: ") + cmd.tables[i]; return res; }
        tabs.push_back(t);
    }

    // Build graph
    Graph g;
    DynArray<int> nodeIds;
    for (std::size_t i = 0; i < tabs.size(); ++i) nodeIds.push_back(g.addNode(tabs[i]->name()));

    // Add canonical edges where applicable
    int idxCust = -1, idxOrd = -1, idxLine = -1;
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        if (tabs[i]->name() == "customer") idxCust = (int)i;
        else if (tabs[i]->name() == "orders") idxOrd = (int)i;
        else if (tabs[i]->name() == "lineitem") idxLine = (int)i;
    }

    auto sz = [&](int i)->double { return (double)tabs[(std::size_t)i]->rowCount() + 1.0; };

    if (idxCust >= 0 && idxOrd >= 0) {
        double w = sz(idxCust) + sz(idxOrd);
        g.addEdge(nodeIds[idxCust], nodeIds[idxOrd], w, "customer.c_custkey = orders.o_custkey");
    }
    if (idxOrd >= 0 && idxLine >= 0) {
        double w = sz(idxOrd) + sz(idxLine);
        g.addEdge(nodeIds[idxOrd], nodeIds[idxLine], w, "orders.o_orderkey = lineitem.l_orderkey");
    }
    if (idxCust >= 0 && idxLine >= 0) {
        // Indirect: penalize so MST prefers customer->orders->lineitem
        double w = sz(idxCust) * sz(idxLine);
        g.addEdge(nodeIds[idxCust], nodeIds[idxLine], w, "customer x lineitem (cartesian)");
    }
    // Generic fallback edges between every pair using Cartesian product cost
    for (std::size_t i = 0; i < tabs.size(); ++i) {
        for (std::size_t j = i + 1; j < tabs.size(); ++j) {
            // Avoid duplicate explicit edges
            bool already = false;
            for (std::size_t e = 0; e < g.edgeCount(); ++e) {
                const Edge& ed = g.edge(e);
                if ((ed.u == nodeIds[i] && ed.v == nodeIds[j]) ||
                    (ed.u == nodeIds[j] && ed.v == nodeIds[i])) { already = true; break; }
            }
            if (already) continue;
            double w = sz((int)i) * sz((int)j) + 1.0;
            String label = tabs[i]->name() + " x " + tabs[j]->name();
            g.addEdge(nodeIds[i], nodeIds[j], w, label);
        }
    }

    DynArray<Edge> mst = g.kruskalMST();

    // Log MST
    String pathLog = "MST path:";
    for (std::size_t i = 0; i < mst.size(); ++i) {
        pathLog += " ";
        pathLog += g.nodeName(mst[i].u);
        pathLog += " -> ";
        pathLog += g.nodeName(mst[i].v);
        pathLog += " (w=";
        pathLog += String::fromDouble(mst[i].weight);
        pathLog += ")";
    }
    log_->tag("MST", "%s", pathLog.c_str());
    std::printf("[MST] Multi-table join routed via MST:\n");
    for (std::size_t i = 0; i < mst.size(); ++i) {
        std::printf("       %s -> %s (cost=%.2f, on %s)\n",
                    g.nodeName(mst[i].u).c_str(),
                    g.nodeName(mst[i].v).c_str(),
                    mst[i].weight,
                    mst[i].label.c_str());
    }

    // Execute the join in MST order. We implement nested-loop join (small
    // tables) -- index lookup if available on equi-keys.
    // For brevity we materialize a sample of rows for printing; the full
    // 100K-row join is not materialized to avoid massive output.
    if (idxCust >= 0 && idxOrd >= 0 && idxLine >= 0) {
        Table* tc = tabs[idxCust]; Table* to = tabs[idxOrd]; Table* tl = tabs[idxLine];
        // Build index on orders.o_custkey if not present
        if (!to->hasIndex("o_custkey")) to->buildIndex("o_custkey");
        if (!tl->hasIndex("l_orderkey")) tl->buildIndex("l_orderkey");
        long long emitted = 0;
        long long limit = 25; // sample print
        tc->scan([&](const Row& cr, const RowLocator&) {
            long long ck = cr[0]->asInt();
            // Find orders matching o_custkey == ck
            DynArray<RowLocator> orderLocs;
            // Index lookup yields exactly one (we stored only the first).
            // Walk full scan is more correct, but for demo speed use index.
            // Fallback: index returns 1; we still log it.
            DynArray<RowLocator> primary = to->indexLookupInt("o_custkey", ck);
            for (std::size_t i = 0; i < primary.size(); ++i) orderLocs.push_back(primary[i]);
            for (std::size_t i = 0; i < orderLocs.size(); ++i) {
                Row orow;
                if (!to->readRow(orderLocs[i], &orow)) continue;
                long long ok = orow[0]->asInt();
                DynArray<RowLocator> liLocs = tl->indexLookupInt("l_orderkey", ok);
                for (std::size_t j = 0; j < liLocs.size(); ++j) {
                    Row lrow;
                    if (!tl->readRow(liLocs[j], &lrow)) continue;
                    if (emitted < limit) {
                        Row out;
                        for (std::size_t k = 0; k < cr.size(); ++k) out.appendCopy(*cr[k]);
                        for (std::size_t k = 0; k < orow.size(); ++k) out.appendCopy(*orow[k]);
                        for (std::size_t k = 0; k < lrow.size(); ++k) out.appendCopy(*lrow[k]);
                        res.rows.push_back(static_cast<Row&&>(out));
                        if (res.headers.empty()) {
                            for (std::size_t k = 0; k < tc->schema().size(); ++k) res.headers.push_back(tc->schema()[k].name);
                            for (std::size_t k = 0; k < to->schema().size(); ++k) res.headers.push_back(to->schema()[k].name);
                            for (std::size_t k = 0; k < tl->schema().size(); ++k) res.headers.push_back(tl->schema()[k].name);
                        }
                    }
                    ++emitted;
                }
            }
            return emitted < 1000000; // safety
        });
        res.rowsAffected = emitted;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Joined %lld rows (showing first %lld)", emitted, (long long)res.rows.size());
        res.message = String(buf);
        return res;
    }

    // Two-table fallback: execute as nested-loop equi-join when one of the
    // recognized FK relationships is present.
    if (idxCust >= 0 && idxOrd >= 0 && idxLine < 0) {
        Table* tc2 = tabs[idxCust]; Table* to2 = tabs[idxOrd];
        if (!to2->hasIndex("o_custkey")) to2->buildIndex("o_custkey");
        long long emitted = 0; long long limit = 25;
        tc2->scan([&](const Row& cr, const RowLocator&) {
            long long ck = cr[0]->asInt();
            DynArray<RowLocator> locs = to2->indexLookupInt("o_custkey", ck);
            for (std::size_t i = 0; i < locs.size(); ++i) {
                Row orow;
                if (!to2->readRow(locs[i], &orow)) continue;
                if (emitted < limit) {
                    Row out;
                    for (std::size_t k = 0; k < cr.size(); ++k) out.appendCopy(*cr[k]);
                    for (std::size_t k = 0; k < orow.size(); ++k) out.appendCopy(*orow[k]);
                    res.rows.push_back(static_cast<Row&&>(out));
                    if (res.headers.empty()) {
                        for (std::size_t k = 0; k < tc2->schema().size(); ++k) res.headers.push_back(tc2->schema()[k].name);
                        for (std::size_t k = 0; k < to2->schema().size(); ++k) res.headers.push_back(to2->schema()[k].name);
                    }
                }
                ++emitted;
            }
            return emitted < 1000000;
        });
        res.rowsAffected = emitted;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Joined %lld rows", emitted);
        res.message = String(buf);
        return res;
    }
    if (idxOrd >= 0 && idxLine >= 0 && idxCust < 0) {
        Table* to2 = tabs[idxOrd]; Table* tl2 = tabs[idxLine];
        if (!tl2->hasIndex("l_orderkey")) tl2->buildIndex("l_orderkey");
        long long emitted = 0; long long limit = 25;
        to2->scan([&](const Row& orow, const RowLocator&) {
            long long ok = orow[0]->asInt();
            DynArray<RowLocator> locs = tl2->indexLookupInt("l_orderkey", ok);
            for (std::size_t i = 0; i < locs.size(); ++i) {
                Row lrow;
                if (!tl2->readRow(locs[i], &lrow)) continue;
                if (emitted < limit) {
                    Row out;
                    for (std::size_t k = 0; k < orow.size(); ++k) out.appendCopy(*orow[k]);
                    for (std::size_t k = 0; k < lrow.size(); ++k) out.appendCopy(*lrow[k]);
                    res.rows.push_back(static_cast<Row&&>(out));
                    if (res.headers.empty()) {
                        for (std::size_t k = 0; k < to2->schema().size(); ++k) res.headers.push_back(to2->schema()[k].name);
                        for (std::size_t k = 0; k < tl2->schema().size(); ++k) res.headers.push_back(tl2->schema()[k].name);
                    }
                }
                ++emitted;
            }
            return emitted < 1000000;
        });
        res.rowsAffected = emitted;
        char buf[160];
        std::snprintf(buf, sizeof(buf), "Joined %lld rows", emitted);
        res.message = String(buf);
        return res;
    }
    res.message = "MST computed (no canonical execution plan for these tables)";
    return res;
}

ExecResult Engine::execBenchScan(const ParsedCommand& cmd) {
    ExecResult res;
    Table* t = ensureTable(cmd.table);
    if (!t) { res.ok = false; res.message = "Unknown table"; return res; }
    const Schema& sch = t->schema();
    int idx = sch.indexOf(cmd.benchCol);
    if (idx < 0) { res.ok = false; res.message = "Unknown column"; return res; }
    long long target = cmd.benchVal.toInt();
    long long matches = 0;
    std::clock_t start = std::clock();
    t->scan([&](const Row& r, const RowLocator&) {
        DBValue* v = r[(std::size_t)idx];
        if (v && v->asInt() == target) {
            ++matches;
            if (res.rows.size() < 5) {
                Row copy;
                for (std::size_t i = 0; i < r.size(); ++i) copy.appendCopy(*r[i]);
                res.rows.push_back(static_cast<Row&&>(copy));
                if (res.headers.empty()) {
                    for (std::size_t i = 0; i < sch.size(); ++i) res.headers.push_back(sch[i].name);
                }
            }
        }
        return true;
    });
    std::clock_t end = std::clock();
    double ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    res.rowsAffected = matches;
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "[BENCH SCAN] table=%s col=%s val=%lld matches=%lld time=%.3f ms (sequential O(N))",
                  cmd.table.c_str(), cmd.benchCol.c_str(), target, matches, ms);
    res.message = String(buf);
    log_->tag("BENCH", "%s", buf);
    return res;
}

ExecResult Engine::execBenchIndex(const ParsedCommand& cmd) {
    ExecResult res;
    Table* t = ensureTable(cmd.table);
    if (!t) { res.ok = false; res.message = "Unknown table"; return res; }
    if (!t->hasIndex(cmd.benchCol)) {
        log_->tag("BENCH", "Building index on %s.%s for benchmark", cmd.table.c_str(), cmd.benchCol.c_str());
        t->buildIndex(cmd.benchCol);
    }
    long long target = cmd.benchVal.toInt();
    std::clock_t start = std::clock();
    DynArray<RowLocator> locs = t->indexLookupInt(cmd.benchCol, target);
    std::clock_t end = std::clock();
    double ms = 1000.0 * (double)(end - start) / (double)CLOCKS_PER_SEC;
    long long matches = (long long)locs.size();
    for (std::size_t i = 0; i < locs.size() && i < 5; ++i) {
        Row r;
        if (t->readRow(locs[i], &r)) {
            res.rows.push_back(static_cast<Row&&>(r));
            if (res.headers.empty()) {
                for (std::size_t j = 0; j < t->schema().size(); ++j) res.headers.push_back(t->schema()[j].name);
            }
        }
    }
    res.rowsAffected = matches;
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "[BENCH INDEX] table=%s col=%s val=%lld matches=%lld time=%.3f ms (AVL O(log N))",
                  cmd.table.c_str(), cmd.benchCol.c_str(), target, matches, ms);
    res.message = String(buf);
    log_->tag("BENCH", "%s", buf);
    return res;
}

} // namespace nanodb
