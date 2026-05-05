#pragma once
// Polymorphic value hierarchy. The spec calls for "Base classes, virtual
// tables, polymorphism" so a row can hold heterogeneous types.
//
//   DBValue (abstract)
//      |--- IntValue
//      |--- FloatValue
//      |--- StringValue
//      |--- NullValue
//
// Operator overloads compare across types where it makes sense
// (int <-> float promotion, strings compare lexicographically).

#include "String.h"
#include <cstdint>

namespace nanodb {

enum class DType : unsigned char {
    NIL = 0,
    INT = 1,
    FLOAT = 2,
    STRING = 3
};

class DBValue {
public:
    virtual ~DBValue() {}
    virtual DType type() const = 0;
    virtual DBValue* clone() const = 0;
    virtual String toString() const = 0;

    // Serialize to a raw byte buffer at offset; returns bytes written.
    virtual std::size_t serialize(unsigned char* buf, std::size_t cap, std::size_t off) const = 0;
    // Coerce to numeric for arithmetic operators.
    virtual double asDouble() const = 0;
    virtual long long asInt() const = 0;
};

class NullValue : public DBValue {
public:
    DType type() const override { return DType::NIL; }
    DBValue* clone() const override { return new NullValue(); }
    String toString() const override { return String("NULL"); }
    std::size_t serialize(unsigned char* buf, std::size_t cap, std::size_t off) const override;
    double asDouble() const override { return 0.0; }
    long long asInt() const override { return 0; }
};

class IntValue : public DBValue {
public:
    long long v;
    IntValue() : v(0) {}
    IntValue(long long x) : v(x) {}
    DType type() const override { return DType::INT; }
    DBValue* clone() const override { return new IntValue(v); }
    String toString() const override { return String::fromInt(v); }
    std::size_t serialize(unsigned char* buf, std::size_t cap, std::size_t off) const override;
    double asDouble() const override { return (double)v; }
    long long asInt() const override { return v; }
};

class FloatValue : public DBValue {
public:
    double v;
    FloatValue() : v(0.0) {}
    FloatValue(double x) : v(x) {}
    DType type() const override { return DType::FLOAT; }
    DBValue* clone() const override { return new FloatValue(v); }
    String toString() const override { return String::fromDouble(v); }
    std::size_t serialize(unsigned char* buf, std::size_t cap, std::size_t off) const override;
    double asDouble() const override { return v; }
    long long asInt() const override { return (long long)v; }
};

class StringValue : public DBValue {
public:
    String v;
    StringValue() : v("") {}
    StringValue(const String& s) : v(s) {}
    StringValue(const char* s) : v(s) {}
    DType type() const override { return DType::STRING; }
    DBValue* clone() const override { return new StringValue(v); }
    String toString() const override { return v; }
    std::size_t serialize(unsigned char* buf, std::size_t cap, std::size_t off) const override;
    double asDouble() const override { return v.toDouble(); }
    long long asInt() const override { return v.toInt(); }
};

// Operator overloads on DBValue references. Promote int<->float for
// arithmetic; string comparisons use lexicographic ordering.
bool operator==(const DBValue& a, const DBValue& b);
bool operator!=(const DBValue& a, const DBValue& b);
bool operator<(const DBValue& a, const DBValue& b);
bool operator>(const DBValue& a, const DBValue& b);
bool operator<=(const DBValue& a, const DBValue& b);
bool operator>=(const DBValue& a, const DBValue& b);

// Arithmetic operators yield a new heap-allocated DBValue.
DBValue* addValues(const DBValue& a, const DBValue& b);
DBValue* subValues(const DBValue& a, const DBValue& b);
DBValue* mulValues(const DBValue& a, const DBValue& b);
DBValue* divValues(const DBValue& a, const DBValue& b);
DBValue* modValues(const DBValue& a, const DBValue& b);

// Deserialize: reads tag byte then payload. Returns new DBValue* and
// advances *off by total bytes consumed.
DBValue* deserializeValue(const unsigned char* buf, std::size_t cap, std::size_t* off);

} // namespace nanodb
