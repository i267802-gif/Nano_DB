#include "Evaluator.h"
#include "Stack.h"

namespace nanodb {

static DBValue* boolVal(bool b) { return new IntValue(b ? 1 : 0); }

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
            // Could be an unknown identifier; treat as string literal so
            // exprs like `c_mktsegment == BUILDING` don't crash.
            *found = false;
            return new StringValue(t.text);
        }
        DBValue* v = row[(std::size_t)idx];
        return v ? v->clone() : new NullValue();
    }
    *found = false;
    return new NullValue();
}

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
            else r = new IntValue(-a->asInt());
            delete a;
            st.push(r);
            continue;
        }

        // Binary operators
        if (st.size() < 2) {
            while (!st.empty()) { delete st.top(); st.pop(); }
            return nullptr;
        }
        DBValue* b = st.top(); st.pop();
        DBValue* a = st.top(); st.pop();

        DBValue* r = nullptr;
        if (op == "+") r = addValues(*a, *b);
        else if (op == "-") r = subValues(*a, *b);
        else if (op == "*") r = mulValues(*a, *b);
        else if (op == "/") r = divValues(*a, *b);
        else if (op == "%") r = modValues(*a, *b);
        else if (op == "==") r = boolVal(*a == *b);
        else if (op == "!=") r = boolVal(*a != *b);
        else if (op == "<")  r = boolVal(*a < *b);
        else if (op == "<=") r = boolVal(*a <= *b);
        else if (op == ">")  r = boolVal(*a > *b);
        else if (op == ">=") r = boolVal(*a >= *b);
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
    while (!st.empty()) { delete st.top(); st.pop(); }
    return res;
}

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
