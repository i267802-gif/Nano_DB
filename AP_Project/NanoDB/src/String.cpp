#include "String.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace nanodb {

// Minimum heap capacity to allocate so very short strings don't thrash the
// allocator with 1- and 2-byte blocks.
static constexpr std::size_t kMinCap = 8;

String::String() : data_(nullptr), len_(0), cap_(kMinCap) {
    data_ = new char[kMinCap];
    data_[0] = '\0';
}

String::String(const char* s) : data_(nullptr), len_(0), cap_(0) {
    if (!s) {
        cap_ = kMinCap;
        data_ = new char[cap_];
        data_[0] = '\0';
        return;
    }
    len_ = std::strlen(s);
    cap_ = (len_ + 1 < kMinCap) ? kMinCap : len_ + 1;
    data_ = new char[cap_];
    std::memcpy(data_, s, len_);
    data_[len_] = '\0';
}

String::String(const char* s, std::size_t n) : data_(nullptr), len_(n), cap_(0) {
    cap_ = (n + 1 < kMinCap) ? kMinCap : n + 1;
    data_ = new char[cap_];
    if (s && n > 0) std::memcpy(data_, s, n);
    data_[n] = '\0';
}

String::String(const String& other) : data_(nullptr), len_(other.len_), cap_(0) {
    cap_ = (other.len_ + 1 < kMinCap) ? kMinCap : other.len_ + 1;
    data_ = new char[cap_];
    if (other.data_) std::memcpy(data_, other.data_, len_);
    data_[len_] = '\0';
}

String::String(String&& other) noexcept
    : data_(other.data_), len_(other.len_), cap_(other.cap_) {
    other.data_ = nullptr;
    other.len_  = 0;
    other.cap_  = 0;
}

String::~String() {
    delete[] data_;
}

String& String::operator=(const String& other) {
    if (this == &other) return *this;
    delete[] data_;
    len_ = other.len_;
    cap_ = (other.len_ + 1 < kMinCap) ? kMinCap : other.len_ + 1;
    data_ = new char[cap_];
    if (other.data_) std::memcpy(data_, other.data_, len_);
    data_[len_] = '\0';
    return *this;
}

String& String::operator=(String&& other) noexcept {
    if (this == &other) return *this;
    delete[] data_;
    data_      = other.data_;
    len_       = other.len_;
    cap_       = other.cap_;
    other.data_ = nullptr;
    other.len_  = 0;
    other.cap_  = 0;
    return *this;
}

String& String::operator=(const char* s) {
    delete[] data_;
    if (!s) {
        cap_ = kMinCap;
        data_ = new char[cap_];
        data_[0] = '\0';
        len_ = 0;
        return *this;
    }
    len_ = std::strlen(s);
    cap_ = (len_ + 1 < kMinCap) ? kMinCap : len_ + 1;
    data_ = new char[cap_];
    std::memcpy(data_, s, len_);
    data_[len_] = '\0';
    return *this;
}

void String::grow(std::size_t need) {
    if (need <= cap_) return;
    // Always jump to at least kMinCap; then double until we fit.
    std::size_t newCap = (cap_ < kMinCap) ? kMinCap : cap_ * 2;
    while (newCap < need) newCap *= 2;
    char* nd = new char[newCap];
    if (data_) std::memcpy(nd, data_, len_ + 1);
    delete[] data_;
    data_ = nd;
    cap_  = newCap;
}

void String::reserve(std::size_t c) { grow(c); }

void String::clear() {
    if (data_) data_[0] = '\0';
    len_ = 0;
}

void String::push_back(char c) {
    grow(len_ + 2);
    data_[len_++] = c;
    data_[len_]   = '\0';
}

String& String::operator+=(const String& rhs) {
    if (rhs.len_ == 0) return *this;
    grow(len_ + rhs.len_ + 1);
    std::memcpy(data_ + len_, rhs.data_, rhs.len_);
    len_ += rhs.len_;
    data_[len_] = '\0';
    return *this;
}

String& String::operator+=(const char* rhs) {
    if (!rhs) return *this;
    std::size_t n = std::strlen(rhs);
    if (n == 0) return *this;
    grow(len_ + n + 1);
    std::memcpy(data_ + len_, rhs, n);
    len_ += n;
    data_[len_] = '\0';
    return *this;
}

String& String::operator+=(char c) {
    push_back(c);
    return *this;
}

int String::compare(const String& other) const {
    // Fast path for self-comparison.
    if (this == &other) return 0;
    return std::strcmp(c_str(), other.c_str());
}

bool String::startsWith(const char* prefix) const {
    if (!prefix) return true;
    std::size_t n = std::strlen(prefix);
    if (n == 0) return true;
    if (n > len_) return false;
    return std::memcmp(data_, prefix, n) == 0;
}

String String::substr(std::size_t pos, std::size_t n) const {
    if (pos > len_) pos = len_;
    if (pos + n > len_) n = len_ - pos;
    return String(data_ + pos, n);
}

long long String::toInt() const {
    if (!data_) return 0;
    return std::strtoll(data_, nullptr, 10);
}

double String::toDouble() const {
    if (!data_) return 0.0;
    return std::strtod(data_, nullptr);
}

String String::fromInt(long long v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%lld", v);
    return String(buf);
}

String String::fromDouble(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.4f", v);
    return String(buf);
}

unsigned long long String::hash() const {
    // FNV-1a 64-bit
    unsigned long long h = 1469598103934665603ULL;
    for (std::size_t i = 0; i < len_; ++i) {
        h ^= (unsigned char)data_[i];
        h *= 1099511628211ULL;
    }
    return h;
}

// Short-circuit on size before doing a full strcmp.
bool operator==(const String& a, const String& b) {
    if (a.size() != b.size()) return false;
    return a.compare(b) == 0;
}
bool operator!=(const String& a, const String& b) {
    if (a.size() != b.size()) return true;
    return a.compare(b) != 0;
}
bool operator<(const String& a, const String& b)  { return a.compare(b) <  0; }
bool operator>(const String& a, const String& b)  { return a.compare(b) >  0; }
bool operator<=(const String& a, const String& b) { return a.compare(b) <= 0; }
bool operator>=(const String& a, const String& b) { return a.compare(b) >= 0; }

bool operator==(const String& a, const char* b) {
    if (!b) return a.empty();
    return std::strcmp(a.c_str(), b) == 0;
}
bool operator==(const char* a, const String& b) { return b == a; }

String operator+(const String& a, const String& b) { String r(a); r += b; return r; }
String operator+(const String& a, const char* b)   { String r(a); r += b; return r; }
String operator+(const char* a, const String& b)   { String r(a); r += b; return r; }

} // namespace nanodb
