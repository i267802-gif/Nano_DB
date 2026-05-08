#pragma once
// Custom dynamic array template. STL std::vector is forbidden.
// Behaves like a minimal vector: heap-allocated contiguous storage with
// geometric growth and proper destructor invocation on element removal.

#include <cstddef>
#include <new>
#include <utility>

namespace nanodb {

template <typename T>
class DynArray {
public:
    // Constructs an empty array with no allocated storage.
    DynArray() : data_(nullptr), size_(0), cap_(0) {}

    // Constructs an array of n copies of v, pre-allocating exactly n slots.
    DynArray(std::size_t n, const T& v = T()) : data_(nullptr), size_(0), cap_(0) {
        reserve(n);
        for (std::size_t i = 0; i < n; ++i) new (data_ + i) T(v);
        size_ = n;
    }

    // Copy constructor: deep-copies every element from o.
    DynArray(const DynArray& o) : data_(nullptr), size_(0), cap_(0) {
        reserve(o.size_);
        for (std::size_t i = 0; i < o.size_; ++i) new (data_ + i) T(o.data_[i]);
        size_ = o.size_;
    }

    // Move constructor: steals o's buffer and leaves o empty.
    DynArray(DynArray&& o) noexcept : data_(o.data_), size_(o.size_), cap_(o.cap_) {
        o.data_ = nullptr;
        o.size_ = 0;
        o.cap_ = 0;
    }

    // Destructor: calls each element's destructor then frees the raw buffer.
    ~DynArray() {
        clear();
        ::operator delete(data_);
    }

    // Copy-assignment: reuses existing capacity when sufficient.
    DynArray& operator=(const DynArray& o) {
        if (this == &o) return *this;
        clear();
        if (cap_ < o.size_) {
            ::operator delete(data_);
            data_ = nullptr;
            cap_ = 0;
            reserve(o.size_);
        }
        for (std::size_t i = 0; i < o.size_; ++i) new (data_ + i) T(o.data_[i]);
        size_ = o.size_;
        return *this;
    }

    // Move-assignment: swaps buffer ownership; leaves o in a valid empty state.
    DynArray& operator=(DynArray&& o) noexcept {
        if (this == &o) return *this;
        clear();
        ::operator delete(data_);
        data_ = o.data_;
        size_ = o.size_;
        cap_ = o.cap_;
        o.data_ = nullptr;
        o.size_ = 0;
        o.cap_ = 0;
        return *this;
    }

    std::size_t size()     const { return size_; }
    std::size_t capacity() const { return cap_;  }
    bool        empty()    const { return size_ == 0; }

    T&       operator[](std::size_t i)       { return data_[i]; }
    const T& operator[](std::size_t i) const { return data_[i]; }

    T&       back()        { return data_[size_ - 1]; }
    const T& back()  const { return data_[size_ - 1]; }
    T&       front()       { return data_[0]; }
    const T& front() const { return data_[0]; }

    T*       data()       { return data_; }
    const T* data() const { return data_; }

    // Ensures capacity for at least n elements using geometric doubling.
    // Existing elements are move-constructed into the new buffer.
    void reserve(std::size_t n) {
        if (n <= cap_) return;
        std::size_t newCap = cap_ ? cap_ * 2 : 4;
        while (newCap < n) newCap *= 2;
        T* nd = static_cast<T*>(::operator new(sizeof(T) * newCap));
        for (std::size_t i = 0; i < size_; ++i) {
            new (nd + i) T(std::move(data_[i]));
            data_[i].~T();
        }
        ::operator delete(data_);
        data_ = nd;
        cap_ = newCap;
    }

    // Shrinks or grows the array to exactly n elements.
    // New elements are value-initialized with v.
    void resize(std::size_t n, const T& v = T()) {
        if (n < size_) {
            for (std::size_t i = n; i < size_; ++i) data_[i].~T();
            size_ = n;
        } else if (n > size_) {
            reserve(n);
            for (std::size_t i = size_; i < n; ++i) new (data_ + i) T(v);
            size_ = n;
        }
    }

    // Appends a copy of v, growing the buffer if needed.
    void push_back(const T& v) {
        if (size_ == cap_) reserve(size_ + 1);
        new (data_ + size_) T(v);
        ++size_;
    }

    // Appends v by move, growing the buffer if needed.
    void push_back(T&& v) {
        if (size_ == cap_) reserve(size_ + 1);
        new (data_ + size_) T(std::move(v));
        ++size_;
    }

    // Destroys and removes the last element. No-op if empty.
    void pop_back() {
        if (size_ == 0) return;
        data_[size_ - 1].~T();
        --size_;
    }

    // Destroys all elements; leaves capacity unchanged.
    void clear() {
        for (std::size_t i = 0; i < size_; ++i) data_[i].~T();
        size_ = 0;
    }

    // Erases the element at idx, shifting subsequent elements left.
    void erase(std::size_t idx) {
        if (idx >= size_) return;
        for (std::size_t i = idx; i + 1 < size_; ++i) {
            data_[i].~T();
            new (data_ + i) T(std::move(data_[i + 1]));
        }
        data_[size_ - 1].~T();
        --size_;
    }

    // Custom in-place insertion sort (no STL). Destruct each slot before
    // re-constructing via placement-new to avoid leaking the moved-from
    // payload and to keep the operation well-defined for non-trivially
    // destructible T.
    template <typename Cmp>
    void sort(Cmp cmp) {
        for (std::size_t i = 1; i < size_; ++i) {
            T key = std::move(data_[i]);
            data_[i].~T();
            std::size_t j = i;
            while (j > 0 && cmp(key, data_[j - 1])) {
                new (data_ + j) T(std::move(data_[j - 1]));
                data_[j - 1].~T();
                --j;
            }
            new (data_ + j) T(std::move(key));
        }
    }

private:
    T*          data_;
    std::size_t size_;
    std::size_t cap_;
};

} // namespace nanodb
