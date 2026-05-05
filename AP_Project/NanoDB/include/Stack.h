#pragma once
// Custom stack template backed by a singly-linked node chain.
// Drives the parser's Shunting-Yard infix->postfix conversion and
// the postfix evaluator.

#include <utility>

namespace nanodb {

template <typename T>
class Stack {
public:
    Stack() : top_(nullptr), size_(0) {}
    Stack(const Stack& o) : top_(nullptr), size_(0) {
        // Two-pass clone preserving order: count, then collect, then
        // push in reverse so the resulting top matches o's top.
        std::size_t cnt = 0;
        for (Node* n = o.top_; n; n = n->next) ++cnt;
        if (cnt == 0) return;
        Node** tmp = new Node*[cnt];
        std::size_t i = 0;
        for (Node* n = o.top_; n; n = n->next) tmp[i++] = n;
        for (std::size_t j = cnt; j-- > 0; ) push(tmp[j]->value);
        delete[] tmp;
    }
    Stack(Stack&& o) noexcept : top_(o.top_), size_(o.size_) {
        o.top_ = nullptr; o.size_ = 0;
    }
    ~Stack() { clear(); }

    Stack& operator=(const Stack& o) {
        if (this == &o) return *this;
        clear();
        Node* cur = o.top_;
        std::size_t cnt = 0; for (Node* n = cur; n; n = n->next) ++cnt;
        if (cnt == 0) return *this;
        Node** tmp = new Node*[cnt];
        std::size_t i = 0; for (Node* n = cur; n; n = n->next) tmp[i++] = n;
        for (std::size_t j = cnt; j-- > 0; ) push(tmp[j]->value);
        delete[] tmp;
        return *this;
    }

    Stack& operator=(Stack&& o) noexcept {
        if (this == &o) return *this;
        clear();
        top_ = o.top_; size_ = o.size_;
        o.top_ = nullptr; o.size_ = 0;
        return *this;
    }

    void push(const T& v) {
        Node* n = new Node{v, top_};
        top_ = n;
        ++size_;
    }

    void push(T&& v) {
        Node* n = new Node{std::move(v), top_};
        top_ = n;
        ++size_;
    }

    void pop() {
        if (!top_) return;
        Node* n = top_;
        top_ = n->next;
        delete n;
        --size_;
    }

    T& top() { return top_->value; }
    const T& top() const { return top_->value; }

    bool empty() const { return top_ == nullptr; }
    std::size_t size() const { return size_; }

    void clear() {
        Node* n = top_;
        while (n) { Node* nx = n->next; delete n; n = nx; }
        top_ = nullptr; size_ = 0;
    }

private:
    struct Node {
        T value;
        Node* next;
    };
    Node* top_;
    std::size_t size_;
};

} // namespace nanodb
