#pragma once
// Schema = ordered list of (column name, type). Columns are addressed
// by index for fast row access; the parser resolves column names to
// indices once during query compilation.

#include "String.h"
#include "DynArray.h"
#include "DBValue.h"

namespace nanodb {

struct Column {
    String name;
    DType  type;
    Column() : name(""), type(DType::NIL) {}
    Column(const String& n, DType t) : name(n), type(t) {}
};

class Schema {
public:
    Schema() {}
    void addColumn(const String& name, DType t) { cols_.push_back(Column(name, t)); }

    int indexOf(const String& name) const {
        for (std::size_t i = 0; i < cols_.size(); ++i) {
            if (cols_[i].name == name) return (int)i;
        }
        return -1;
    }

    std::size_t size() const { return cols_.size(); }
    const Column& at(std::size_t i) const { return cols_[i]; }
    const Column& operator[](std::size_t i) const { return cols_[i]; }

private:
    DynArray<Column> cols_;
};

} // namespace nanodb
