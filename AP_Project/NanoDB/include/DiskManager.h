#pragma once
// Per-table file backed by raw page-sized binary blocks.
// Page N starts at byte offset N * PAGE_SIZE in the file. Reads and
// writes use FILE* fread/fwrite -- no formatting.

#include "String.h"
#include "Page.h"
#include <cstdio>

namespace nanodb {

class DiskManager {
public:
    DiskManager();
    ~DiskManager();

    bool open(const String& path);
    void close();

    // Allocate a new page id (returns previous max + 1).
    std::uint32_t allocatePageId();

    // True if a page with this id exists on disk.
    bool pageExists(std::uint32_t pageId);

    // Read one page from disk. Returns false on failure.
    bool readPage(std::uint32_t pageId, Page& dst);

    // Write a page to disk at the page's id offset.
    bool writePage(const Page& src);

    // Total page count (file_size / PAGE_SIZE).
    std::uint32_t pageCount();

    const String& path() const { return path_; }

private:
    String path_;
    std::FILE* file_;
    std::uint32_t nextPageId_;
};

} // namespace nanodb
