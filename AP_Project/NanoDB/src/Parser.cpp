#include "Parser.h"
#include "Stack.h"
#include "Logger.h"

#include <cstring>
#include <cctype>

namespace nanodb {

// ----- Operator precedence -----
// Higher number = binds tighter.  NOT and unary-minus are right-associative.
static int precedence(const String& op) {
    if (op == "OR")  return 1;
    if (op == "AND") return 2;
    if (op == "NOT") return 3;                          // unary, right-assoc
    if (op == "==" || op == "!=") return 4;
    if (op == "<" || op == "<=" || op == ">" || op == ">=") return 5;
    if (op == "+" || op == "-") return 6;
    if (op == "*" || op == "/" || op == "%") return 7;
    if (op == "u-") return 8;                           // unary minus
    return 0;
}

static bool isRightAssoc(const String& op) {
    return op == "NOT" || op == "u-";
}

static bool isOperator(const Token& t) { return t.kind == TokKind::OP; }

bool ExpressionParser::parseToPostfix(const DynArray<Token>& tokens,
                                      std::size_t start, std::size_t endExclusive,
                                      DynArray<PostTerm>& outPostfix,
                                      String* errMsg) {
    Stack<String> ops;
    bool prevWasOperand = false;

    auto emitOp = [&](const String& op) {
        PostTerm t; t.kind = PostKind::OP; t.text = op;
        outPostfix.push_back(t);
    };

    for (std::size_t i = start; i < endExclusive; ++i) {
        const Token& tk = tokens[i];

        if (tk.kind == TokKind::NUMBER) {
            PostTerm t; t.kind = PostKind::NUMBER;
            t.text = tk.text; t.intVal = tk.intVal; t.fltVal = tk.fltVal; t.isFloat = tk.isFloat;
            outPostfix.push_back(t);
            prevWasOperand = true;
        } else if (tk.kind == TokKind::STRING) {
            PostTerm t; t.kind = PostKind::STRING; t.text = tk.text;
            outPostfix.push_back(t);
            prevWasOperand = true;
        } else if (tk.kind == TokKind::IDENT) {
            PostTerm t; t.kind = PostKind::IDENT; t.text = tk.text;
            outPostfix.push_back(t);
            prevWasOperand = true;
        } else if (tk.kind == TokKind::LPAREN) {
            ops.push(String("("));
            prevWasOperand = false;
        } else if (tk.kind == TokKind::RPAREN) {
            bool found = false;
            while (!ops.empty()) {
                if (ops.top() == "(") { ops.pop(); found = true; break; }
                emitOp(ops.top()); ops.pop();
            }
            if (!found) {
                if (errMsg) *errMsg = "Mismatched parentheses";
                return false;
            }
            prevWasOperand = true;
        } else if (isOperator(tk)) {
            String op = tk.text;
            if (op == "-" && !prevWasOperand) op = "u-"; // unary minus
            int  myP  = precedence(op);
            bool myRA = isRightAssoc(op);
            while (!ops.empty()) {
                const String& topOp = ops.top();
                if (topOp == "(") break;
                int tp = precedence(topOp);
                if (myRA ? tp > myP : tp >= myP) {
                    emitOp(topOp);
                    ops.pop();
                } else break;
            }
            ops.push(op);
            prevWasOperand = false;
        } else {
            if (errMsg) { *errMsg = String("Unexpected token: '") + tk.text + "'"; }
            return false;
        }
    }
    while (!ops.empty()) {
        if (ops.top() == "(") {
            if (errMsg) *errMsg = "Mismatched parentheses";
            return false;
        }
        emitOp(ops.top()); ops.pop();
    }
    return true;
}

String ExpressionParser::postfixToString(const DynArray<PostTerm>& pf) {
    String out;
    for (std::size_t i = 0; i < pf.size(); ++i) {
        if (i) out += ' ';
        const PostTerm& t = pf[i];
        if (t.kind == PostKind::STRING) {
            out += '"';
            out += t.text;
            out += '"';
        } else {
            out += t.text;
        }
    }
    return out;
}

// ---- Helpers ----

static bool ieq(const String& a, const char* b) {
    std::size_t n = a.size();
    if (std::strlen(b) != n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        char ca = a[i]; if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
        char cb = b[i]; if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
        if (ca != cb) return false;
    }
    return true;
}

static DType identToType(const String& s) {
    if (ieq(s, "INT")) return DType::INT;
    if (ieq(s, "FLOAT") || ieq(s, "DOUBLE") || ieq(s, "DECIMAL")) return DType::FLOAT;
    if (ieq(s, "STRING") || ieq(s, "VARCHAR") || ieq(s, "TEXT") || ieq(s, "CHAR")) return DType::STRING;
    return DType::NIL;
}

// Advance wend past the WHERE expression, stopping before any clause keyword
// (ORDER, GROUP, LIMIT, HAVING) so they are not fed into the expression parser.
static std::size_t scanWhereEnd(const DynArray<Token>& tokens, std::size_t start) {
    std::size_t wend = start;
    while (wend < tokens.size() &&
           tokens[wend].kind != TokKind::END &&
           tokens[wend].kind != TokKind::SEMI) {
        if (tokens[wend].kind == TokKind::IDENT) {
            const String& kw = tokens[wend].text;
            if (ieq(kw, "ORDER") || ieq(kw, "GROUP") ||
                ieq(kw, "LIMIT") || ieq(kw, "HAVING")) break;
        }
        ++wend;
    }
    return wend;
}

// Silently consume ORDER BY / GROUP BY / HAVING / LIMIT / OFFSET clauses.
// The engine does not yet execute these, but accepting them lets the parser
// handle richer SQL without returning "Unknown command".
static std::size_t consumePostSelectClauses(const DynArray<Token>& tokens,
                                            std::size_t i) {
    bool consumed = true;
    while (consumed && i < tokens.size() &&
           tokens[i].kind != TokKind::END &&
           tokens[i].kind != TokKind::SEMI) {
        consumed = false;

        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "GROUP")) {
            ++i; consumed = true;
            if (i < tokens.size() && tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "BY")) ++i;
            // consume column list until next clause keyword or end
            while (i < tokens.size() &&
                   tokens[i].kind != TokKind::END &&
                   tokens[i].kind != TokKind::SEMI) {
                if (tokens[i].kind == TokKind::IDENT) {
                    const String& kw = tokens[i].text;
                    if (ieq(kw, "HAVING") || ieq(kw, "ORDER") || ieq(kw, "LIMIT")) break;
                }
                ++i;
            }
        }

        if (i < tokens.size() && tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "HAVING")) {
            ++i; consumed = true;
            while (i < tokens.size() &&
                   tokens[i].kind != TokKind::END &&
                   tokens[i].kind != TokKind::SEMI) {
                if (tokens[i].kind == TokKind::IDENT) {
                    const String& kw = tokens[i].text;
                    if (ieq(kw, "ORDER") || ieq(kw, "LIMIT")) break;
                }
                ++i;
            }
        }

        if (i < tokens.size() && tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "ORDER")) {
            ++i; consumed = true;
            if (i < tokens.size() && tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "BY")) ++i;
            while (i < tokens.size() &&
                   tokens[i].kind != TokKind::END &&
                   tokens[i].kind != TokKind::SEMI) {
                if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "LIMIT")) break;
                ++i;
            }
        }

        if (i < tokens.size() && tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "LIMIT")) {
            ++i; consumed = true;
            if (i < tokens.size() && tokens[i].kind == TokKind::NUMBER) ++i;
            if (i < tokens.size() && tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "OFFSET")) {
                ++i;
                if (i < tokens.size() && tokens[i].kind == TokKind::NUMBER) ++i;
            }
        }
    }
    return i;
}

