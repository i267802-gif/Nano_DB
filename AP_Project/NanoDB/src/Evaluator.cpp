#include "Evaluator.h"
#include "Stack.h"

namespace nanodb {

// Returns a heap-allocated IntValue of 1 (true) or 0 (false).
static DBValue* boolVal(bool b) { return new IntValue(b ? 1 : 0); }

// Resolves a single postfix term to its DBValue.
// - NUMBER: constructs a float or integer literal.
// - STRING: constructs a string literal.
// - IDENT:  looks up the column value from the current row; falls back to
//           a string literal for unrecognized identifiers so expressions
//           like `c_mktsegment == BUILDING` don't crash.
static DBValue* termToValue(const PostTerm& t, const Schema& schema, const Row& row, bool* found) {
    *found = true;
    if (t.kind == PostKind::NUMBER) {
        if (t.isFloat) return new FloatValue(t.fltVal);
        return new IntValue(t.intVal);
    }
    if (t.kind == PostKind::STRING) {
        return new StringValue(t.text);
    }
    if (t.kind == PostKind::IDENT) {
        int idx = schema.indexOf(t.text);
        if (idx < 0) {
            *found = false;
            return new StringValue(t.text);
        }
        DBValue* v = row[(std::size_t)idx];
        return v ? v->clone() : new NullValue();
    }
    *found = false;
    return new NullValue();
}

// Evaluates a postfix expression against a single row using a value stack.
// Operands are pushed directly; operators pop their operands, compute, and
// push the result. Returns the top of the stack on success, nullptr on error.
DBValue* Evaluator::evaluate(const DynArray<PostTerm>& postfix,
                             const Schema& schema,
                             const Row& row) {
    Stack<DBValue*> st;
    for (std::size_t i = 0; i < postfix.size(); ++i) {
        const PostTerm& t = postfix[i];
        if (t.kind != PostKind::OP) {
            bool ok;
            DBValue* v = termToValue(t, schema, row, &ok);
            st.push(v);
            continue;
        }

        const String& op = t.text;

        // Unary operators: NOT, u-
        if (op == "NOT") {
            if (st.empty()) return nullptr;
            DBValue* a = st.top(); st.pop();
            bool truthy = a->asDouble() != 0.0;
            delete a;
            st.push(boolVal(!truthy));
            continue;
        }
        if (op == "u-") {
            if (st.empty()) return nullptr;
            DBValue* a = st.top(); st.pop();
            DBValue* r;
            if (a->type() == DType::FLOAT) r = new FloatValue(-a->asDouble());
            else                           r = new IntValue(-a->asInt());
            delete a;
            st.push(r);
            continue;
        }

        // Binary operators — need at least two operands.
        if (st.size() < 2) {
            while (!st.empty()) { delete st.top(); st.pop(); }
            return nullptr;
        }
        DBValue* b = st.top(); st.pop();
        DBValue* a = st.top(); st.pop();

        DBValue* r = nullptr;
        if      (op == "+")   r = addValues(*a, *b);
        else if (op == "-")   r = subValues(*a, *b);
        else if (op == "*")   r = mulValues(*a, *b);
        else if (op == "/")   r = divValues(*a, *b);
        else if (op == "%")   r = modValues(*a, *b);
        else if (op == "==")  r = boolVal(*a == *b);
        else if (op == "!=")  r = boolVal(*a != *b);
        else if (op == "<")   r = boolVal(*a <  *b);
        else if (op == "<=")  r = boolVal(*a <= *b);
        else if (op == ">")   r = boolVal(*a >  *b);
        else if (op == ">=")  r = boolVal(*a >= *b);
        else if (op == "AND") {
            bool x = a->asDouble() != 0.0;
            bool y = b->asDouble() != 0.0;
            r = boolVal(x && y);
        } else if (op == "OR") {
            bool x = a->asDouble() != 0.0;
            bool y = b->asDouble() != 0.0;
            r = boolVal(x || y);
        } else {
            r = new NullValue();
        }
        delete a; delete b;
        st.push(r);
    }
    if (st.empty()) return nullptr;
    DBValue* res = st.top(); st.pop();
    // Discard any leftover values (malformed expression guard).
    while (!st.empty()) { delete st.top(); st.pop(); }
    return res;
}

// Evaluates the postfix expression and interprets the result as a boolean.
// A non-zero numeric result is truthy; nullptr or zero is false.
bool Evaluator::evalPredicate(const DynArray<PostTerm>& postfix,
                              const Schema& schema,
                              const Row& row) {
    DBValue* v = evaluate(postfix, schema, row);
    if (!v) return false;
    bool truthy = v->asDouble() != 0.0;
    delete v;
    return truthy;
}

} // namespace nanodb
