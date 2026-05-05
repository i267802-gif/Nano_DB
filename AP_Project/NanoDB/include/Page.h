#pragma once
// Page = fixed-size raw byte block. We treat it as a slotted page:
// a small header at the front, then row payloads packed sequentially.
// Header layout (16 bytes):
//   uint32 magic
//   uint32 page_id
//   uint32 row_count
//   uint32 used_bytes (offset of next free byte after header)

#include <cstdint>
#include <cstddef>

namespace nanodb {

constexpr std::size_t PAGE_SIZE = 4096;
constexpr std::size_t PAGE_HEADER = 16;
constexpr std::uint32_t PAGE_MAGIC = 0x4E444250; // "NDBP"

class Page {
public:
    Page() : id_(0), dirty_(false) {
        for (std::size_t i = 0; i < PAGE_SIZE; ++i) data_[i] = 0;
        writeHeader(0, 0, PAGE_HEADER);
    }
    Page(std::uint32_t id) : id_(id), dirty_(false) {
        for (std::size_t i = 0; i < PAGE_SIZE; ++i) data_[i] = 0;
        writeHeader(id, 0, PAGE_HEADER);
    }

    std::uint32_t id() const { return id_; }
    void setId(std::uint32_t i) { id_ = i; writeHeader(id_, rowCount(), used()); }
    bool isDirty() const { return dirty_; }
    void setDirty(bool d) { dirty_ = d; }

    unsigned char* raw() { return data_; }
    const unsigned char* raw() const { return data_; }
    std::size_t size() const { return PAGE_SIZE; }

    std::uint32_t rowCount() const { return readU32(8); }
    std::uint32_t used() const { return readU32(12); }
    std::uint32_t freeBytes() const { return PAGE_SIZE - used(); }

    // Try to append serialized bytes for one row. The serializer must
    // already know how many bytes a row occupies; we store an inline
    // length prefix so the deserializer can advance correctly.
    // Returns true if it fit.
    bool tryAppendRow(const unsigned char* rowBytes, std::uint32_t rowLen) {
        std::uint32_t u = used();
        if (u + sizeof(std::uint32_t) + rowLen > PAGE_SIZE) return false;
        writeU32(u, rowLen);
        for (std::uint32_t i = 0; i < rowLen; ++i) data_[u + 4 + i] = rowBytes[i];
        u += 4 + rowLen;
        std::uint32_t rc = rowCount();
        writeHeader(id_, rc + 1, u);
        dirty_ = true;
        return true;
    }

    // Iterate over rows: invokes fn(rowBytes, rowLen). Stops if fn returns false.
    template <typename Fn>
    void forEachRow(Fn fn) const {
        std::uint32_t off = PAGE_HEADER;
        std::uint32_t rc = rowCount();
        for (std::uint32_t i = 0; i < rc; ++i) {
            std::uint32_t rl = 0;
            for (int b = 0; b < 4; ++b) rl |= ((std::uint32_t)data_[off + b]) << (8 * b);
            off += 4;
            if (!fn(data_ + off, rl)) return;
            off += rl;
        }
    }

    // Reset page to empty state with a new id.
    void reset(std::uint32_t newId) {
        id_ = newId;
        for (std::size_t i = 0; i < PAGE_SIZE; ++i) data_[i] = 0;
        writeHeader(id_, 0, PAGE_HEADER);
        dirty_ = true;
    }

    // Initialize from a raw 4KB block read from disk.
    void loadFromBlock(const unsigned char* src) {
        for (std::size_t i = 0; i < PAGE_SIZE; ++i) data_[i] = src[i];
        std::uint32_t magic = readU32(0);
        if (magic != PAGE_MAGIC) {
            // Treat as fresh page
            writeHeader(id_, 0, PAGE_HEADER);
        } else {
            id_ = readU32(4);
        }
        dirty_ = false;
    }

private:
    unsigned char data_[PAGE_SIZE];
    std::uint32_t id_;
    bool dirty_;

    void writeU32(std::size_t off, std::uint32_t v) {
        data_[off + 0] = (unsigned char)(v & 0xFF);
        data_[off + 1] = (unsigned char)((v >> 8) & 0xFF);
        data_[off + 2] = (unsigned char)((v >> 16) & 0xFF);
        data_[off + 3] = (unsigned char)((v >> 24) & 0xFF);
    }

    std::uint32_t readU32(std::size_t off) const {
        return  (std::uint32_t)data_[off + 0]
             | ((std::uint32_t)data_[off + 1] << 8)
             | ((std::uint32_t)data_[off + 2] << 16)
             | ((std::uint32_t)data_[off + 3] << 24);
    }

    void writeHeader(std::uint32_t id, std::uint32_t rc, std::uint32_t used) {
        writeU32(0, PAGE_MAGIC);
        writeU32(4, id);
        writeU32(8, rc);
        writeU32(12, used);
    }
};

} // namespace nanodb
