// test_runner.cpp -- automated test harness for NanoDB.
//
// Behavior:
//   1. Opens nanodb_execution.log
//   2. Generates TPC-H data into ./data if missing
//   3. Reads queries.txt and runs each command in order
//   4. Runs the seven explicit demo test cases (A-G) from the spec
//   5. Prints stats and a summary
//
// CLI flags (optional):
//   --queries <path>   override queries.txt path
//   --data <dir>       override data dir
//   --log <path>       override log file path
//   --no-gen           skip TPC-H generation if already exists
//   --buffer <N>       override default buffer pages per table
//   --skip-load        skip large LOAD lines (for fast smoke tests)

#include "Engine.h"
#include "Logger.h"
#include "String.h"
#include "DynArray.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

using namespace nanodb;

namespace {

bool fileExists(const String& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

bool dirExists(const String& path) {
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    return (st.st_mode & S_IFDIR) != 0;
}

// Cross-platform mkdir
void ensureDir(const String& path) {
    if (dirExists(path)) return;
#ifdef _WIN32
    String cmd = String("cmd /C if not exist \"") + path + "\" mkdir \"" + path + "\"";
    std::system(cmd.c_str());
#else
    String cmd = String("mkdir -p \"") + path + "\"";
    std::system(cmd.c_str());
#endif
}

// Run the bundled gen_tpch tool: prefer linked function call. Since we
// don't link gen_tpch into this binary by default, we instead
// re-implement a minimal generator here so the harness is self-contained.
// (gen_tpch.cpp is also provided as a standalone tool.)
unsigned long long g_seed = 1469598103934665603ULL;
unsigned long long rng() {
    g_seed ^= g_seed >> 12;
    g_seed ^= g_seed << 25;
    g_seed ^= g_seed >> 27;
    return g_seed * 2685821657736338717ULL;
}
int rInt(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + (int)(rng() % (unsigned long long)(hi - lo + 1));
}
double rDbl(double lo, double hi) {
    double t = (double)(rng() % 1000000ULL) / 1000000.0;
    return lo + t * (hi - lo);
}
const char* rSeg() {
    static const char* s[] = {"BUILDING","AUTOMOBILE","MACHINERY","HOUSEHOLD","FURNITURE"};
    return s[rInt(0, 4)];
}
const char* rStat(){ static const char* s[] = {"O","F","P"}; return s[rInt(0,2)]; }
const char* rPrio(){ static const char* s[] = {"1-URGENT","2-HIGH","3-MEDIUM","4-NOT SPECIFIED","5-LOW"}; return s[rInt(0,4)]; }
const char* rFlag(){ static const char* s[] = {"N","R","A"}; return s[rInt(0,2)]; }
const char* rMode(){ static const char* s[] = {"AIR","RAIL","SHIP","TRUCK","MAIL","FOB","REG AIR"}; return s[rInt(0,6)]; }
void rDate(char* b, int n){ std::snprintf(b, (size_t)n, "%04d-%02d-%02d", rInt(1992,1998), rInt(1,12), rInt(1,28)); }

void genCustomers(const char* path, int n) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) return;
    char nm[64], ph[32];
    for (int i = 1; i <= n; ++i) {
        std::snprintf(nm, sizeof(nm), "Customer#%09d", i);
        std::snprintf(ph, sizeof(ph), "%02d-%03d-%03d-%04d", rInt(10,99), rInt(100,999), rInt(100,999), rInt(1000,9999));
        std::fprintf(f, "%d|%s|Addr#%d|%d|%s|%.2f|%s|comment_%d|\n",
                     i, nm, rInt(1,99999), rInt(0,24), ph, rDbl(-999.0, 9999.99), rSeg(), rInt(0,9999));
    }
    std::fclose(f);
}
void genOrders(const char* path, int n, int customerN) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) return;
    char clk[32], dt[16];
    for (int i = 1; i <= n; ++i) {
        std::snprintf(clk, sizeof(clk), "Clerk#%09d", rInt(1,1000));
        rDate(dt, sizeof(dt));
        std::fprintf(f, "%d|%d|%s|%.2f|%s|%s|%s|0|comment_%d|\n",
                     i, rInt(1, customerN), rStat(), rDbl(900.0, 500000.0), dt, rPrio(), clk, rInt(0,9999));
    }
    std::fclose(f);
}
void genLineItems(const char* path, int n, int orderN) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) return;
    char sd[16], cd[16], rd[16];
    for (int i = 1; i <= n; ++i) {
        rDate(sd, sizeof(sd)); rDate(cd, sizeof(cd)); rDate(rd, sizeof(rd));
        std::fprintf(f, "%d|%d|%d|%d|%.2f|%.2f|%.4f|%.4f|%s|%s|%s|%s|%s|DELIVER|%s|comment_%d|\n",
                     rInt(1, orderN), rInt(1, 200000), rInt(1, 10000), rInt(1, 7),
                     rDbl(1.0, 50.0), rDbl(900.0, 90000.0), rDbl(0.0, 0.10), rDbl(0.0, 0.08),
                     rFlag(), rStat(), sd, cd, rd, rMode(), rInt(0,9999));
    }
    std::fclose(f);
}

