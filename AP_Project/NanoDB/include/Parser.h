#pragma once
// Expression parser using Shunting-Yard (custom Stack only).
// Converts the token sequence of a WHERE expression into a postfix
// (RPN) array. The Evaluator then runs that postfix tape per row.

#include "Tokenizer.h"
#include "DynArray.h"
#include "String.h"
#include "DBValue.h"

namespace nanodb {

// Postfix term kinds
enum class PostKind {
    NUMBER,    // numeric literal
    STRING,    // string literal
    IDENT,     // column reference
    OP         // operator: + - * / % == != < <= > >= AND OR NOT
};

struct PostTerm {
    PostKind kind;
    String   text;     // for STRING and IDENT and OP
    long long intVal;
    double  fltVal;
    bool    isFloat;
    PostTerm() : kind(PostKind::NUMBER), text(""), intVal(0), fltVal(0.0), isFloat(false) {}
};

// Parses an expression. The caller passes a slice [start, end) of tokens.
// On success the postfix output is appended to outPostfix.
class ExpressionParser {
public:
    // Returns true on success.
    static bool parseToPostfix(const DynArray<Token>& tokens,
                               std::size_t start, std::size_t endExclusive,
                               DynArray<PostTerm>& outPostfix,
                               String* errMsg = nullptr);

    // Pretty-printed RPN for logging.
    static String postfixToString(const DynArray<PostTerm>& pf);
};

// Top-level command kinds the Engine understands.
enum class CmdKind {
    UNKNOWN,
    CREATE_TABLE,
    INSERT,
    SELECT,
    UPDATE,
    DELETE_,
    LOAD_TBL,    // LOAD <table> FROM 'path'
    BUILD_INDEX, // INDEX <table> ON <col>
    JOIN,        // SELECT JOIN A B C ... (3-table MST demo)
    BENCH_SCAN,  // BENCH SCAN <table> <col> <val>  (forced sequential)
    BENCH_INDEX, // BENCH INDEX <table> <col> <val> (force index)
    SHUTDOWN,
    REBOOT,
    PRIORITY_FLUSH, // PRIORITY FLUSH
    HELP
};

struct ParsedCommand {
    CmdKind kind;
    String  table;         // primary table
    DynArray<String> tables;     // for joins
    DynArray<String> columns;    // column list (for CREATE TABLE & SELECT cols)
    DynArray<DType>  colTypes;   // matched 1:1 with columns for CREATE TABLE
    DynArray<String> values;     // raw values for INSERT / UPDATE rhs
    DynArray<DType>  valueTypes; // type tags for the values
    DynArray<PostTerm> wherePostfix;
    bool     hasWhere;
    String   path;          // for LOAD
    String   updateCol;     // UPDATE SET <col> = <value>
    String   updateValueRaw;
    DType    updateValueType;
    bool     adminPriority; // ADMIN UPDATE ...
    String   benchCol;
    String   benchVal;

    ParsedCommand() : kind(CmdKind::UNKNOWN), table(""), hasWhere(false), path(""),
                      updateCol(""), updateValueRaw(""), updateValueType(DType::NIL),
                      adminPriority(false), benchCol(""), benchVal("") {}
};

class CommandParser {
public:
    static ParsedCommand parse(const String& sql, String* err = nullptr);
};

} // namespace nanodb
