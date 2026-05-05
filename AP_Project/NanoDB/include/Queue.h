#pragma once
// Custom FIFO queue: circular doubly-linked list (no tail tracking needed,
// head->prev is the tail). The hint in the spec is "loop without tracking
// the tail" -- a circular DLL achieves O(1) enqueue and dequeue.

#include <utility>

namespace nanodb {

template <typename T>
class Queue {
public:
    Queue() : head_(nullptr), size_(0) {}
    Queue(const Queue& o) : head_(nullptr), size_(0) {
        Node* n = o.head_;
        for (std::size_t i = 0; i < o.size_; ++i) {
            push(n->value);
            n = n->next;
        }
    }
    Queue(Queue&& o) noexcept : head_(o.head_), size_(o.size_) {
        o.head_ = nullptr; o.size_ = 0;
    }
    ~Queue() { clear(); }

    Queue& operator=(const Queue& o) {
        if (this == &o) return *this;
        clear();
        Node* n = o.head_;
        for (std::size_t i = 0; i < o.size_; ++i) {
            push(n->value);
            n = n->next;
        }
        return *this;
    }

    Queue& operator=(Queue&& o) noexcept {
        if (this == &o) return *this;
        clear();
        head_ = o.head_; size_ = o.size_;
        o.head_ = nullptr; o.size_ = 0;
        return *this;
    }

    void push(const T& v) {
        Node* n = new Node{v, nullptr, nullptr};
        if (!head_) {
            n->next = n; n->prev = n;
            head_ = n;
        } else {
            Node* tail = head_->prev;
            n->prev = tail;
            n->next = head_;
            tail->next = n;
            head_->prev = n;
        }
        ++size_;
    }

    void pop() {
        if (!head_) return;
        Node* n = head_;
        if (n->next == n) {
            head_ = nullptr;
        } else {
            Node* tail = n->prev;
            head_ = n->next;
            head_->prev = tail;
            tail->next = head_;
        }
        delete n;
        --size_;
    }

    T& front() { return head_->value; }
    const T& front() const { return head_->value; }

    bool empty() const { return head_ == nullptr; }
    std::size_t size() const { return size_; }

    void clear() {
        if (!head_) return;
        Node* n = head_;
        n->prev->next = nullptr; // break cycle
        while (n) { Node* nx = n->next; delete n; n = nx; }
        head_ = nullptr; size_ = 0;
    }

private:
    struct Node {
        T value;
        Node* next;
        Node* prev;
    };
    Node* head_;
    std::size_t size_;
};

} // namespace nanodb
