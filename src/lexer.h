// Goose compiler — lexer. A hand-written scanner: one big switch over the next
// character. Input is 0-terminated, so only the 0 case checks for end of input.
#pragma once

namespace goose {

// X-macro token tables: expanded for the enum and for the display-name function.
#define TOKENS_MISC \
    F(T_EOF,      "end of file") \
    F(T_IDENT,    "identifier") \
    F(T_INTLIT,   "integer literal") \
    F(T_FLTLIT,   "float literal") \
    F(T_STRLIT,   "string literal")

#define TOKENS_PUNCT \
    F(T_LPAREN,   "(")   F(T_RPAREN,   ")") \
    F(T_LBRACKET, "[")   F(T_RBRACKET, "]") \
    F(T_LCURLY,   "{")   F(T_RCURLY,   "}") \
    F(T_COMMA,    ",")   F(T_SEMI,     ";")  F(T_COLON, ":")  F(T_QUESTION, "?") \
    F(T_DOT,      ".")   F(T_DOTDOT,   "..") F(T_DOTASSIGN, ".=") \
    F(T_PLUS,     "+")   F(T_MINUS,    "-")  F(T_MUL,   "*")  F(T_DIV, "/")  F(T_MOD, "%") \
    F(T_PLUSEQ,   "+=")  F(T_MINUSEQ,  "-=") F(T_MULEQ, "*=") F(T_DIVEQ, "/=") F(T_MODEQ, "%=") \
    F(T_ANDEQ,    "&=")  F(T_OREQ,     "|=") F(T_XOREQ, "^=") \
    F(T_INC,      "++")  F(T_DEC,      "--") \
    F(T_ASSIGN,   "=")   F(T_EQ,       "==") F(T_NEQ,   "!=") \
    F(T_LT,       "<")   F(T_GT,       ">")  F(T_LTEQ,  "<=") F(T_GTEQ, ">=") \
    F(T_SHL,      "<<")  F(T_SHR,      ">>") \
    F(T_NOT,      "!")   F(T_BITNOT,   "~") \
    F(T_BITAND,   "&")   F(T_BITOR,    "|")  F(T_XOR,   "^") \
    F(T_ANDAND,   "&&")  F(T_OROR,     "||") \
    F(T_ARROW,    "->")  F(T_FATARROW, "=>")

#define TOKENS_KEYWORDS \
    F(T_IMPORT,   "import")    F(T_STRUCT,   "struct")  F(T_ENUM,   "enum") \
    F(T_TYPE,     "type")      F(T_FN,       "fn")      F(T_THREADFN, "thread_fn") \
    F(T_RECURSIVE, "recursive") F(T_LET,     "let")     F(T_VAR,    "var") \
    F(T_PAD,      "pad")       F(T_REUSABLE, "reusable") \
    F(T_IF,       "if")        F(T_ELSE,     "else")    F(T_WHILE,  "while") \
    F(T_FOR,      "for")       F(T_IN,       "in")      F(T_LOOP,   "loop") \
    F(T_BLOCK,    "block")     F(T_GUARD,    "guard")   F(T_MATCH,  "match") \
    F(T_RETURN,   "return")    F(T_FROM,     "from")    F(T_BREAK,  "break") \
    F(T_CONTINUE, "continue")  F(T_AS,       "as") \
    F(T_TRUE,     "true")      F(T_FALSE,    "false")   F(T_NULLLIT, "null") \
    F(T_TBOOL,    "bool")      F(T_TVARINT,  "varint") \
    F(T_TI8,      "i8")  F(T_TI16, "i16") F(T_TI32, "i32") F(T_TI64, "i64") \
    F(T_TU8,      "u8")  F(T_TU16, "u16") F(T_TU32, "u32") F(T_TU64, "u64") \
    F(T_TF32,     "f32") F(T_TF64, "f64")

enum TType {
    #define F(t, s) t,
    TOKENS_MISC TOKENS_PUNCT TOKENS_KEYWORDS
    #undef F
};

inline const char *TName(TType t) {
    static const char *names[] = {
        #define F(t, s) s,
        TOKENS_MISC TOKENS_PUNCT TOKENS_KEYWORDS
        #undef F
    };
    return names[t];
}

inline bool IsIdentStart(char c) { return isalpha((uint8_t)c) || c == '_'; }
inline bool IsIdentCont(char c)  { return isalnum((uint8_t)c) || c == '_'; }

// The lexer holds exactly one current token. The parser copies the whole Lexer
// object where it needs to look ahead and backtrack (it is cheap to copy).
struct Lexer {
    const char *p = nullptr;         // Scan position in the 0-terminated source.
    const char *linestart = nullptr;
    int line = 1;
    string filename;

