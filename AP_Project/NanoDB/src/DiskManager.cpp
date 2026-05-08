#include "DiskManager.h"

#include <cstdio>

namespace nanodb {

// Initializes with an empty path, null file handle, and zero page counter.
DiskManager::DiskManager() : path_(""), file_(nullptr), nextPageId_(0) {}

// Ensures the file is flushed and closed on destruction.
DiskManager::~DiskManager() { close(); }

// Opens the file at path for binary read/write.
// Falls back to creating the file if it does not yet exist.
// On success, syncs nextPageId_ with the current on-disk page count.
bool DiskManager::open(const String& path) {
    close();
    path_ = path;
    // Try opening existing file first; create it if missing.
    file_ = std::fopen(path.c_str(), "rb+");
    if (!file_) {
        file_ = std::fopen(path.c_str(), "wb+");
        if (!file_) return false;
    }
    nextPageId_ = pageCount();
    return true;
}

// Flushes pending writes and closes the file handle.
void DiskManager::close() {
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

// Returns and increments the next available page ID (monotonically increasing).
std::uint32_t DiskManager::allocatePageId() {
    return nextPageId_++;
}

// Computes the number of pages on disk by dividing file size by PAGE_SIZE.
// Temporarily seeks to the end; restores the original position afterwards.
std::uint32_t DiskManager::pageCount() {
    if (!file_) return 0;
    long cur = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    long end = std::ftell(file_);
    std::fseek(file_, cur, SEEK_SET);
    if (end <= 0) return 0;
    return (std::uint32_t)(end / (long)PAGE_SIZE);
}

// Returns true if pageId falls within the current on-disk page range.
bool DiskManager::pageExists(std::uint32_t pageId) {
    return pageId < pageCount();
}

// Reads PAGE_SIZE bytes from the file at the offset for pageId into dst.
// If the page does not exist yet (short read), resets dst to an empty page.
bool DiskManager::readPage(std::uint32_t pageId, Page& dst) {
    if (!file_) return false;
    long off = (long)pageId * (long)PAGE_SIZE;
    if (std::fseek(file_, off, SEEK_SET) != 0) return false;
    unsigned char buf[PAGE_SIZE];
    std::size_t r = std::fread(buf, 1, PAGE_SIZE, file_);
    if (r != PAGE_SIZE) {
        // Page didn't exist; treat as empty.
        dst.reset(pageId);
        return true;
    }
    dst.loadFromBlock(buf);
    dst.setId(pageId);
    return true;
}

// Writes PAGE_SIZE bytes from src to the file at src's page offset.
// Flushes immediately and advances nextPageId_ if a new page was written.
bool DiskManager::writePage(const Page& src) {
    if (!file_) return false;
    long off = (long)src.id() * (long)PAGE_SIZE;
    if (std::fseek(file_, off, SEEK_SET) != 0) return false;
    std::size_t w = std::fwrite(src.raw(), 1, PAGE_SIZE, file_);
    if (w != PAGE_SIZE) return false;
    std::fflush(file_);
    if (src.id() + 1 > nextPageId_) nextPageId_ = src.id() + 1;
    return true;
}

} // namespace nanodb
