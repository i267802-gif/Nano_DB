#pragma once
// Row owns N polymorphic DBValue* columns. Serializing a row writes
// each value's tag+payload sequentially.

#include "DBValue.h"
#include "DynArray.h"

namespace nanodb {

class Row {
public:
    Row() {}
    Row(const Row& o) { for (std::size_t i = 0; i < o.cols_.size(); ++i) cols_.push_back(o.cols_[i]->clone()); }
    Row(Row&& o) noexcept : cols_(static_cast<DynArray<DBValue*>&&>(o.cols_)) {}
    ~Row() { for (std::size_t i = 0; i < cols_.size(); ++i) delete cols_[i]; }

    Row& operator=(const Row& o) {
        if (this == &o) return *this;
        for (std::size_t i = 0; i < cols_.size(); ++i) delete cols_[i];
        cols_.clear();
        for (std::size_t i = 0; i < o.cols_.size(); ++i) cols_.push_back(o.cols_[i]->clone());
        return *this;
    }

    Row& operator=(Row&& o) noexcept {
        if (this == &o) return *this;
        for (std::size_t i = 0; i < cols_.size(); ++i) delete cols_[i];
        cols_.clear();
        cols_ = static_cast<DynArray<DBValue*>&&>(o.cols_);
        return *this;
    }

    void appendOwning(DBValue* v) { cols_.push_back(v); }
    void appendCopy(const DBValue& v) { cols_.push_back(v.clone()); }

    std::size_t size() const { return cols_.size(); }
    DBValue* at(std::size_t i) const { return cols_[i]; }
    DBValue* operator[](std::size_t i) const { return cols_[i]; }

    void replace(std::size_t i, DBValue* v) {
        if (i >= cols_.size()) return;
        delete cols_[i];
        cols_[i] = v;
    }

    // Serialize all columns into buf starting at off; returns total bytes
    // written. 0 means out-of-space.
    std::size_t serialize(unsigned char* buf, std::size_t cap, std::size_t off) const {
        std::size_t start = off;
        for (std::size_t i = 0; i < cols_.size(); ++i) {
            std::size_t w = cols_[i]->serialize(buf, cap, off);
            if (w == 0) return 0;
            off += w;
        }
        return off - start;
    }

    // Read 'ncols' values from buf and append to this row.
    bool deserialize(const unsigned char* buf, std::size_t cap, std::size_t* off, std::size_t ncols) {
        for (std::size_t i = 0; i < ncols; ++i) {
            DBValue* v = deserializeValue(buf, cap, off);
            if (!v) return false;
            cols_.push_back(v);
        }
        return true;
    }

private:
    DynArray<DBValue*> cols_;
};

} // namespace nanodb
