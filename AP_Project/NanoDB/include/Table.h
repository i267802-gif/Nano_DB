#pragma once
// Table: schema + per-table buffer pool, disk file, and AVL index map.
// Rows are appended into pages; flushed via the buffer pool. Indexed
// columns get a custom AVL tree from value -> (page_id, slot).

#include "Schema.h"
#include "Row.h"
#include "BufferPool.h"
#include "DiskManager.h"
#include "AVLTree.h"
#include "HashMap.h"

namespace nanodb {

class Logger;

struct RowLocator {
    std::uint32_t pageId;
    std::uint32_t slotIdx;
    RowLocator() : pageId(0), slotIdx(0) {}
    RowLocator(std::uint32_t p, std::uint32_t s) : pageId(p), slotIdx(s) {}
};

class Table {
public:
    Table(const String& name, const Schema& schema, const String& filePath, std::size_t bufferPages, Logger* log);
    ~Table();

    Table(const Table&) = delete;
    Table& operator=(const Table&) = delete;

    const String& name() const { return name_; }
    const Schema& schema() const { return schema_; }
    std::size_t   rowCount() const { return totalRows_; }
    BufferPool&   bufferPool() { return *buf_; }
    DiskManager&  disk() { return *disk_; }

    // Insert: serializes the row into the latest page (allocates if needed).
    bool insert(const Row& r);

    // Full table scan, invokes fn(row, locator). Stops if fn returns false.
    template <typename Fn>
    void scan(Fn fn);

    // Build an AVL index on column 'col' (by name). Replaces existing one.
    bool buildIndex(const String& col);

    bool hasIndex(const String& col) const { return indexed_.find(col) != nullptr; }

    // Look up rows whose indexed column == value (integer-keyed only).
    DynArray<RowLocator> indexLookupInt(const String& col, long long val) const;

    // Update / Delete via predicate. Returns count modified.
    template <typename Pred>
    std::size_t updateWhere(Pred pred, const String& col, DBValue* newVal);

    template <typename Pred>
    std::size_t deleteWhere(Pred pred);

    // Recover total row count from disk on boot.
    void rebuildIndexes();

    void flushAll() { buf_->flushAll(); }

    // Get row from locator (read-only). Returned Row owns clones.
    bool readRow(const RowLocator& loc, Row* outRow);

private:
    String   name_;
    Schema   schema_;
    DiskManager* disk_;
    BufferPool*  buf_;
    Logger*  log_;
    std::size_t totalRows_;

    // Map column name -> AVL tree of integer key -> first locator.
    HashMap<String, AVLTree<long long, RowLocator>*> indexed_;

    // Recompute totalRows_ by walking pages from disk.
    void recountRows();

    // Append row bytes to the current "open" page; allocate new page on overflow.
    bool appendBytes(const unsigned char* rowBuf, std::uint32_t rowLen, RowLocator* outLoc);

    // Track current writable page id.
    std::uint32_t currentWritePage_;
};

template <typename Fn>
void Table::scan(Fn fn) {
    std::uint32_t total = disk_->pageCount();
    for (std::uint32_t pid = 0; pid < total; ++pid) {
        Page* p = buf_->fetchPage(pid);
        if (!p) continue;
        std::uint32_t slot = 0;
        bool keepGoing = true;
        p->forEachRow([&](const unsigned char* rb, std::uint32_t rl) -> bool {
            Row r;
            std::size_t off = 0;
            r.deserialize(rb, rl, &off, schema_.size());
            RowLocator loc(pid, slot++);
            keepGoing = fn(r, loc);
            return keepGoing;
        });
        if (!keepGoing) break;
    }
}

template <typename Pred>
std::size_t Table::updateWhere(Pred pred, const String& col, DBValue* newVal) {
    int colIdx = schema_.indexOf(col);
    if (colIdx < 0) { delete newVal; return 0; }
    std::size_t modified = 0;
    std::uint32_t total = disk_->pageCount();
    for (std::uint32_t pid = 0; pid < total; ++pid) {
        Page* p = buf_->fetchPage(pid);
        if (!p) continue;
        DynArray<Row> rows;
        bool anyChange = false;
        p->forEachRow([&](const unsigned char* rb, std::uint32_t rl) -> bool {
            Row r;
            std::size_t off = 0;
            r.deserialize(rb, rl, &off, schema_.size());
            if (pred(r)) {
                r.replace((std::size_t)colIdx, newVal->clone());
                anyChange = true;
                ++modified;
            }
            rows.push_back(static_cast<Row&&>(r));
            return true;
        });
        if (anyChange) {
            // Rewrite page
            p->reset(pid);
            for (std::size_t i = 0; i < rows.size(); ++i) {
                unsigned char tmp[PAGE_SIZE];
                std::size_t w = rows[i].serialize(tmp, PAGE_SIZE, 0);
                if (w == 0) continue;
                p->tryAppendRow(tmp, (std::uint32_t)w);
            }
            p->setDirty(true);
        }
    }
    delete newVal;
    if (modified > 0) {
        buf_->flushAll();
        // Indexes might be stale -- rebuild defensively.
        rebuildIndexes();
    }
    return modified;
}

template <typename Pred>
std::size_t Table::deleteWhere(Pred pred) {
    std::size_t deleted = 0;
    std::uint32_t total = disk_->pageCount();
    for (std::uint32_t pid = 0; pid < total; ++pid) {
        Page* p = buf_->fetchPage(pid);
        if (!p) continue;
        DynArray<Row> kept;
        bool anyChange = false;
        p->forEachRow([&](const unsigned char* rb, std::uint32_t rl) -> bool {
            Row r;
            std::size_t off = 0;
            r.deserialize(rb, rl, &off, schema_.size());
            if (pred(r)) { ++deleted; anyChange = true; return true; }
            kept.push_back(static_cast<Row&&>(r));
            return true;
        });
        if (anyChange) {
            p->reset(pid);
            for (std::size_t i = 0; i < kept.size(); ++i) {
                unsigned char tmp[PAGE_SIZE];
                std::size_t w = kept[i].serialize(tmp, PAGE_SIZE, 0);
                if (w == 0) continue;
                p->tryAppendRow(tmp, (std::uint32_t)w);
            }
            p->setDirty(true);
        }
    }
    if (deleted > 0) {
        totalRows_ -= deleted;
        buf_->flushAll();
        rebuildIndexes();
    }
    return deleted;
}

} // namespace nanodb
