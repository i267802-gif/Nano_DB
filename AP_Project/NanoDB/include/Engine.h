#pragma once
// NanoDB Engine: top-level driver. Owns the System Catalog (HashMap of
// table name -> Table*), the priority query queue, and the Logger.
// Executes parsed commands, including the join optimizer and bench
// modes used by the demo test cases.

#include "Parser.h"
#include "Table.h"
#include "HashMap.h"
#include "PriorityQueue.h"
#include "DynArray.h"
#include "Logger.h"

namespace nanodb {

struct ScheduledQuery {
    int priority;       // higher first
    long long sequence; // tie-breaker (lower sequence first)
    String sql;
    ScheduledQuery() : priority(0), sequence(0), sql("") {}
    ScheduledQuery(int p, long long s, const String& q) : priority(p), sequence(s), sql(q) {}
};

// PriorityQueue is a max-heap by operator<; define ordering on ScheduledQuery
// so that higher priority OR (equal priority AND lower sequence) sorts first.
inline bool operator<(const ScheduledQuery& a, const ScheduledQuery& b) {
    // a < b means b has "higher" priority
    if (a.priority != b.priority) return a.priority < b.priority;
    return a.sequence > b.sequence; // earlier (smaller) seq is "greater"
}

struct ExecResult {
    bool ok;
    String message;
    long long rowsAffected;
    DynArray<Row> rows;       // for SELECT
    DynArray<String> headers;
    ExecResult() : ok(true), message(""), rowsAffected(0) {}
};

class Engine {
public:
    Engine(const String& dataDir, std::size_t bufferPagesPerTable = 64);
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // Execute one SQL string immediately. Returns ExecResult.
    ExecResult executeImmediate(const String& sql);

    // Submit a query to the priority queue. Higher priority runs first.
    void submit(const String& sql, int priority);

    // Execute all queued queries in priority order. Returns count.
    long long drainQueue();

    // Override the buffer pool capacity for a given table (Test Case D).
    bool resizeTableBuffer(const String& table, std::size_t pages);

    // Reset stats per-table.
    void resetTableStats(const String& table);

    Logger& logger() { return *log_; }

    // Get internal Table by name (or null).
    Table* table(const String& name);
    HashMap<String, Table*>& catalog() { return catalog_; }

    const String& dataDir() const { return dataDir_; }

    // Schema metadata is loaded from a fixed registry; see Engine.cpp.
    void registerSchema(const String& name, const Schema& s, std::size_t bufferPages);

    // Total rows in catalog
    long long totalRowsAll() const;

    // For test-runner reporting
    long long getEvictionCount(const String& table) const;
    long long getPageFaultCount(const String& table) const;

private:
    String dataDir_;
    std::size_t bufferPagesPerTable_;
    HashMap<String, Table*> catalog_;
    HashMap<String, Schema> schemas_;
    PriorityQueue<ScheduledQuery> queue_;
    long long submitSeq_;
    Logger* log_;

    // Helpers
    ExecResult execCreate(const ParsedCommand& cmd);
    ExecResult execInsert(const ParsedCommand& cmd);
    ExecResult execLoad(const ParsedCommand& cmd);
    ExecResult execIndex(const ParsedCommand& cmd);
    ExecResult execSelect(const ParsedCommand& cmd);
    ExecResult execUpdate(const ParsedCommand& cmd);
    ExecResult execDelete(const ParsedCommand& cmd);
    ExecResult execJoin(const ParsedCommand& cmd);
    ExecResult execBenchScan(const ParsedCommand& cmd);
    ExecResult execBenchIndex(const ParsedCommand& cmd);

    // Load TPC-H .tbl format -> insert into table
    long long loadTblFile(Table* t, const String& path);

    void printRows(const ExecResult& res);

    // Build a fresh Table from registered schema (file under dataDir/<name>.ndb).
    Table* ensureTable(const String& name);
};

} // namespace nanodb
