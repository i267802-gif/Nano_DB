#pragma once
// Doubly-Linked List template. Used as the LRU recency list for the
// Buffer Pool: O(1) splice-to-front on cache hit, O(1) tail eviction.

#include <cstddef>
#include <utility>

namespace nanodb {

template <typename T>
class DList {
public:
    // Intrusive node holding a value and bidirectional pointers.
    struct Node {
        T value;
        Node* prev;
        Node* next;
        Node(const T& v) : value(v), prev(nullptr), next(nullptr) {}
        Node(T&& v) : value(std::move(v)), prev(nullptr), next(nullptr) {}
    };

    // Constructs an empty list.
    DList() : head_(nullptr), tail_(nullptr), size_(0) {}

    // Deep-copies all nodes from o in order.
    DList(const DList& o) : head_(nullptr), tail_(nullptr), size_(0) {
        for (Node* n = o.head_; n; n = n->next) push_back(n->value);
    }

    // Transfers ownership of o's nodes; leaves o empty.
    DList(DList&& o) noexcept : head_(o.head_), tail_(o.tail_), size_(o.size_) {
        o.head_ = nullptr;
        o.tail_ = nullptr;
        o.size_ = 0;
    }

    // Destroys all remaining nodes.
    ~DList() { clear(); }

    // Copy-assignment: clears self then copies all nodes from o.
    DList& operator=(const DList& o) {
        if (this == &o) return *this;
        clear();
        for (Node* n = o.head_; n; n = n->next) push_back(n->value);
        return *this;
    }

    // Move-assignment: steals o's node pointers and leaves o empty.
    DList& operator=(DList&& o) noexcept {
        if (this == &o) return *this;
        clear();
        head_ = o.head_; tail_ = o.tail_; size_ = o.size_;
        o.head_ = nullptr; o.tail_ = nullptr; o.size_ = 0;
        return *this;
    }

    // Inserts a new node at the front of the list. Returns the new node.
    Node* push_front(const T& v) {
        Node* n = new Node(v);
        n->next = head_;
        if (head_) head_->prev = n;
        head_ = n;
        if (!tail_) tail_ = n;
        ++size_;
        return n;
    }

    // Appends a new node at the back of the list. Returns the new node.
    Node* push_back(const T& v) {
        Node* n = new Node(v);
        n->prev = tail_;
        if (tail_) tail_->next = n;
        tail_ = n;
        if (!head_) head_ = n;
        ++size_;
        return n;
    }

    // Removes and deletes the front node. No-op if the list is empty.
    void pop_front() {
        if (!head_) return;
        Node* n = head_;
        head_ = head_->next;
        if (head_) head_->prev = nullptr;
        else tail_ = nullptr;
        delete n;
        --size_;
    }

    // Removes and deletes the back node. No-op if the list is empty.
    void pop_back() {
        if (!tail_) return;
        Node* n = tail_;
        tail_ = tail_->prev;
        if (tail_) tail_->next = nullptr;
        else head_ = nullptr;
        delete n;
        --size_;
    }

    // Detach node from list without deleting it.
    void detach(Node* n) {
        if (!n) return;
        if (n->prev) n->prev->next = n->next;
        else head_ = n->next;
        if (n->next) n->next->prev = n->prev;
        else tail_ = n->prev;
        n->prev = nullptr;
        n->next = nullptr;
        --size_;
    }

    // Re-attach a previously detached node at the head: O(1).
    void attachFront(Node* n) {
        if (!n) return;
        n->prev = nullptr;
        n->next = head_;
        if (head_) head_->prev = n;
        head_ = n;
        if (!tail_) tail_ = n;
        ++size_;
    }

    // Move existing node to the head: O(1). Used by LRU on hit.
    void moveToFront(Node* n) {
        if (!n || n == head_) return;
        detach(n);
        attachFront(n);
    }

    // Erase node entirely: O(1).
    void erase(Node* n) {
        if (!n) return;
        detach(n);
        delete n;
    }

    // Destroys every node and resets the list to empty.
    void clear() {
        Node* n = head_;
        while (n) { Node* nx = n->next; delete n; n = nx; }
        head_ = nullptr; tail_ = nullptr; size_ = 0;
    }

    Node* head() const { return head_; }
    Node* tail() const { return tail_; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

private:
    Node* head_;
    Node* tail_;
    std::size_t size_;
};

} // namespace nanodb
