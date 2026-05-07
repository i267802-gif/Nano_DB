#include "DBValue.h"

#include <cstring>

namespace nanodb {

// Writes a single byte to buf at offset off; returns false if capacity exceeded.
static bool writeByte(unsigned char* buf, std::size_t cap, std::size_t& off, unsigned char b) {
    if (off + 1 > cap) return false;
    buf[off++] = b;
    return true;
}

// Writes n bytes from src to buf at offset off; returns false if capacity exceeded.
static bool writeBytes(unsigned char* buf, std::size_t cap, std::size_t& off, const void* src, std::size_t n) {
    if (off + n > cap) return false;
    std::memcpy(buf + off, src, n);
    off += n;
    return true;
}

// Serializes a NULL value: writes only the type tag.
std::size_t NullValue::serialize(unsigned char* buf, std::size_t cap, std::size_t off) const {
    std::size_t start = off;
    if (!writeByte(buf, cap, off, (unsigned char)DType::NIL)) return 0;
    return off - start;
}

// Serializes an integer value: type tag followed by 8-byte signed integer.
std::size_t IntValue::serialize(unsigned char* buf, std::size_t cap, std::size_t off) const {
    std::size_t start = off;
    if (!writeByte(buf, cap, off, (unsigned char)DType::INT)) return 0;
    if (!writeBytes(buf, cap, off, &v, sizeof(v))) return 0;
    return off - start;
}

// Serializes a float value: type tag followed by 8-byte double.
std::size_t FloatValue::serialize(unsigned char* buf, std::size_t cap, std::size_t off) const {
    std::size_t start = off;
    if (!writeByte(buf, cap, off, (unsigned char)DType::FLOAT)) return 0;
    if (!writeBytes(buf, cap, off, &v, sizeof(v))) return 0;
    return off - start;
}

// Serializes a string value: type tag, 4-byte length, then raw string bytes.
std::size_t StringValue::serialize(unsigned char* buf, std::size_t cap, std::size_t off) const {
    std::size_t start = off;
    if (!writeByte(buf, cap, off, (unsigned char)DType::STRING)) return 0;
    unsigned int n = (unsigned int)v.size();
    if (!writeBytes(buf, cap, off, &n, sizeof(n))) return 0;
    if (n > 0) {
        if (!writeBytes(buf, cap, off, v.c_str(), n)) return 0;
    }
    return off - start;
}

// Deserializes a DBValue from a byte buffer starting at *off.
// Advances *off past the consumed bytes. Returns nullptr on failure.
DBValue* deserializeValue(const unsigned char* buf, std::size_t cap, std::size_t* off) {
    if (*off + 1 > cap) return nullptr;
    unsigned char tag = buf[(*off)++];
    DType t = (DType)tag;
    switch (t) {
        case DType::NIL: return new NullValue();
        case DType::INT: {
            if (*off + sizeof(long long) > cap) return nullptr;
            long long x = 0;
            std::memcpy(&x, buf + *off, sizeof(x));
            *off += sizeof(x);
            return new IntValue(x);
        }
        case DType::FLOAT: {
            if (*off + sizeof(double) > cap) return nullptr;
            double x = 0;
            std::memcpy(&x, buf + *off, sizeof(x));
            *off += sizeof(x);
            return new FloatValue(x);
        }
        case DType::STRING: {
            if (*off + sizeof(unsigned int) > cap) return nullptr;
            unsigned int n = 0;
            std::memcpy(&n, buf + *off, sizeof(n));
            *off += sizeof(n);
            if (*off + n > cap) return nullptr;
            String s((const char*)(buf + *off), n);
            *off += n;
            return new StringValue(s);
        }
    }
    return nullptr;
}

// Compare semantics:
// - INT vs INT: signed integer compare
// - FLOAT vs FLOAT, INT vs FLOAT: double compare (promote)
// - STRING vs STRING: lex compare
// - mismatched non-numeric: compare by toString
static int compare(const DBValue& a, const DBValue& b) {
    DType ta = a.type(), tb = b.type();

    // Numeric comparison: promote both sides to double.
    if ((ta == DType::INT || ta == DType::FLOAT) &&
        (tb == DType::INT || tb == DType::FLOAT)) {
        double da = a.asDouble(), db = b.asDouble();
        if (da < db) return -1;
        if (da > db) return  1;
        return 0;
    }

    // Lexicographic string comparison.
    if (ta == DType::STRING && tb == DType::STRING) {
        const StringValue& sa = static_cast<const StringValue&>(a);
        const StringValue& sb = static_cast<const StringValue&>(b);
        return sa.v.compare(sb.v);
    }

    // Fallback to string comparison
    return a.toString().compare(b.toString());
}

bool operator==(const DBValue& a, const DBValue& b) { return compare(a, b) == 0; }
bool operator!=(const DBValue& a, const DBValue& b) { return compare(a, b) != 0; }
bool operator<(const DBValue& a, const DBValue& b)  { return compare(a, b) <  0; }
bool operator>(const DBValue& a, const DBValue& b)  { return compare(a, b) >  0; }
bool operator<=(const DBValue& a, const DBValue& b) { return compare(a, b) <= 0; }
bool operator>=(const DBValue& a, const DBValue& b) { return compare(a, b) >= 0; }

static bool isNumeric(const DBValue& v) {
    return v.type() == DType::INT || v.type() == DType::FLOAT;
}

// Adds two values. Strings are concatenated; numerics follow float-promotion rules.
DBValue* addValues(const DBValue& c, const DBValue& b) {
    if (c.type() == DType::STRING || b.type() == DType::STRING) {
        return new StringValue(c.toString() + b.toString());
    }
    if (c.type() == DType::FLOAT || b.type() == DType::FLOAT) {
        return new FloatValue(c.asDouble() + b.asDouble());
    }
    return new IntValue(c.asInt() + b.asInt());
}

// Subtracts b from a. Returns float if either operand is float.
DBValue* subValues(const DBValue& a, const DBValue& b) {
    if (a.type() == DType::FLOAT || b.type() == DType::FLOAT) {
        return new FloatValue(a.asDouble() - b.asDouble());
    }
    return new IntValue(a.asInt() - b.asInt());
}

// Multiplies two values. Returns float if either operand is float.
DBValue* mulValues(const DBValue& a, const DBValue& b) {
    if (a.type() == DType::FLOAT || b.type() == DType::FLOAT) {
        return new FloatValue(a.asDouble() * b.asDouble());
    }
    return new IntValue(a.asInt() * b.asInt());
}

// Divides a by b. Guards against division by zero by returning 0.
DBValue* divValues(const DBValue& a, const DBValue& b) {
    double d = b.asDouble();
    if (d == 0.0) return new IntValue(0);
    if (a.type() == DType::FLOAT || b.type() == DType::FLOAT) {
        return new FloatValue(a.asDouble() / d);
    }
    long long bi = b.asInt();
    if (bi == 0) return new IntValue(0);
    return new IntValue(a.asInt() / bi);
}

// Returns a mod b. Guards against modulo by zero by returning 0.
DBValue* modValues(const DBValue& a, const DBValue& b) {
    long long bi = b.asInt();
    if (bi == 0) return new IntValue(0);
    return new IntValue(a.asInt() % bi);
}

} // namespace nanodb