// ---- Command parser ----

ParsedCommand CommandParser::parse(const String& sql, String* err) {
    ParsedCommand cmd;
    DynArray<Token> tokens = Tokenizer::tokenize(sql);
    if (tokens.size() == 0 || tokens[0].kind == TokKind::END) {
        if (err) *err = "Empty input";
        return cmd;
    }
    std::size_t i = 0;

    // Optional ADMIN priority modifier
    if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "ADMIN")) {
        cmd.adminPriority = true;
        ++i;
    }

    if (tokens[i].kind != TokKind::IDENT) {
        if (err) *err = "Expected command keyword";
        return cmd;
    }

    const String& kw = tokens[i].text;

    // ---- CREATE TABLE ----
    if (ieq(kw, "CREATE")) {
        cmd.kind = CmdKind::CREATE_TABLE;
        ++i;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "TABLE")) ++i;
        if (tokens[i].kind != TokKind::IDENT) {
            if (err) *err = "Expected table name";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        cmd.table = tokens[i++].text;
        if (tokens[i].kind != TokKind::LPAREN) {
            if (err) *err = "Expected '('";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        ++i;
        while (tokens[i].kind != TokKind::RPAREN && tokens[i].kind != TokKind::END) {
            if (tokens[i].kind != TokKind::IDENT) { ++i; continue; }
            String col = tokens[i++].text;
            DType ty = DType::STRING;
            if (tokens[i].kind == TokKind::IDENT) {
                ty = identToType(tokens[i].text);
                ++i;
            }
            cmd.columns.push_back(col);
            cmd.colTypes.push_back(ty);
            if (tokens[i].kind == TokKind::COMMA) ++i;
        }
        if (tokens[i].kind == TokKind::RPAREN) ++i;
        return cmd;
    }

    // ---- INSERT ----
    if (ieq(kw, "INSERT")) {
        cmd.kind = CmdKind::INSERT;
        ++i;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "INTO")) ++i;
        if (tokens[i].kind != TokKind::IDENT) {
            if (err) *err = "Expected table name";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        cmd.table = tokens[i++].text;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "VALUES")) ++i;
        if (tokens[i].kind != TokKind::LPAREN) {
            if (err) *err = "Expected '('";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        ++i;
        while (tokens[i].kind != TokKind::RPAREN && tokens[i].kind != TokKind::END) {
            const Token& tk = tokens[i];
            if (tk.kind == TokKind::NUMBER) {
                cmd.values.push_back(tk.text);
                cmd.valueTypes.push_back(tk.isFloat ? DType::FLOAT : DType::INT);
            } else if (tk.kind == TokKind::STRING) {
                cmd.values.push_back(tk.text);
                cmd.valueTypes.push_back(DType::STRING);
            } else if (tk.kind == TokKind::IDENT) {
                cmd.values.push_back(tk.text);
                cmd.valueTypes.push_back(DType::STRING);
            } else if (tk.kind == TokKind::OP && tk.text == "-") {
                ++i;
                if (tokens[i].kind == TokKind::NUMBER) {
                    String neg = String("-") + tokens[i].text;
                    cmd.values.push_back(neg);
                    cmd.valueTypes.push_back(tokens[i].isFloat ? DType::FLOAT : DType::INT);
                }
            }
            ++i;
            if (tokens[i].kind == TokKind::COMMA) ++i;
        }
        if (tokens[i].kind == TokKind::RPAREN) ++i;
        return cmd;
    }

    // ---- LOAD ----
    if (ieq(kw, "LOAD")) {
        cmd.kind = CmdKind::LOAD_TBL;
        ++i;
        if (tokens[i].kind != TokKind::IDENT) {
            if (err) *err = "Expected table";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        cmd.table = tokens[i++].text;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "FROM")) ++i;
        if (tokens[i].kind == TokKind::STRING)      cmd.path = tokens[i++].text;
        else if (tokens[i].kind == TokKind::IDENT)  cmd.path = tokens[i++].text;
        return cmd;
    }

    // ---- INDEX ----
    if (ieq(kw, "INDEX")) {
        cmd.kind = CmdKind::BUILD_INDEX;
        ++i;
        if (tokens[i].kind != TokKind::IDENT) {
            if (err) *err = "Expected table";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        cmd.table = tokens[i++].text;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "ON")) ++i;
        if (tokens[i].kind == TokKind::IDENT) cmd.columns.push_back(tokens[i++].text);
        return cmd;
    }

    // ---- BENCH ----
    if (ieq(kw, "BENCH")) {
        ++i;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "SCAN")) {
            cmd.kind = CmdKind::BENCH_SCAN; ++i;
        } else if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "INDEX")) {
            cmd.kind = CmdKind::BENCH_INDEX; ++i;
        } else {
            if (err) *err = "BENCH SCAN|INDEX expected";
            return cmd;
        }
        if (tokens[i].kind == TokKind::IDENT) cmd.table    = tokens[i++].text;
        if (tokens[i].kind == TokKind::IDENT) cmd.benchCol = tokens[i++].text;
        if      (tokens[i].kind == TokKind::NUMBER) cmd.benchVal = tokens[i++].text;
        else if (tokens[i].kind == TokKind::STRING) cmd.benchVal = tokens[i++].text;
        else if (tokens[i].kind == TokKind::IDENT)  cmd.benchVal = tokens[i++].text;
        return cmd;
    }

    // ---- UPDATE ----
    if (ieq(kw, "UPDATE")) {
        cmd.kind = CmdKind::UPDATE;
        ++i;
        if (tokens[i].kind != TokKind::IDENT) {
            if (err) *err = "Expected table";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        cmd.table = tokens[i++].text;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "SET")) ++i;
        if (tokens[i].kind != TokKind::IDENT) {
            if (err) *err = "Expected column";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        cmd.updateCol = tokens[i++].text;
        if (tokens[i].kind == TokKind::OP &&
            (tokens[i].text == "=" || tokens[i].text == "==")) ++i;
        if (tokens[i].kind == TokKind::NUMBER) {
            cmd.updateValueRaw  = tokens[i].text;
            cmd.updateValueType = tokens[i].isFloat ? DType::FLOAT : DType::INT;
            ++i;
        } else if (tokens[i].kind == TokKind::STRING) {
            cmd.updateValueRaw  = tokens[i].text;
            cmd.updateValueType = DType::STRING;
            ++i;
        } else if (tokens[i].kind == TokKind::OP && tokens[i].text == "-") {
            ++i;
            if (tokens[i].kind == TokKind::NUMBER) {
                cmd.updateValueRaw  = String("-") + tokens[i].text;
                cmd.updateValueType = tokens[i].isFloat ? DType::FLOAT : DType::INT;
                ++i;
            }
        } else if (tokens[i].kind == TokKind::IDENT) {
            cmd.updateValueRaw  = tokens[i++].text;
            cmd.updateValueType = DType::STRING;
        }
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "WHERE")) {
            ++i;
            std::size_t wstart = i;
            std::size_t wend   = scanWhereEnd(tokens, wstart);
            String e;
            if (!ExpressionParser::parseToPostfix(tokens, wstart, wend, cmd.wherePostfix, &e)) {
                if (err) *err = e;
            }
            cmd.hasWhere = true;
        }
        return cmd;
    }

    // ---- DELETE ----
    if (ieq(kw, "DELETE")) {
        cmd.kind = CmdKind::DELETE_;
        ++i;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "FROM")) ++i;
        if (tokens[i].kind != TokKind::IDENT) {
            if (err) *err = "Expected table";
            cmd.kind = CmdKind::UNKNOWN; return cmd;
        }
        cmd.table = tokens[i++].text;
        if (tokens[i].kind == TokKind::IDENT && ieq(tokens[i].text, "WHERE")) {
            ++i;
            std::size_t wstart = i;
            std::size_t wend   = scanWhereEnd(tokens, wstart);
            String e;
            ExpressionParser::parseToPostfix(tokens, wstart, wend, cmd.wherePostfix, &e);
            cmd.hasWhere = true;
        }
        return cmd;
    }

    // ---- SELECT (and JOIN when multiple tables) ----
    if (ieq(kw, "SELECT")) {
        cmd.kind = CmdKind::SELECT;
        ++i;

        // Optional DISTINCT: accepted and silently ignored (no dedup engine yet).
        if (i < tokens.size() && tokens[i].kind == TokKind::IDENT &&
            ieq(tokens[i].text, "DISTINCT")) {
            ++i;
        }

        // Column list
        if (tokens[i].kind == TokKind::STAR) {
            cmd.columns.push_back(String("*"));
            ++i;
        } else {
            while (tokens[i].kind == TokKind::IDENT &&
                   !ieq(tokens[i].text, "FROM") &&
                   !ieq(tokens[i].text, "JOIN") &&
                   !ieq(tokens[i].text, "WHERE")) {
                cmd.columns.push_back(tokens[i++].text);
                // Handle optional AS alias: consume alias name, keep original col.
                if (i < tokens.size() &&
                    tokens[i].kind == TokKind::IDENT &&
                    ieq(tokens[i].text, "AS")) {
                    ++i; // skip AS
                    if (i < tokens.size() && tokens[i].kind == TokKind::IDENT) ++i; // skip alias
                }
                if (tokens[i].kind == TokKind::COMMA) ++i;
            }
            if (cmd.columns.size() == 0) {
                cmd.columns.push_back(String("*"));
            }
        }

        // FROM
        if (i < tokens.size() && tokens[i].kind == TokKind::IDENT &&
            ieq(tokens[i].text, "FROM")) ++i;
        if (i < tokens.size() && tokens[i].kind == TokKind::IDENT) {
            cmd.table = tokens[i].text;
            cmd.tables.push_back(tokens[i].text);
            ++i;
        }

        // JOIN list with optional ON clause
        while (i < tokens.size() &&
               tokens[i].kind == TokKind::IDENT &&
               ieq(tokens[i].text, "JOIN")) {
            ++i;
            if (i < tokens.size() && tokens[i].kind == TokKind::IDENT) {
                cmd.tables.push_back(tokens[i++].text);
            }
            // Optional ON condition: consume until WHERE / JOIN / END / SEMI.
            // The join optimizer uses FK conventions, so explicit ON is ignored.
            if (i < tokens.size() && tokens[i].kind == TokKind::IDENT &&
                ieq(tokens[i].text, "ON")) {
                while (i < tokens.size() &&
                       tokens[i].kind != TokKind::END &&
                       tokens[i].kind != TokKind::SEMI &&
                       !(tokens[i].kind == TokKind::IDENT &&
                         (ieq(tokens[i].text, "WHERE") ||
                          ieq(tokens[i].text, "JOIN")))) {
                    ++i;
                }
            }
        }

        if (cmd.tables.size() >= 2) cmd.kind = CmdKind::JOIN;

        // WHERE clause — stop before ORDER / GROUP / LIMIT / HAVING so those
        // tokens are not fed into the expression parser as column references.
        if (i < tokens.size() &&
            tokens[i].kind == TokKind::IDENT &&
            ieq(tokens[i].text, "WHERE")) {
            ++i;
            std::size_t wstart = i;
            std::size_t wend   = scanWhereEnd(tokens, wstart);
            String e;
            ExpressionParser::parseToPostfix(tokens, wstart, wend, cmd.wherePostfix, &e);
            cmd.hasWhere = true;
            i = wend; // advance past the expression
        }

        // Consume any trailing ORDER BY / GROUP BY / HAVING / LIMIT / OFFSET.
        i = consumePostSelectClauses(tokens, i);

        return cmd;
    }

    // ---- Utility commands ----
    if (ieq(kw, "SHUTDOWN") || ieq(kw, "EXIT") || ieq(kw, "QUIT")) {
        cmd.kind = CmdKind::SHUTDOWN; return cmd;
    }
    if (ieq(kw, "REBOOT") || ieq(kw, "RESTART")) {
        cmd.kind = CmdKind::REBOOT; return cmd;
    }
    if (ieq(kw, "HELP"))     { cmd.kind = CmdKind::HELP;           return cmd; }
    if (ieq(kw, "PRIORITY")) { cmd.kind = CmdKind::PRIORITY_FLUSH; return cmd; }

    if (err) *err = String("Unknown command: ") + kw;
    return cmd;
}

} // namespace nanodb