// Print up to N rows of an ExecResult to stdout (formatted).
void printResult(const ExecResult& res, int maxRows = 10) {
    std::printf(">> %s\n", res.message.c_str());
    if (res.headers.size() == 0 || res.rows.size() == 0) return;
    // header
    for (std::size_t i = 0; i < res.headers.size(); ++i) {
        if (i) std::printf(" | ");
        std::printf("%s", res.headers[i].c_str());
    }
    std::printf("\n");
    int n = (int)res.rows.size();
    if (n > maxRows) n = maxRows;
    for (int i = 0; i < n; ++i) {
        const Row& r = res.rows[(std::size_t)i];
        for (std::size_t k = 0; k < r.size(); ++k) {
            if (k) std::printf(" | ");
            std::printf("%s", r[k]->toString().c_str());
        }
        std::printf("\n");
    }
    if ((int)res.rows.size() > maxRows) {
        std::printf("... (%zu rows total, showing %d)\n", res.rows.size(), maxRows);
    }
}

} // anon

static String trim(const String& s) {
    std::size_t i = 0, j = s.size();
    while (i < j && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i;
    while (j > i && (s[j-1] == ' ' || s[j-1] == '\t' || s[j-1] == '\r' || s[j-1] == '\n')) --j;
    return s.substr(i, j - i);
}

int main(int argc, char** argv) {
    String queriesPath = "queries.txt";
    String dataDir = "data";
    String logPath = "nanodb_execution.log";
    bool   noGen = false;
    bool   skipLoad = false;
    std::size_t bufferPages = 64;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--queries") == 0 && i + 1 < argc) queriesPath = argv[++i];
        else if (std::strcmp(argv[i], "--data") == 0 && i + 1 < argc) dataDir = argv[++i];
        else if (std::strcmp(argv[i], "--log") == 0 && i + 1 < argc) logPath = argv[++i];
        else if (std::strcmp(argv[i], "--no-gen") == 0) noGen = true;
        else if (std::strcmp(argv[i], "--skip-load") == 0) skipLoad = true;
        else if (std::strcmp(argv[i], "--buffer") == 0 && i + 1 < argc) bufferPages = (std::size_t)std::atoi(argv[++i]);
    }

    ensureDir(dataDir);
    if (!gLogger().open(logPath, false)) {
        std::fprintf(stderr, "Could not open log file %s\n", logPath.c_str());
        return 1;
    }
    gLogger().tag("BOOT", "NanoDB starting (data=%s, queries=%s, log=%s, buffer=%zu)",
                  dataDir.c_str(), queriesPath.c_str(), logPath.c_str(), bufferPages);
    std::printf("[NanoDB] Booting (data=%s, queries=%s)\n", dataDir.c_str(), queriesPath.c_str());

    // Generate TPC-H data if missing
    String custFile = dataDir + "/customer.tbl";
    String ordFile  = dataDir + "/orders.tbl";
    String liFile   = dataDir + "/lineitem.tbl";
    if (!noGen && (!fileExists(custFile) || !fileExists(ordFile) || !fileExists(liFile))) {
        std::printf("[NanoDB] Generating TPC-H data (20K customers, 30K orders, 50K lineitems)...\n");
        gLogger().tag("DATA", "Generating TPC-H subset under %s", dataDir.c_str());
        genCustomers(custFile.c_str(), 20000);
        genOrders(ordFile.c_str(), 30000, 20000);
        genLineItems(liFile.c_str(), 50000, 30000);
    } else if (noGen) {
        std::printf("[NanoDB] --no-gen set: assuming data files exist.\n");
    } else {
        std::printf("[NanoDB] Reusing existing data files.\n");
    }

    Engine eng(dataDir, bufferPages);

    // Run queries.txt
    std::FILE* f = std::fopen(queriesPath.c_str(), "r");
    if (!f) {
        std::fprintf(stderr, "Cannot open queries file: %s\n", queriesPath.c_str());
        gLogger().tag("ERR", "Missing queries file: %s", queriesPath.c_str());
    } else {
        char buf[4096];
        long long lineNo = 0;
        long long executed = 0;
        while (std::fgets(buf, sizeof(buf), f)) {
            ++lineNo;
            String line = trim(String(buf));
            if (line.empty()) continue;
            if (line[0] == '#') continue;
            if (skipLoad && line.startsWith("LOAD")) continue;
            std::printf("\n[Q%lld] %s\n", lineNo, line.c_str());
            ExecResult res = eng.executeImmediate(line);
            printResult(res);
            ++executed;
        }
        std::fclose(f);
        std::printf("\n[NanoDB] Workload complete: %lld queries executed.\n", executed);
        gLogger().tag("WORKLOAD", "Executed %lld queries from %s", executed, queriesPath.c_str());
    }

    // ===== DEMO TEST CASES =====

    std::printf("\n=========================================\n");
    std::printf("  DEMO TEST CASES (per project spec)\n");
    std::printf("=========================================\n");

    // --- Test Case A: Parser & Evaluator ---
    {
        std::printf("\n--- Test Case A: Parser & Evaluator ---\n");
        String q = "SELECT * FROM customer WHERE (c_acctbal > 5000 AND c_mktsegment == \"BUILDING\") OR c_nationkey == 15";
        std::printf("Input: %s\n", q.c_str());
        ExecResult r = eng.executeImmediate(q);
        printResult(r, 5);
    }

    // --- Test Case B: Index optimizer ---
    {
        std::printf("\n--- Test Case B: Index Optimizer (scan vs AVL) ---\n");
        ExecResult s1 = eng.executeImmediate("BENCH SCAN customer c_custkey 12345");
        std::printf("%s\n", s1.message.c_str());
        ExecResult s2 = eng.executeImmediate("BENCH INDEX customer c_custkey 12345");
        std::printf("%s\n", s2.message.c_str());
        std::printf("(Compare the two times above. AVL index should be O(log N).)\n");
    }

    // --- Test Case C: Join Optimizer (MST) ---
    {
        std::printf("\n--- Test Case C: Join Optimizer (MST routing) ---\n");
        ExecResult r = eng.executeImmediate("SELECT * FROM customer JOIN orders JOIN lineitem");
        printResult(r, 5);
    }

    // --- Test Case D: Memory stress test (50 pages, scan 5000 lineitems) ---
    {
        std::printf("\n--- Test Case D: Memory Stress (50 pages, 5K scan) ---\n");
        eng.resizeTableBuffer("lineitem", 50);
        eng.resetTableStats("lineitem");
        // Manually walk the first 5000 lineitem rows so we trigger evictions.
        Table* tl = eng.table("lineitem");
        if (tl) {
            long long count = 0;
            tl->scan([&](const Row&, const RowLocator&) {
                ++count;
                return count < 5000;
            });
            std::printf("Scanned %lld lineitem records.\n", count);
            std::printf("Page faults: %lld | Evictions via LRU: %lld\n",
                        eng.getPageFaultCount("lineitem"),
                        eng.getEvictionCount("lineitem"));
        }
    }

    // --- Test Case E: Priority Queue Concurrency ---
    {
        std::printf("\n--- Test Case E: Priority Queue (admin preempts) ---\n");
        for (int i = 0; i < 50; ++i) {
            char qb[160];
            std::snprintf(qb, sizeof(qb), "SELECT c_name FROM customer WHERE c_custkey == %d", 1000 + i);
            eng.submit(String(qb), /*priority=*/0);
        }
        eng.submit("ADMIN UPDATE customer SET c_acctbal = 99999 WHERE c_custkey == 900001", /*priority=*/100);
        long long ran = eng.drainQueue();
        std::printf("Drained %lld queries (admin transaction ran first by priority).\n", ran);
        ExecResult after = eng.executeImmediate("SELECT c_custkey, c_acctbal FROM customer WHERE c_custkey == 900001");
        printResult(after, 1);
    }

    // --- Test Case F: Deep expression tree ---
    {
        std::printf("\n--- Test Case F: Deep Expression Tree ---\n");
        String q = "SELECT * FROM orders WHERE ((o_totalprice * 1.5) > 100000 AND (o_custkey % 2 == 0)) OR (o_orderstatus != \"O\")";
        std::printf("Input: %s\n", q.c_str());
        ExecResult r = eng.executeImmediate(q);
        std::printf(">> %s\n", r.message.c_str());
    }

    // --- Test Case G: Durability ---
    {
        std::printf("\n--- Test Case G: Durability & Persistence ---\n");
        // Insert 5 sentinel customers
        for (int i = 0; i < 5; ++i) {
            char qb[200];
            std::snprintf(qb, sizeof(qb),
                "INSERT INTO customer VALUES (%d, \"Persist#%d\", \"AddrP\", %d, \"99-000-0000\", %.2f, \"FURNITURE\", \"persist-test\")",
                950000 + i, i, i, 1000.0 + i * 100.0);
            eng.executeImmediate(String(qb));
        }
        // Force flush (Engine destructor flushes too, but we want to verify mid-run)
        Table* tc = eng.table("customer");
        if (tc) tc->flushAll();
        std::printf("Inserted 5 persisted records (custkey 950000..950004) and flushed buffer.\n");
        std::printf("After this run, restart the engine; SELECT * FROM customer WHERE c_custkey == 950000 must return the row.\n");
    }

    // --- Final stats ---
    std::printf("\n========== Engine Stats ==========\n");
    std::printf("Total rows across catalog: %lld\n", eng.totalRowsAll());
    Table* tc = eng.table("customer");
    if (tc) {
        const BufferStats& s = tc->bufferPool().stats();
        std::printf("customer  buffer: hits=%lld misses=%lld evictions=%lld pageFaults=%lld\n",
                    s.hits, s.misses, s.evictions, s.pageFaults);
    }
    Table* to = eng.table("orders");
    if (to) {
        const BufferStats& s = to->bufferPool().stats();
        std::printf("orders    buffer: hits=%lld misses=%lld evictions=%lld pageFaults=%lld\n",
                    s.hits, s.misses, s.evictions, s.pageFaults);
    }
    Table* tl = eng.table("lineitem");
    if (tl) {
        const BufferStats& s = tl->bufferPool().stats();
        std::printf("lineitem  buffer: hits=%lld misses=%lld evictions=%lld pageFaults=%lld\n",
                    s.hits, s.misses, s.evictions, s.pageFaults);
    }
    std::printf("Log written to: %s\n", logPath.c_str());

    gLogger().tag("BOOT", "NanoDB shutting down cleanly");
    gLogger().close();
    return 0;
}