    // Current token.
    TType tok = T_EOF;
    string_view attr;                // Source text of ident/literal tokens.
    int64_t ival = 0;                // T_INTLIT value (also char literals).
    bool iuns = false;               // T_INTLIT above i64.max: a u64 constant.
    double fval = 0;                 // T_FLTLIT value.
    string sval;                     // T_STRLIT decoded value.
    int tokline = 1;
    const char *toklinestart = nullptr;

    Lexer(string_view _filename, const char *source) : p(source), filename(_filename) {
        linestart = p;
        Next();
    }

    [[noreturn]] void Error(const string &msg) { ErrorAt(msg, tokline, toklinestart); }

    [[noreturn]] void ErrorAt(const string &msg, int errline, const char *errlinestart) {
        auto s = cat(filename, ":", errline, ": error: ", msg);
        if (errlinestart) {
            auto end = errlinestart;
            while (*end && *end != '\n' && *end != '\r') end++;
            Append(s, "\n", string_view(errlinestart, (size_t)(end - errlinestart)), "\n");
            // Caret under the token start where we have it on this line.
            auto caretpos = errlinestart == toklinestart ? attr.data() : nullptr;
            if (caretpos && caretpos >= errlinestart && caretpos <= end) {
                for (auto q = errlinestart; q < caretpos; q++) s += *q == '\t' ? '\t' : ' ';
                s += '^';
            }
        }
        throw CompileError { s };
    }

