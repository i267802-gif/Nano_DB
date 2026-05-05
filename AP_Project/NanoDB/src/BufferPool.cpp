#include "BufferPool.h"
#include "Logger.h"

#include <cstdio>

namespace nanodb {

BufferPool::BufferPool(std::size_t capacity, Logger* log)
    : frames_(nullptr), capacity_(0), resident_(0), dm_(nullptr), log_(log) {
    resize(capacity);
}

BufferPool::~BufferPool() {
    flushAll();
    delete[] frames_;
}

void BufferPool::resize(std::size_t capacity) {
    flushAll();
    delete[] frames_;
    capacity_ = capacity;
    frames_ = new Frame[capacity_];
    resident_ = 0;
    pageToSlot_ = HashMap<long long, int>(capacity_ * 2);
    lru_.clear();
    stats_.reset();
}

int BufferPool::findFreeSlot() {
    for (std::size_t i = 0; i < capacity_; ++i) {
        if (!frames_[i].inUse) return (int)i;
    }
    return -1;
}

int BufferPool::evictOne() {
    DList<int>::Node* tail = lru_.tail();
    if (!tail) return -1;
    int slot = tail->value;
    Frame& f = frames_[slot];
    if (f.page.isDirty() && dm_) {
        dm_->writePage(f.page);
        if (log_) {
            log_->tag("LRU", "Page %u evicted via LRU, written to disk (dirty)", (unsigned)f.page.id());
        }
    } else {
        if (log_) {
            log_->tag("LRU", "Page %u evicted via LRU (clean, no flush)", (unsigned)f.page.id());
        }
    }
    pageToSlot_.erase((long long)f.page.id());
    lru_.erase(tail);
    f.inUse = false;
    f.lruNode = nullptr;
    --resident_;
    ++stats_.evictions;
    return slot;
}

Page* BufferPool::fetchPage(std::uint32_t pageId) {
    int* slotPtr = pageToSlot_.find((long long)pageId);
    if (slotPtr) {
        ++stats_.hits;
        Frame& f = frames_[*slotPtr];
        lru_.moveToFront(f.lruNode);
        return &f.page;
    }
    ++stats_.misses;
    ++stats_.pageFaults;
    int slot = findFreeSlot();
    if (slot < 0) slot = evictOne();
    if (slot < 0) return nullptr;
    Frame& f = frames_[slot];
    f.page.setId(pageId);
    if (dm_) dm_->readPage(pageId, f.page);
    f.inUse = true;
    f.lruNode = lru_.push_front(slot);
    pageToSlot_.insert((long long)pageId, slot);
    ++resident_;
    if (log_) {
        log_->tag("BUF", "Loaded page %u into slot %d (resident %zu/%zu)",
                  (unsigned)pageId, slot, resident_, capacity_);
    }
    return &f.page;
}

Page* BufferPool::allocateNewPage(std::uint32_t* outPageId) {
    if (!dm_) return nullptr;
    std::uint32_t pid = dm_->allocatePageId();
    int slot = findFreeSlot();
    if (slot < 0) slot = evictOne();
    if (slot < 0) return nullptr;
    Frame& f = frames_[slot];
    f.page.reset(pid);
    f.inUse = true;
    f.lruNode = lru_.push_front(slot);
    pageToSlot_.insert((long long)pid, slot);
    ++resident_;
    if (outPageId) *outPageId = pid;
    if (log_) {
        log_->tag("BUF", "Allocated new page %u in slot %d", (unsigned)pid, slot);
    }
    return &f.page;
}

void BufferPool::markDirty(std::uint32_t pageId) {
    int* s = pageToSlot_.find((long long)pageId);
    if (s) frames_[*s].page.setDirty(true);
}

void BufferPool::flushAll() {
    if (!frames_) return;
    for (std::size_t i = 0; i < capacity_; ++i) {
        if (frames_[i].inUse && frames_[i].page.isDirty() && dm_) {
            dm_->writePage(frames_[i].page);
            frames_[i].page.setDirty(false);
        }
    }
    if (log_) log_->tag("BUF", "Flushed all dirty pages");
}

} // namespace nanodb
