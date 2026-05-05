#pragma once
// Custom String class. STL std::string is forbidden by project spec.
// Owns a heap-allocated null-terminated char buffer.

#include <cstddef>

namespace nanodb {

class String {
public:
    String();
    String(const char* s);
    String(const char* s, std::size_t n);
    String(const String& other);
    String(String&& other) noexcept;
    ~String();

    String& operator=(const String& other);
    String& operator=(String&& other) noexcept;
    String& operator=(const char* s);

    std::size_t size() const { return len_; }
    std::size_t length() const { return len_; }
    bool empty() const { return len_ == 0; }
    const char* c_str() const { return data_ ? data_ : ""; }
    const char* data() const { return data_; }

    char& operator[](std::size_t i) { return data_[i]; }
    char operator[](std::size_t i) const { return data_[i]; }

    String& operator+=(const String& rhs);
    String& operator+=(const char* rhs);
    String& operator+=(char c);

    void clear();
    void reserve(std::size_t cap);
    void push_back(char c);

    // Compare lexicographically
    int compare(const String& other) const;
    bool startsWith(const char* prefix) const;
    String substr(std::size_t pos, std::size_t n) const;

    // Convert to numeric
    long long toInt() const;
    double toDouble() const;

    // Utility
    static String fromInt(long long v);
    static String fromDouble(double v);

    // Hash
    unsigned long long hash() const;

private:
    char* data_;
    std::size_t len_;
    std::size_t cap_;

    void grow(std::size_t need);
};

bool operator==(const String& a, const String& b);
bool operator!=(const String& a, const String& b);
bool operator<(const String& a, const String& b);
bool operator>(const String& a, const String& b);
bool operator<=(const String& a, const String& b);
bool operator>=(const String& a, const String& b);

bool operator==(const String& a, const char* b);
bool operator==(const char* a, const String& b);

String operator+(const String& a, const String& b);
String operator+(const String& a, const char* b);
String operator+(const char* a, const String& b);

} // namespace nanodb
