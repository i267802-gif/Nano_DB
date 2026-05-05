#include "DiskManager.h"

#include <cstdio>

namespace nanodb {

DiskManager::DiskManager() : path_(""), file_(nullptr), nextPageId_(0) {}
DiskManager::~DiskManager() { close(); }

bool DiskManager::open(const String& path) {
    close();
    path_ = path;
    // Open in binary read+write, create if missing
    file_ = std::fopen(path.c_str(), "rb+");
    if (!file_) {
        file_ = std::fopen(path.c_str(), "wb+");
        if (!file_) return false;
    }
    nextPageId_ = pageCount();
    return true;
}

void DiskManager::close() {
    if (file_) {
        std::fflush(file_);
        std::fclose(file_);
        file_ = nullptr;
    }
}

std::uint32_t DiskManager::allocatePageId() {
    return nextPageId_++;
}

std::uint32_t DiskManager::pageCount() {
    if (!file_) return 0;
    long cur = std::ftell(file_);
    std::fseek(file_, 0, SEEK_END);
    long end = std::ftell(file_);
    std::fseek(file_, cur, SEEK_SET);
    if (end <= 0) return 0;
    return (std::uint32_t)(end / (long)PAGE_SIZE);
}

bool DiskManager::pageExists(std::uint32_t pageId) {
    return pageId < pageCount();
}

bool DiskManager::readPage(std::uint32_t pageId, Page& dst) {
    if (!file_) return false;
    long off = (long)pageId * (long)PAGE_SIZE;
    if (std::fseek(file_, off, SEEK_SET) != 0) return false;
    unsigned char buf[PAGE_SIZE];
    std::size_t r = std::fread(buf, 1, PAGE_SIZE, file_);
    if (r != PAGE_SIZE) {
        // Page didn't exist; treat as empty
        dst.reset(pageId);
        return true;
    }
    dst.loadFromBlock(buf);
    dst.setId(pageId);
    return true;
}

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
