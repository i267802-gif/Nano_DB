#include "Table.h"
#include "Logger.h"

namespace nanodb {

// Opens the backing file, attaches a buffer pool, counts existing rows,
// and sets up the current write page (allocating one if the file is empty).
Table::Table(const String& name, const Schema& schema, const String& filePath, std::size_t bufferPages, Logger* log)
    : name_(name), schema_(schema), disk_(nullptr), buf_(nullptr), log_(log),
      totalRows_(0), indexed_(16), currentWritePage_(0) {
    disk_ = new DiskManager();
    if (!disk_->open(filePath) && log) {
        log->tag("ERR", "Failed to open table file: %s", filePath.c_str());
    }
    buf_ = new BufferPool(bufferPages, log);
    buf_->attach(disk_);
    recountRows();
    if (disk_->pageCount() == 0) {
        // Empty file: allocate the first page.
        std::uint32_t pid = 0;
        Page* p = buf_->allocateNewPage(&pid);
        currentWritePage_ = pid;
        (void)p;
    } else {
        currentWritePage_ = disk_->pageCount() - 1;
    }
}

// Flushes all dirty pages, destroys all index trees, and releases heap resources.
Table::~Table() {
    flushAll();
    indexed_.forEach([](const String&, AVLTree<long long, RowLocator>* const& t) { delete t; });
    delete buf_;
    delete disk_;
}

// Scans every page in the disk file to recompute totalRows_.
void Table::recountRows() {
    totalRows_ = 0;
    std::uint32_t total = disk_->pageCount();
    for (std::uint32_t pid = 0; pid < total; ++pid) {
        Page* p = buf_->fetchPage(pid);
        if (!p) continue;
        totalRows_ += p->rowCount();
    }
}

// Tries to append a serialized row to the current write page.
// If the page is full, allocates a new one and retries.
bool Table::appendBytes(const unsigned char* rowBuf, std::uint32_t rowLen, RowLocator* outLoc) {
    Page* cur = buf_->fetchPage(currentWritePage_);
    if (!cur) return false;
    std::uint32_t slot = cur->rowCount();
    if (cur->tryAppendRow(rowBuf, rowLen)) {
        if (outLoc) { outLoc->pageId = currentWritePage_; outLoc->slotIdx = slot; }
        return true;
    }
    // Current page is full — allocate a new page.
    std::uint32_t newPid = 0;
    Page* np = buf_->allocateNewPage(&newPid);
    if (!np) return false;
    currentWritePage_ = newPid;
    slot = np->rowCount();
    bool ok = np->tryAppendRow(rowBuf, rowLen);
    if (ok && outLoc) { outLoc->pageId = newPid; outLoc->slotIdx = slot; }
    return ok;
}

// Serializes the row, appends it to storage, and updates all active indexes.
bool Table::insert(const Row& r) {
    unsigned char tmp[PAGE_SIZE];
    std::size_t w = r.serialize(tmp, PAGE_SIZE, 0);
    if (w == 0) return false;
    RowLocator loc;
    if (!appendBytes(tmp, (std::uint32_t)w, &loc)) return false;
    ++totalRows_;
    // Maintain integer indexes for every indexed column.
    indexed_.forEach([&](const String& colName, AVLTree<long long, RowLocator>* const& tree) {
        int idx = schema_.indexOf(colName);
        if (idx < 0) return;
        DBValue* v = r[(std::size_t)idx];
        if (!v) return;
        if (v->type() == DType::INT || v->type() == DType::FLOAT) {
            long long key = v->asInt();
            tree->insert(key, loc);
        }
    });
    return true;
}

// Builds (or rebuilds) an AVL index on a numeric column for fast lookups.
bool Table::buildIndex(const String& col) {
    int idx = schema_.indexOf(col);
    if (idx < 0) return false;

    // Drop any pre-existing index on this column.
    AVLTree<long long, RowLocator>** existing = indexed_.find(col);
    if (existing) {
        delete *existing;
        indexed_.erase(col);
    }

    AVLTree<long long, RowLocator>* tree = new AVLTree<long long, RowLocator>();
    std::uint32_t total = disk_->pageCount();
    for (std::uint32_t pid = 0; pid < total; ++pid) {
        Page* p = buf_->fetchPage(pid);
        if (!p) continue;
        std::uint32_t slot = 0;
        p->forEachRow([&](const unsigned char* rb, std::uint32_t rl) -> bool {
            Row r;
            std::size_t off = 0;
            r.deserialize(rb, rl, &off, schema_.size());
            DBValue* v = r[(std::size_t)idx];
            if (v && (v->type() == DType::INT || v->type() == DType::FLOAT)) {
                tree->insert(v->asInt(), RowLocator(pid, slot));
            }
            ++slot;
            return true;
        });
    }
    indexed_.insert(col, tree);
    if (log_) log_->tag("INDEX", "Built AVL index on %s.%s, height=%d, size=%zu",
                        name_.c_str(), col.c_str(), tree->height(), tree->size());
    return true;
}

// Returns all row locators matching val in the given indexed column.
DynArray<RowLocator> Table::indexLookupInt(const String& col, long long val) const {
    DynArray<RowLocator> out;
    AVLTree<long long, RowLocator>* const* t = indexed_.find(col);
    if (!t) return out;
    RowLocator* loc = (*t)->find(val);
    if (loc) out.push_back(*loc);
    return out;
}

// Rebuilds all currently active indexes from scratch (e.g. after bulk inserts).
void Table::rebuildIndexes() {
    DynArray<String> cols;
    indexed_.forEach([&](const String& k, AVLTree<long long, RowLocator>* const&) {
        cols.push_back(k);
    });
    for (std::size_t i = 0; i < cols.size(); ++i) buildIndex(cols[i]);
}

// Fetches the page for loc and scans slots until the target slot is found.
bool Table::readRow(const RowLocator& loc, Row* outRow) {
    Page* p = buf_->fetchPage(loc.pageId);
    if (!p) return false;
    std::uint32_t s = 0;
    bool found = false;
    p->forEachRow([&](const unsigned char* rb, std::uint32_t rl) -> bool {
        if (s == loc.slotIdx) {
            std::size_t off = 0;
            outRow->deserialize(rb, rl, &off, schema_.size());
            found = true;
            return false;
        }
        ++s;
        return true;
    });
    return found;
}

} // namespace nanodb
