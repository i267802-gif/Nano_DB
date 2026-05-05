#include "Tokenizer.h"

#include <cctype>
#include <cstring>

namespace nanodb {

static bool ieq(const String& a, const char* b) {
    std::size_t n = a.size();
    if (std::strlen(b) != n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        char ca = a[i]; if (ca >= 'a' && ca <= 'z') ca = ca - 'a' + 'A';
        char cb = b[i]; if (cb >= 'a' && cb <= 'z') cb = cb - 'a' + 'A';
        if (ca != cb) return false;
    }
    return true;
}

DynArray<Token> Tokenizer::tokenize(const String& src) {
    DynArray<Token> out;
    std::size_t i = 0;
    std::size_t n = src.size();
    const char* s = src.c_str();
    while (i < n) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++i; continue; }

        // String literal: "..." with simple backslash escapes
        if (c == '"' || c == '\'') {
            char q = c; ++i;
            String lit;
            while (i < n && s[i] != q) {
                if (s[i] == '\\' && i + 1 < n) {
                    char nx = s[i + 1];
                    if (nx == 'n') lit.push_back('\n');
                    else if (nx == 't') lit.push_back('\t');
                    else lit.push_back(nx);
                    i += 2;
                } else {
                    lit.push_back(s[i++]);
                }
            }
            if (i < n) ++i; // skip closing quote
            Token t; t.kind = TokKind::STRING; t.text = lit;
            out.push_back(t);
            continue;
        }

        // Number (int or float). Negative numbers handled as unary in
        // expressions; here we detect digit start.
        if (c >= '0' && c <= '9') {
            std::size_t st = i;
            bool isf = false;
            while (i < n && ((s[i] >= '0' && s[i] <= '9') || s[i] == '.')) {
                if (s[i] == '.') isf = true;
                ++i;
            }
            String numText(s + st, i - st);
            Token t;
            t.kind = TokKind::NUMBER;
            t.text = numText;
            t.isFloat = isf;
            if (isf) t.fltVal = numText.toDouble();
            else     t.intVal = numText.toInt();
            out.push_back(t);
            continue;
        }

        // Identifier / keyword
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
            std::size_t st = i;
            while (i < n && ((s[i] >= 'A' && s[i] <= 'Z') ||
                             (s[i] >= 'a' && s[i] <= 'z') ||
                             (s[i] >= '0' && s[i] <= '9') ||
                              s[i] == '_')) {
                ++i;
            }
            String ident(s + st, i - st);
            Token t; t.text = ident;
            if (ieq(ident, "AND") || ieq(ident, "OR") || ieq(ident, "NOT")) {
                t.kind = TokKind::OP;
                if      (ieq(ident, "AND")) t.text = "AND";
                else if (ieq(ident, "OR"))  t.text = "OR";
                else                        t.text = "NOT";
            } else {
                t.kind = TokKind::IDENT;
            }
            out.push_back(t);
            continue;
        }

        // Multi-char operators: == != <= >= && ||
        if (c == '=' && i + 1 < n && s[i + 1] == '=') {
            Token t; t.kind = TokKind::OP; t.text = "=="; out.push_back(t); i += 2; continue;
        }
        if (c == '!' && i + 1 < n && s[i + 1] == '=') {
            Token t; t.kind = TokKind::OP; t.text = "!="; out.push_back(t); i += 2; continue;
        }
        if (c == '<' && i + 1 < n && s[i + 1] == '=') {
            Token t; t.kind = TokKind::OP; t.text = "<="; out.push_back(t); i += 2; continue;
        }
        if (c == '>' && i + 1 < n && s[i + 1] == '=') {
            Token t; t.kind = TokKind::OP; t.text = ">="; out.push_back(t); i += 2; continue;
        }
        if (c == '&' && i + 1 < n && s[i + 1] == '&') {
            Token t; t.kind = TokKind::OP; t.text = "AND"; out.push_back(t); i += 2; continue;
        }
        if (c == '|' && i + 1 < n && s[i + 1] == '|') {
            Token t; t.kind = TokKind::OP; t.text = "OR"; out.push_back(t); i += 2; continue;
        }

        // Single-char tokens
        switch (c) {
            case '(': { Token t; t.kind = TokKind::LPAREN; t.text = "("; out.push_back(t); ++i; continue; }
            case ')': { Token t; t.kind = TokKind::RPAREN; t.text = ")"; out.push_back(t); ++i; continue; }
            case ',': { Token t; t.kind = TokKind::COMMA;  t.text = ","; out.push_back(t); ++i; continue; }
            case ';': { Token t; t.kind = TokKind::SEMI;   t.text = ";"; out.push_back(t); ++i; continue; }
            case '*': { Token t; t.kind = TokKind::STAR;   t.text = "*"; out.push_back(t); ++i; continue; }
            case '+': case '-': case '/': case '%':
            case '<': case '>': case '!': {
                Token t; t.kind = TokKind::OP;
                t.text = ""; t.text += c;
                out.push_back(t); ++i; continue;
            }
            case '=': {
                // Bare '=' as in UPDATE SET col = value (not equality)
                Token t; t.kind = TokKind::OP; t.text = "="; out.push_back(t); ++i; continue;
            }
            default:
                // Unknown character: skip
                ++i;
        }
    }
    Token endTok; endTok.kind = TokKind::END; endTok.text = "";
    out.push_back(endTok);
    return out;
}

} // namespace nanodb
