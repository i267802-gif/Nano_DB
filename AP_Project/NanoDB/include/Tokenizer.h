#pragma once
// Tokenizer breaks a SQL/expression string into typed tokens consumed
// by the Parser. Recognizes:
//   - Identifiers (column / table / keyword names)
//   - Numbers (int / float)
//   - String literals "..."
//   - Operators: == != < <= > >= + - * / % AND OR NOT
//   - Parentheses ( )
//   - Comma , semicolon ;
//   - Equals = (used in UPDATE SET col = value)

#include "String.h"
#include "DynArray.h"

namespace nanodb {

enum class TokKind {
    END,
    IDENT,
    NUMBER,
    STRING,
    OP,        // operator or keyword like AND/OR/NOT
    LPAREN,
    RPAREN,
    COMMA,
    SEMI,
    STAR       // '*' as in SELECT *
};

struct Token {
    TokKind kind;
    String  text;        // raw text
    long long intVal;
    double  fltVal;
    bool    isFloat;     // for NUMBER

    Token() : kind(TokKind::END), text(""), intVal(0), fltVal(0.0), isFloat(false) {}
};

class Tokenizer {
public:
    static DynArray<Token> tokenize(const String& src);
};

} // namespace nanodb
