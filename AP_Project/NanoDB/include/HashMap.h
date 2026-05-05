#pragma once
// Open-chained hash table. The system catalog needs O(1) lookup of
// table metadata by name; chaining via singly-linked nodes resolves
// collisions deterministically. Resizes when load factor > 0.75.

#include <cstddef>
#include <utility>
#include "String.h"

namespace nanodb {

// Default hash for String
inline unsigned long long defaultHash(const String& s) { return s.hash(); }
inline unsigned long long defaultHash(int v) {
    unsigned long long x = (unsigned long long)v;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return x;
}
inline unsigned long long defaultHash(long long v) {
    return defaultHash((int)(v ^ (v >> 32)));
}

template <typename K, typename V>
class HashMap {
public:
    struct Entry {
        K key;
        V value;
        Entry* next;
        Entry(const K& k, const V& v) : key(k), value(v), next(nullptr) {}
    };

    HashMap(std::size_t initBuckets = 16)
        : buckets_(nullptr), nbuckets_(0), size_(0) {
        rehash(initBuckets);
    }

    HashMap(const HashMap& o) : buckets_(nullptr), nbuckets_(0), size_(0) {
        rehash(o.nbuckets_);
        for (std::size_t i = 0; i < o.nbuckets_; ++i) {
            Entry* e = o.buckets_[i];
            while (e) { insert(e->key, e->value); e = e->next; }
        }
    }

    HashMap(HashMap&& o) noexcept
        : buckets_(o.buckets_), nbuckets_(o.nbuckets_), size_(o.size_) {
        o.buckets_ = nullptr; o.nbuckets_ = 0; o.size_ = 0;
    }

    ~HashMap() {
        clearAll();
    }

    HashMap& operator=(const HashMap& o) {
        if (this == &o) return *this;
        clearAll();
        rehash(o.nbuckets_);
        for (std::size_t i = 0; i < o.nbuckets_; ++i) {
            Entry* e = o.buckets_[i];
            while (e) { insert(e->key, e->value); e = e->next; }
        }
        return *this;
    }

    HashMap& operator=(HashMap&& o) noexcept {
        if (this == &o) return *this;
        clearAll();
        buckets_ = o.buckets_; nbuckets_ = o.nbuckets_; size_ = o.size_;
        o.buckets_ = nullptr; o.nbuckets_ = 0; o.size_ = 0;
        return *this;
    }

    bool contains(const K& key) const {
        if (nbuckets_ == 0) return false;
        std::size_t b = defaultHash(key) % nbuckets_;
        for (Entry* e = buckets_[b]; e; e = e->next) {
            if (e->key == key) return true;
        }
        return false;
    }

    V* find(const K& key) const {
        if (nbuckets_ == 0) return nullptr;
        std::size_t b = defaultHash(key) % nbuckets_;
        for (Entry* e = buckets_[b]; e; e = e->next) {
            if (e->key == key) return &e->value;
        }
        return nullptr;
    }

    void insert(const K& key, const V& value) {
        if (nbuckets_ == 0) rehash(16);
        // Replace if exists
        std::size_t b = defaultHash(key) % nbuckets_;
        for (Entry* e = buckets_[b]; e; e = e->next) {
            if (e->key == key) { e->value = value; return; }
        }
        Entry* ne = new Entry(key, value);
        ne->next = buckets_[b];
        buckets_[b] = ne;
        ++size_;
        if (size_ * 4 > nbuckets_ * 3) rehash(nbuckets_ * 2);
    }

    bool erase(const K& key) {
        if (nbuckets_ == 0) return false;
        std::size_t b = defaultHash(key) % nbuckets_;
        Entry* prev = nullptr;
        Entry* e = buckets_[b];
        while (e) {
            if (e->key == key) {
                if (prev) prev->next = e->next;
                else buckets_[b] = e->next;
                delete e;
                --size_;
                return true;
            }
            prev = e;
            e = e->next;
        }
        return false;
    }

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    std::size_t bucketCount() const { return nbuckets_; }

    // Iterate all (key, value) pairs.
    template <typename Fn>
    void forEach(Fn fn) const {
        for (std::size_t i = 0; i < nbuckets_; ++i) {
            for (Entry* e = buckets_[i]; e; e = e->next) {
                fn(e->key, e->value);
            }
        }
    }

    // Stats: longest chain length (for collision diagnostics)
    std::size_t longestChain() const {
        std::size_t best = 0;
        for (std::size_t i = 0; i < nbuckets_; ++i) {
            std::size_t c = 0;
            for (Entry* e = buckets_[i]; e; e = e->next) ++c;
            if (c > best) best = c;
        }
        return best;
    }

private:
    Entry** buckets_;
    std::size_t nbuckets_;
    std::size_t size_;

    void clearAll() {
        if (!buckets_) return;
        for (std::size_t i = 0; i < nbuckets_; ++i) {
            Entry* e = buckets_[i];
            while (e) { Entry* nx = e->next; delete e; e = nx; }
        }
        delete[] buckets_;
        buckets_ = nullptr;
        nbuckets_ = 0;
        size_ = 0;
    }

    void rehash(std::size_t newCount) {
        if (newCount < 4) newCount = 4;
        Entry** nb = new Entry*[newCount];
        for (std::size_t i = 0; i < newCount; ++i) nb[i] = nullptr;
        if (buckets_) {
            for (std::size_t i = 0; i < nbuckets_; ++i) {
                Entry* e = buckets_[i];
                while (e) {
                    Entry* nx = e->next;
                    std::size_t b = defaultHash(e->key) % newCount;
                    e->next = nb[b];
                    nb[b] = e;
                    e = nx;
                }
            }
            delete[] buckets_;
        }
        buckets_ = nb;
        nbuckets_ = newCount;
    }
};

} // namespace nanodb
