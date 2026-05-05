#pragma once
// AVL tree (height-balanced binary search tree).
// Used by the Index Optimizer (Test Case B in spec): O(log N) search by key.
// On insert/delete, rotations restore the height invariant |bf| <= 1.

#include <cstddef>
#include <utility>

namespace nanodb {

template <typename K, typename V>
class AVLTree {
public:
    struct Node {
        K key;
        V value;
        int height;
        Node* left;
        Node* right;
        Node(const K& k, const V& v) : key(k), value(v), height(1), left(nullptr), right(nullptr) {}
    };

    AVLTree() : root_(nullptr), size_(0) {}
    AVLTree(const AVLTree& o) : root_(nullptr), size_(0) { root_ = clone(o.root_); size_ = o.size_; }
    AVLTree(AVLTree&& o) noexcept : root_(o.root_), size_(o.size_) { o.root_ = nullptr; o.size_ = 0; }
    ~AVLTree() { clear(root_); }

    AVLTree& operator=(const AVLTree& o) {
        if (this == &o) return *this;
        clear(root_);
        root_ = clone(o.root_);
        size_ = o.size_;
        return *this;
    }

    AVLTree& operator=(AVLTree&& o) noexcept {
        if (this == &o) return *this;
        clear(root_);
        root_ = o.root_; size_ = o.size_;
        o.root_ = nullptr; o.size_ = 0;
        return *this;
    }

    void insert(const K& k, const V& v) {
        bool inserted = false;
        root_ = insertRec(root_, k, v, inserted);
        if (inserted) ++size_;
    }

    bool erase(const K& k) {
        bool removed = false;
        root_ = eraseRec(root_, k, removed);
        if (removed) --size_;
        return removed;
    }

    // Returns pointer to value if key found, else nullptr. O(log N).
    V* find(const K& k) const {
        Node* cur = root_;
        while (cur) {
            if (k < cur->key) cur = cur->left;
            else if (cur->key < k) cur = cur->right;
            else return &cur->value;
        }
        return nullptr;
    }

    int height() const { return root_ ? root_->height : 0; }
    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // In-order traversal — supplies (key, value) sorted by key.
    template <typename Fn>
    void inorder(Fn fn) const { inorderRec(root_, fn); }

private:
    Node* root_;
    std::size_t size_;

    static int h(Node* n) { return n ? n->height : 0; }
    static int bf(Node* n) { return n ? h(n->left) - h(n->right) : 0; }

    static void updateH(Node* n) {
        int hl = h(n->left), hr = h(n->right);
        n->height = (hl > hr ? hl : hr) + 1;
    }

    static Node* rotR(Node* y) {
        Node* x = y->left;
        Node* t = x->right;
        x->right = y;
        y->left = t;
        updateH(y); updateH(x);
        return x;
    }

    static Node* rotL(Node* x) {
        Node* y = x->right;
        Node* t = y->left;
        y->left = x;
        x->right = t;
        updateH(x); updateH(y);
        return y;
    }

    static Node* rebalance(Node* n) {
        updateH(n);
        int b = bf(n);
        if (b > 1 && bf(n->left) >= 0) return rotR(n);
        if (b > 1 && bf(n->left) < 0) {
            n->left = rotL(n->left);
            return rotR(n);
        }
        if (b < -1 && bf(n->right) <= 0) return rotL(n);
        if (b < -1 && bf(n->right) > 0) {
            n->right = rotR(n->right);
            return rotL(n);
        }
        return n;
    }

    Node* insertRec(Node* n, const K& k, const V& v, bool& inserted) {
        if (!n) { inserted = true; return new Node(k, v); }
        if (k < n->key) n->left = insertRec(n->left, k, v, inserted);
        else if (n->key < k) n->right = insertRec(n->right, k, v, inserted);
        else { n->value = v; inserted = false; return n; }
        return rebalance(n);
    }

    static Node* minNode(Node* n) {
        while (n && n->left) n = n->left;
        return n;
    }

    Node* eraseRec(Node* n, const K& k, bool& removed) {
        if (!n) return nullptr;
        if (k < n->key) n->left = eraseRec(n->left, k, removed);
        else if (n->key < k) n->right = eraseRec(n->right, k, removed);
        else {
            removed = true;
            if (!n->left || !n->right) {
                Node* tmp = n->left ? n->left : n->right;
                delete n;
                return tmp;
            }
            Node* succ = minNode(n->right);
            n->key = succ->key;
            n->value = succ->value;
            bool dummy = false;
            n->right = eraseRec(n->right, succ->key, dummy);
        }
        return rebalance(n);
    }

    static Node* clone(Node* n) {
        if (!n) return nullptr;
        Node* c = new Node(n->key, n->value);
        c->height = n->height;
        c->left = clone(n->left);
        c->right = clone(n->right);
        return c;
    }

    static void clear(Node* n) {
        if (!n) return;
        clear(n->left);
        clear(n->right);
        delete n;
    }

    template <typename Fn>
    static void inorderRec(Node* n, Fn fn) {
        if (!n) return;
        inorderRec(n->left, fn);
        fn(n->key, n->value);
        inorderRec(n->right, fn);
    }
};

} // namespace nanodb
