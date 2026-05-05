#include "DBValue.h"

#include <cstring>

namespace nanodb {

static bool writeByte(unsigned char* buf, std::size_t cap, std::size_t& off, unsigned char b) {
    if (off + 1 > cap) return false;
    buf[off++] = b;
    return true;
}

static bool writeBytes(unsigned char* buf, std::size_t cap, std::size_t& off, const void* src, std::size_t n) {
    if (off + n > cap) return false;
    std::memcpy(buf + off, src, n);
    off += n;
    return true;
}

std::size_t NullValue::serialize(unsigned char* buf, std::size_t cap, std::size_t off) const {
    std::size_t start = off;
    if (!writeByte(buf, cap, off, (unsigned char)DType::NIL)) return 0;
    return off - start;
}

std::size_t IntValue::serialize(unsigned char* buf, std::size_t cap, std::size_t off) const {
    std::size_t start = off;
    if (!writeByte(buf, cap, off, (unsigned char)DType::INT)) return 0;
    if (!writeBytes(buf, cap, off, &v, sizeof(v))) return 0;
    return off - start;
}

std::size_t FloatValue::serialize(unsigned char* buf, std::size_t cap, std::size_t off) const {
    std::size_t start = off;
    if (!writeByte(buf, cap, off, (unsigned char)DType::FLOAT)) return 0;
    if (!writeBytes(buf, cap, off, &v, sizeof(v))) return 0;
    return off - start;
}

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
    if ((ta == DType::INT || ta == DType::FLOAT) &&
        (tb == DType::INT || tb == DType::FLOAT)) {
        double da = a.asDouble(), db = b.asDouble();
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }
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

DBValue* addValues(const DBValue& a, const DBValue& b) {
    if (a.type() == DType::STRING || b.type() == DType::STRING) {
        return new StringValue(a.toString() + b.toString());
    }
    if (a.type() == DType::FLOAT || b.type() == DType::FLOAT) {
        return new FloatValue(a.asDouble() + b.asDouble());
    }
    return new IntValue(a.asInt() + b.asInt());
}

DBValue* subValues(const DBValue& a, const DBValue& b) {
    if (a.type() == DType::FLOAT || b.type() == DType::FLOAT) {
        return new FloatValue(a.asDouble() - b.asDouble());
    }
    return new IntValue(a.asInt() - b.asInt());
}

DBValue* mulValues(const DBValue& a, const DBValue& b) {
    if (a.type() == DType::FLOAT || b.type() == DType::FLOAT) {
        return new FloatValue(a.asDouble() * b.asDouble());
    }
    return new IntValue(a.asInt() * b.asInt());
}

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

DBValue* modValues(const DBValue& a, const DBValue& b) {
    long long bi = b.asInt();
    if (bi == 0) return new IntValue(0);
    return new IntValue(a.asInt() % bi);
}

} // namespace nanodb
