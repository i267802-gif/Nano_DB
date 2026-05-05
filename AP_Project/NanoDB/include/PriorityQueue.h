#pragma once
// Binary max-heap priority queue, hand-rolled atop a raw dynamic array.
// Used to schedule queries: admin transactions get higher priority than
// background SELECTs and therefore execute first.

#include <cstddef>
#include <utility>

namespace nanodb {

template <typename T>
class PriorityQueue {
public:
    PriorityQueue() : data_(nullptr), size_(0), cap_(0) {}
    PriorityQueue(const PriorityQueue& o) : data_(nullptr), size_(0), cap_(0) {
        reserve(o.size_);
        for (std::size_t i = 0; i < o.size_; ++i) {
            new (data_ + i) T(o.data_[i]);
        }
        size_ = o.size_;
    }
    PriorityQueue(PriorityQueue&& o) noexcept : data_(o.data_), size_(o.size_), cap_(o.cap_) {
        o.data_ = nullptr; o.size_ = 0; o.cap_ = 0;
    }
    ~PriorityQueue() {
        for (std::size_t i = 0; i < size_; ++i) data_[i].~T();
        ::operator delete(data_);
    }

    PriorityQueue& operator=(const PriorityQueue& o) {
        if (this == &o) return *this;
        for (std::size_t i = 0; i < size_; ++i) data_[i].~T();
        size_ = 0;
        if (cap_ < o.size_) {
            ::operator delete(data_);
            data_ = nullptr; cap_ = 0;
            reserve(o.size_);
        }
        for (std::size_t i = 0; i < o.size_; ++i) new (data_ + i) T(o.data_[i]);
        size_ = o.size_;
        return *this;
    }

    PriorityQueue& operator=(PriorityQueue&& o) noexcept {
        if (this == &o) return *this;
        for (std::size_t i = 0; i < size_; ++i) data_[i].~T();
        ::operator delete(data_);
        data_ = o.data_; size_ = o.size_; cap_ = o.cap_;
        o.data_ = nullptr; o.size_ = 0; o.cap_ = 0;
        return *this;
    }

    void push(const T& v) {
        if (size_ == cap_) reserve(size_ + 1);
        new (data_ + size_) T(v);
        ++size_;
        siftUp(size_ - 1);
    }

    void push(T&& v) {
        if (size_ == cap_) reserve(size_ + 1);
        new (data_ + size_) T(std::move(v));
        ++size_;
        siftUp(size_ - 1);
    }

    void pop() {
        if (size_ == 0) return;
        data_[0].~T();
        if (size_ == 1) {
            size_ = 0;
            return;
        }
        new (data_ + 0) T(std::move(data_[size_ - 1]));
        data_[size_ - 1].~T();
        --size_;
        siftDown(0);
    }

    T& top() { return data_[0]; }
    const T& top() const { return data_[0]; }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

private:
    T* data_;
    std::size_t size_;
    std::size_t cap_;

    void reserve(std::size_t n) {
        if (n <= cap_) return;
        std::size_t newCap = cap_ ? cap_ * 2 : 8;
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

    void swapAt(std::size_t a, std::size_t b) {
        T tmp(std::move(data_[a]));
        data_[a].~T();
        new (data_ + a) T(std::move(data_[b]));
        data_[b].~T();
        new (data_ + b) T(std::move(tmp));
    }

    void siftUp(std::size_t i) {
        while (i > 0) {
            std::size_t p = (i - 1) / 2;
            if (data_[p] < data_[i]) {
                swapAt(p, i);
                i = p;
            } else break;
        }
    }

    void siftDown(std::size_t i) {
        while (true) {
            std::size_t l = 2 * i + 1;
            std::size_t r = 2 * i + 2;
            std::size_t big = i;
            if (l < size_ && data_[big] < data_[l]) big = l;
            if (r < size_ && data_[big] < data_[r]) big = r;
            if (big == i) break;
            swapAt(big, i);
            i = big;
        }
    }
};

} // namespace nanodb
