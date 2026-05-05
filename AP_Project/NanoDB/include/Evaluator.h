#pragma once
// Evaluates a postfix (RPN) expression against a row using the row's
// schema. Resolves IDENT terms to column values; otherwise pushes
// literals; for OPs pops 1 or 2 operands and pushes the result.
//
// Boolean values are represented as IntValue(0) / IntValue(1).

#include "Parser.h"
#include "Schema.h"
#include "Row.h"
#include "DBValue.h"

namespace nanodb {

class Evaluator {
public:
    // Returns a heap-allocated DBValue (caller deletes) representing the
    // expression result. Returns nullptr on failure.
    static DBValue* evaluate(const DynArray<PostTerm>& postfix,
                             const Schema& schema,
                             const Row& row);

    // Convenience: evaluate boolean predicate (true if non-zero numeric).
    static bool evalPredicate(const DynArray<PostTerm>& postfix,
                              const Schema& schema,
                              const Row& row);
};

} // namespace nanodb