    void Next() {
        again:
        for (;;) {
            auto c = *p;
            if (c == ' ' || c == '\t' || c == '\r') { p++; }
            else if (c == '\n') { p++; line++; linestart = p; }
            else break;
        }
        auto start = p;
        tokline = line;
        toklinestart = linestart;
        auto Set = [&](TType t) {
            tok = t;
            attr = string_view(start, (size_t)(p - start));
        };
        auto c = *p++;
        switch (c) {
            case 0:   p--; Set(T_EOF); return;

            case '(': Set(T_LPAREN);   return;
            case ')': Set(T_RPAREN);   return;
            case '[': Set(T_LBRACKET); return;
            case ']': Set(T_RBRACKET); return;
            case '{': Set(T_LCURLY);   return;
            case '}': Set(T_RCURLY);   return;
            case ',': Set(T_COMMA);    return;
            case ';': Set(T_SEMI);     return;
            case ':': Set(T_COLON);    return;
            case '?': Set(T_QUESTION); return;
            case '~': Set(T_BITNOT);   return;

            case '.':
                if (*p == '.') { p++; Set(T_DOTDOT); }
                else if (*p == '=') { p++; Set(T_DOTASSIGN); }
                else Set(T_DOT);
                return;
            case '+':
                if (*p == '+') { p++; Set(T_INC); }
                else if (*p == '=') { p++; Set(T_PLUSEQ); }
                else Set(T_PLUS);
                return;
            case '-':
                if (*p == '-') { p++; Set(T_DEC); }
                else if (*p == '=') { p++; Set(T_MINUSEQ); }
                else if (*p == '>') { p++; Set(T_ARROW); }
                else Set(T_MINUS);
                return;
            case '*':
                if (*p == '=') { p++; Set(T_MULEQ); } else Set(T_MUL);
                return;
            case '%':
                if (*p == '=') { p++; Set(T_MODEQ); } else Set(T_MOD);
                return;
            case '=':
                if (*p == '=') { p++; Set(T_EQ); }
                else if (*p == '>') { p++; Set(T_FATARROW); }
                else Set(T_ASSIGN);
                return;
            case '!':
                if (*p == '=') { p++; Set(T_NEQ); } else Set(T_NOT);
                return;
            case '<':
                if (*p == '=') { p++; Set(T_LTEQ); }
                else if (*p == '<') { p++; Set(T_SHL); }
                else Set(T_LT);
                return;
            case '>':
                if (*p == '=') { p++; Set(T_GTEQ); }
                else if (*p == '>') { p++; Set(T_SHR); }
                else Set(T_GT);
                return;
            case '&':
                if (*p == '&') { p++; Set(T_ANDAND); }
                else if (*p == '=') { p++; Set(T_ANDEQ); }
                else Set(T_BITAND);
                return;
            case '|':
                if (*p == '|') { p++; Set(T_OROR); }
                else if (*p == '=') { p++; Set(T_OREQ); }
                else Set(T_BITOR);
                return;
            case '^':
                if (*p == '=') { p++; Set(T_XOREQ); } else Set(T_XOR);
                return;

            case '/':
                if (*p == '/') {
                    while (*p && *p != '\n') p++;
                    goto again;
                }
                if (*p == '*') {
                    // Block comments nest, so commenting out code containing them works.
                    p++;
                    for (auto nesting = 1; nesting;) {
                        switch (*p++) {
                            case 0: p--; Error("unterminated /* comment");
                            case '\n': line++; linestart = p; break;
                            case '*': if (*p == '/') { p++; nesting--; } break;
                            case '/': if (*p == '*') { p++; nesting++; } break;
                        }
                    }
                    goto again;
                }
                if (*p == '=') { p++; Set(T_DIVEQ); } else Set(T_DIV);
                return;

            case '\'': {
                ival = LexEscapedChar('\'');
                iuns = false;
                if (*p != '\'') Error("expected closing \' of character literal");
                p++;
                Set(T_INTLIT);
                return;
            }
            case '"': {
                sval.clear();
                while (*p != '"') {
                    if (!*p || *p == '\n') { Set(T_STRLIT); Error("unterminated string literal"); }
                    sval += (char)LexEscapedChar('"');
                }
                p++;
                Set(T_STRLIT);
                return;
            }

            default: {
                if (isdigit((uint8_t)c)) {
                    if (c == '0' && (*p == 'x' || *p == 'X')) {
                        p++;
                        auto numstart = p;
                        while (isxdigit((uint8_t)*p)) p++;
                        if (p == numstart) Error("hex digits expected after 0x");
                        // C99-style hex floats: 0x1.8p3 (binary exponent required).
                        auto ishexfloat = false;
                        if (*p == '.' && isxdigit((uint8_t)p[1])) {
                            ishexfloat = true;
                            p++;
                            while (isxdigit((uint8_t)*p)) p++;
                        }
                        if (*p == 'p' || *p == 'P') {
                            ishexfloat = true;
                            p++;
                            if (*p == '+' || *p == '-') p++;
                            if (!isdigit((uint8_t)*p)) Error("exponent digits expected in hex float");
                            while (isdigit((uint8_t)*p)) p++;
                        } else if (ishexfloat) {
                            Error("hex float requires a p exponent");
                        }
                        if (ishexfloat) {
                            fval = strtod(string(start, p).c_str(), nullptr);
                            Set(T_FLTLIT);
                            return;
                        }
                        errno = 0;
                        ival = (int64_t)strtoull(string(numstart, p).c_str(), nullptr, 16);
                        if (errno) Error("hex literal too large for 64 bits");
                        iuns = ival < 0;
                        Set(T_INTLIT);
                        return;
                    }
                    while (isdigit((uint8_t)*p)) p++;
                    // A '.' only makes this a float if a digit follows; "1..2" must
                    // lex as int, "..", int.
                    auto isfloat = *p == '.' && isdigit((uint8_t)p[1]);
                    if (isfloat) {
                        p++;
                        while (isdigit((uint8_t)*p)) p++;
                    }
                    if ((*p == 'e' || *p == 'E') &&
                        (isdigit((uint8_t)p[1]) ||
                         ((p[1] == '+' || p[1] == '-') && isdigit((uint8_t)p[2])))) {
                        isfloat = true;
                        p += 2;
                        while (isdigit((uint8_t)*p)) p++;
                    }
                    auto text = string(start, p);
                    if (isfloat) {
                        fval = strtod(text.c_str(), nullptr);
                        Set(T_FLTLIT);
                    } else {
                        // Decimal literals cover the full u64 range; a value
                        // above i64.max is a u64 constant (§2).
                        errno = 0;
                        ival = (int64_t)strtoull(text.c_str(), nullptr, 10);
                        if (errno) Error(cat("integer literal too large for 64 bits: ", text));
                        iuns = ival < 0;
                        Set(T_INTLIT);
                    }
                    return;
                }
                if (IsIdentStart(c)) {
                    while (IsIdentCont(*p)) p++;
                    Set(T_IDENT);
                    static const unordered_map<string_view, TType> keywords = {
                        #define F(t, s) { s, t },
                        TOKENS_KEYWORDS
                        #undef F
                    };
                    auto it = keywords.find(attr);
                    if (it != keywords.end()) tok = it->second;
                    return;
                }
                Set(T_EOF);
                Error(cat("illegal character: \'", string_view(start, 1), "\' (", (int)(uint8_t)c, ")"));
            }
        }
    }

    // Reads one (possibly escaped) character of a char/string literal body.
    int LexEscapedChar(char quote) {
        auto c = *p++;
        if (c == '\\') {
            auto e = *p++;
            switch (e) {
                case 'n':  return '\n';
                case 't':  return '\t';
                case 'r':  return '\r';
                case '0':  return 0;
                case '\\': return '\\';
                case '"':  return '"';
                case '\'': return '\'';
                case 'x': {
                    if (!isxdigit((uint8_t)p[0]) || !isxdigit((uint8_t)p[1]))
                        Error("\\x escape requires two hex digits");
                    auto hex = [](char h) { return h <= '9' ? h - '0' : (h | 0x20) - 'a' + 10; };
                    auto v = hex(*p) * 16 + hex(p[1]);
                    p += 2;
                    return v;
                }
                default:   p -= 2; Error(cat("unknown escape sequence: \\", string_view(p + 1, 1)));
            }
        }
        if (!c || c == '\n') { p--; Error(cat("unterminated ", quote == '"' ? "string" : "character", " literal")); }
        return (uint8_t)c;
    }
};

}  // namespace goose
