#pragma once
// Buffer Pool: a fixed-capacity raw array of Page slots, plus a
// doubly-linked recency list for LRU eviction. Frame metadata
// (page_id -> slot index) lives in a custom HashMap so cache hits
// are O(1).
//
// Cache miss path:
//   1. If a free slot exists, use it.
//   2. Otherwise pop the LRU tail; if dirty, flush via DiskManager;
//      remove its mapping; reuse that slot.
//   3. Read the requested page from disk into the slot.
//   4. Push the slot to the head of the recency list.
//
// Cache hit path:
//   1. moveToFront(node) -- O(1).

#include "DList.h"
#include "HashMap.h"
#include "Page.h"
#include "DiskManager.h"
#include <cstddef>
#include <cstdint>

namespace nanodb {

class Logger; // fwd

struct Frame {
    Page page;
    bool inUse;
    DList<int>::Node* lruNode; // node in recency list whose value = slot index
    Frame() : inUse(false), lruNode(nullptr) {}
};

struct BufferStats {
    long long hits;
    long long misses;
    long long evictions;
    long long pageFaults;
    BufferStats() : hits(0), misses(0), evictions(0), pageFaults(0) {}
    void reset() { hits = misses = evictions = pageFaults = 0; }
};

class BufferPool {
public:
    BufferPool(std::size_t capacity, Logger* log = nullptr);
    ~BufferPool();

    BufferPool(const BufferPool&) = delete;
    BufferPool& operator=(const BufferPool&) = delete;

    // Resize buffer pool. Flushes everything first.
    void resize(std::size_t capacity);

    // Register a DiskManager for a particular file id. We use a per-table
    // bufferpool actually, so just one disk manager.
    void attach(DiskManager* dm) { dm_ = dm; }

    // Fetch a page; loads from disk if not resident. The reference is
    // valid until another fetchPage call may evict it.
    Page* fetchPage(std::uint32_t pageId);

    // Pin a page (mark in use; not evictable). For our simple usage we
    // don't pin during scans.
    void markDirty(std::uint32_t pageId);

    // Flush all dirty pages back to disk.
    void flushAll();

    // Allocate a new page (returns a fresh page id and resident slot).
    Page* allocateNewPage(std::uint32_t* outPageId);

    std::size_t capacity() const { return capacity_; }
    std::size_t residentCount() const { return resident_; }

    const BufferStats& stats() const { return stats_; }
    void resetStats() { stats_.reset(); }

private:
    Frame* frames_;
    std::size_t capacity_;
    std::size_t resident_;
    HashMap<long long, int> pageToSlot_;
    DList<int> lru_; // node value = slot index; head = MRU, tail = LRU
    DiskManager* dm_;
    Logger* log_;
    BufferStats stats_;

    int evictOne();
    int findFreeSlot();
};

} // namespace nanodb
