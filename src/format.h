// Goose compiler — `goose format`: rewrites a Goose file in the standard
// layout. The author's line breaks stay; indentation, the spaces between
// tokens, blank lines, trailing whitespace and the final newline are
// normalized, a list whose closing bracket starts a line gets a trailing
// comma, and a run of declarations' types and `=`s line up as gofmt's do. The formatter prints the file's own tokens and comments, the text
// between them chosen afresh, with the parser as the oracle for what a token
// is (Parser::roles: a generic `<` or a comparison, a unary `-` or a binary
// one, a bracket whose list may end in a comma). Every result is checked
// before it is used: the same tokens and comments, a comma added only
// before a closing bracket, and a parse to the same dump and documentation.
// Format is pure, so the language server can call it as well.
#pragma once

namespace goose {

// In main.cpp: stdin and stdout as byte streams.
void SetBinaryStdio();

struct FormatResult {
    bool ok = false;
    string text;                 // The formatted file, where ok.
    vector<Diagnostic> diags;    // Why the file does not parse.
    string internal;             // Why the result failed its check: a formatter bug.
};

namespace format {

// The column the text [b, e) ends at, starting at `col`: tabs stop every four
// columns and a UTF-8 character is one column.
inline int Width(const char *b, const char *e, int col = 0) {
    for (auto p = b; p < e; p++) {
        auto c = (uint8_t)*p;
        if (c == '\t') col = (col / 4 + 1) * 4;
        else if ((c & 0xC0) != 0x80) col++;
    }
    return col;
}
inline int Width(string_view s, int col = 0) { return Width(s.data(), s.data() + s.size(), col); }

// The column after `s`, which starts at `col` and may span lines.
inline int EndCol(string_view s, int col) {
    auto nl = s.rfind('\n');
    return nl == string_view::npos ? Width(s, col) : Width(s.substr(nl + 1));
}

// A token or a comment of the input, in order.
struct Item {
    enum Kind { TOKEN, LINE_COMMENT, BLOCK_COMMENT } kind = TOKEN;
    TType tok = T_EOF;
    string_view text;
    uint8_t roles = 0;           // TokenRole bits.
    int closes = 0;              // The generic lists a `>` or `>>` closes.
    int nl = 0;                  // Line breaks between it and the item before.
    bool space = false;          // Whitespace before it on its line.
    int oldcol = 0;              // Its column in the input.
    int oldcodeend = 0;          // A line comment's: the column after what precedes it.
    int match = -1;              // An opening bracket's closing one, and back.
    bool comma = false;          // A trailing comma goes after it.
};

inline bool IsTok(const Item &it, TType t) { return it.kind == Item::TOKEN && it.tok == t; }

inline bool IsOpener(const Item &it) {
    if (it.kind != Item::TOKEN) return false;
    return it.tok == T_LPAREN || it.tok == T_LBRACKET || it.tok == T_LCURLY ||
           (it.roles & R_ANGLE_OPEN);
}

// How many brackets the item closes.
inline int Closes(const Item &it) {
    if (it.kind != Item::TOKEN) return 0;
    if (it.tok == T_RPAREN || it.tok == T_RBRACKET || it.tok == T_RCURLY) return 1;
    return it.closes;
}

// A `{` that opens a block, not a list.
inline bool IsBlockCurly(const Item &it) { return IsTok(it, T_LCURLY) && !(it.roles & R_LIST); }

// What may end an operand: a name, a literal, a closing bracket, a type's
// suffix. A `(`, `[` or `<` after one belongs to it.
inline bool OperandEnd(const Item &t) {
    switch (t.tok) {
        case T_IDENT: case T_INTLIT: case T_FLTLIT: case T_STRLIT:
        case T_TRUE: case T_FALSE: case T_NULLLIT: case T_SELF:
        case T_RPAREN: case T_RBRACKET: case T_RCURLY:
        case T_INC: case T_DEC:
            return true;
        default:
            return IsPrimTypeToken(t.tok) || (t.roles & (R_ANGLE_CLOSE | R_TYPE_POSTFIX));
    }
}

// The spaces between two tokens on one line: 0 or 1. The first rule that
// applies decides.
inline int Space(const Item &a, const Item &b) {
    auto rangea = a.tok == T_DOTDOT && !(a.roles & (R_TYPE_POSTFIX | R_GLUE));
    auto rangeb = b.tok == T_DOTDOT && !(b.roles & (R_TYPE_POSTFIX | R_GLUE));
    switch (b.tok) {
        case T_COMMA: case T_SEMI: case T_RPAREN: case T_RBRACKET:
        case T_INC: case T_DEC: case T_QUESTION:
            return 0;
        default: break;
    }
    if (b.roles & (R_TYPE_POSTFIX | R_ANGLE_CLOSE | R_GLUE_LEFT | R_GLUE)) return 0;
    if (a.roles & (R_GLUE | R_ANGLE_OPEN | R_UNARY)) return 0;
    switch (a.tok) {
        case T_LPAREN: case T_LBRACKET: case T_DOT: case T_COLONCOLON: return 0;
        default: break;
    }
    if (b.tok == T_LCURLY) return 1;
    if (a.tok == T_LCURLY) return b.tok == T_RCURLY ? 0 : 1;
    if (b.tok == T_RCURLY) return 1;
    if (b.tok == T_DOT) return OperandEnd(a) ? 0 : 1;
    if (b.tok == T_COLONCOLON) return a.tok == T_IDENT ? 0 : 1;
    if (rangeb) return OperandEnd(a) ? 0 : 1;
    if (rangea) return 0;
    if (b.tok == T_COLON) return 0;
    if (b.tok == T_LPAREN) return OperandEnd(a) || a.tok == T_FN ? 0 : 1;
    if (b.tok == T_LBRACKET) return OperandEnd(a) || a.tok == T_REUSABLE ? 0 : 1;
    if (b.roles & R_ANGLE_OPEN) return OperandEnd(a) ? 0 : 1;
    return 1;
}

// Whether two tokens written with nothing between them would lex as
// something else: `- -x` as `--x`.
inline bool Glues(string_view a, string_view b) {
    auto s = cat(a, b);
    try {
        Lexer lx("", s.c_str());
        if (lx.attr.data() != s.c_str() || lx.attr.size() != a.size()) return true;
        lx.Next();
        if (lx.attr.data() != s.c_str() + a.size() || lx.attr.size() != b.size()) return true;
        lx.Next();
        return lx.tok != T_EOF;
    } catch (CompileError &) {
        return true;
    }
}

// The tokens and comments of `src`, with the roles the parse gave them. A
// comment's text is its own, less trailing whitespace on a line comment.
inline vector<Item> Collect(const string &src, const vector<pair<const char *, uint8_t>> &roles) {
    unordered_map<const char *, pair<uint8_t, int>> rolemap;
    for (auto &[at, r] : roles) {
        auto &e = rolemap[at];
        e.first |= r;
        if (r == R_ANGLE_CLOSE) e.second++;
    }
    vector<Item> items;
    auto linestart = src.c_str();
    // The column of `at` on its line, counted on from the last one asked for
    // rather than from the line's start, since one line can hold a whole table.
    auto colfrom = linestart;
    auto colat = 0;
    auto column = [&](const char *at) {
        if (colfrom < linestart || colfrom > at) colfrom = linestart, colat = 0;
        colat = Width(colfrom, at, colat);
        colfrom = at;
        return colat;
    };
    auto nl = 0;
    auto space = false;
    auto add = [&](Item it, const char *at) {
        it.nl = nl;
        it.space = space;
        it.oldcol = column(at);
        for (auto p = it.text.data(); p < it.text.data() + it.text.size(); p++)
            if (*p == '\n') linestart = p + 1;
        items.push_back(it);
        nl = 0;
        space = false;
    };
    // The whitespace and comments between two tokens.
    auto gap = [&](const char *b, const char *e) {
        for (auto p = b; p < e;) {
            auto c = *p;
            if (c == '\n') {
                nl++;
                space = false;
                linestart = ++p;
            } else if (c == ' ' || c == '\t' || c == '\r') {
                space = true;
                p++;
            } else if (c == '/' && p[1] == '/') {
                auto q = p;
                while (q < e && *q != '\n') q++;
                auto end = q;
                while (end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
                Item it;
                it.kind = Item::LINE_COMMENT;
                it.text = string_view(p, (size_t)(end - p));
                auto code = p;
                while (code > linestart && (code[-1] == ' ' || code[-1] == '\t' || code[-1] == '\r'))
                    code--;
                it.oldcodeend = column(code);
                add(it, p);
                p = q;
            } else if (c == '/' && p[1] == '*') {
                auto q = p + 2;
                for (auto nesting = 1; nesting && q < e;) {
                    if (q[0] == '*' && q[1] == '/') { nesting--; q += 2; }
                    else if (q[0] == '/' && q[1] == '*') { nesting++; q += 2; }
                    else q++;
                }
                Item it;
                it.kind = Item::BLOCK_COMMENT;
                it.text = string_view(p, (size_t)(q - p));
                add(it, p);
                p = q;
            } else {
                throw CompileError { cat("unexpected text between tokens: ", QuotedCharAt(p)) };
            }
        }
    };
    Lexer lex("", src.c_str());
    for (;;) {
        gap(lex.prevend, lex.attr.data());
        if (lex.tok == T_EOF) break;
        Item it;
        it.tok = lex.tok;
        it.text = lex.attr;
        auto r = rolemap.find(lex.attr.data());
        if (r != rolemap.end()) {
            it.roles = r->second.first;
            it.closes = r->second.second;
        }
        add(it, lex.attr.data());
        lex.Next();
    }
    // Brackets, matched.
    vector<int> open;
    auto pairs = [](const Item &o, const Item &c) {
        if (o.roles & R_ANGLE_OPEN) return (c.roles & R_ANGLE_CLOSE) != 0;
        return (o.tok == T_LPAREN && c.tok == T_RPAREN) || (o.tok == T_LBRACKET && c.tok == T_RBRACKET) ||
               (o.tok == T_LCURLY && c.tok == T_RCURLY);
    };
    for (int i = 0; i < (int)items.size(); i++) {
        auto &it = items[i];
        for (int n = Closes(it); n > 0; n--) {
            if (open.empty() || !pairs(items[open.back()], it))
                throw CompileError { cat("unmatched ", it.text) };
            items[open.back()].match = i;
            if (it.match < 0) it.match = open.back();
            open.pop_back();
        }
        if (IsOpener(it)) open.push_back(i);
    }
    if (!open.empty()) throw CompileError { cat("unclosed ", items[open.back()].text) };
    // Trailing commas: after the last element of a list whose closing
    // bracket starts a line.
    for (int o = 0; o < (int)items.size(); o++) {
        if (!(items[o].roles & R_LIST) || !IsOpener(items[o])) continue;
        auto c = items[o].match;
        if (items[c].nl == 0) continue;
        auto t = c - 1;
        while (t > o && items[t].kind != Item::TOKEN) t--;
        if (t == o || IsTok(items[t], T_COMMA)) continue;
        // An initializer list's trailing `..` (§4.2) takes no comma after it.
        if (IsTok(items[t], T_DOTDOT) && !(items[t].roles & R_TYPE_POSTFIX) &&
            IsTok(items[o], T_LCURLY))
            continue;
        items[t].comma = true;
    }
    return items;
}

// The lines after the first of a multi-line raw string or block comment,
// moved `delta` columns: spaces added in front of each line that is not
// empty, or taken from the front of each. `applied` is the shift made: 0
// where the text cannot move by `delta` (a line that does not start with
// that many spaces). A comment's lines lose their trailing whitespace; a
// string's text is never touched, and its value must come out the same.
inline string Shift(string_view text, int delta, bool comment, int &applied) {
    vector<string> lines;
    for (size_t pos = 0;;) {
        auto e = text.find('\n', pos);
        lines.emplace_back(text.substr(pos, e == string_view::npos ? string_view::npos : e - pos));
        if (e == string_view::npos) break;
        pos = e + 1;
    }
    if (comment)
        for (auto &l : lines) {
            auto e = l.find_last_not_of(" \t\r");
            l.resize(e == string::npos ? 0 : e + 1);
        }
    auto join = [](const vector<string> &ls) {
        string s;
        for (size_t i = 0; i < ls.size(); i++) Append(s, i ? "\n" : "", ls[i]);
        return s;
    };
    auto verbatim = join(lines);
    applied = 0;
    if (!delta) return verbatim;
    auto moved = lines;
    for (size_t i = 1; i < moved.size(); i++) {
        auto &l = moved[i];
        if (l.empty()) continue;
        if (delta > 0) {
            l.insert(0, (size_t)delta, ' ');
            continue;
        }
        auto n = (size_t)-delta;
        auto lead = l.find_first_not_of(' ');
        if (lead == string::npos) lead = l.size();
        if (lead >= n) l.erase(0, n);
        else if (l.find_first_not_of(" \t") == string::npos) l.erase(0, min(n, l.size()));
        else return verbatim;
    }
    auto s = join(moved);
    if (!comment) {
        // The value is the same, or the text stays as it was.
        try {
            auto before = string(text), after = s;
            Lexer a("", before.c_str()), b("", after.c_str());
            if (a.tok != T_STRLIT || b.tok != T_STRLIT || a.sval != b.sval ||
                a.smultiline != b.smultiline || b.attr.size() != after.size())
                return verbatim;
        } catch (CompileError &) {
            return verbatim;
        }
    }
    applied = delta;
    return s;
}

// A line of the output, before its trailing comment is placed.
struct OutLine {
    int blank = 0;              // Blank lines before it.
    int orignl = 0;             // Line breaks before it in the input.
    string text;                // Indentation and code; a multi-line token's lines included.
    string_view comment;        // A trailing line comment, if any.
    int commentold = 0;         // That comment's column in the input,
    int codeendold = 0;         // and the column after the code before it there.
    int delta = 0;              // How far the end of the line moved.
    int codeend = 0;            // The column after the code.
    int depth = 0;              // Brackets open at its start.
    // A table's cells: the type and the `=` of a line that declares a name
    // with its type, `[const|let|...] name: type [= value]`, as items, and
    // where they start; -1 where the line has none. Lines with the same key
    // in a run line them up.
    int cell[2] = { -1, -1 };
    int cellcol[2] = { 0, 0 };
    string cellkey;
};

// The keywords that may come before the name a table's line declares.
inline bool DeclKeyword(const Item &it) {
    if (it.kind != Item::TOKEN) return false;
    switch (it.tok) {
        case T_EXPORT: case T_LET: case T_VAR: case T_CONST: case T_EXTERN: case T_PAD:
            return true;
        default:
            return false;
    }
}

// The lines of the output, with `pad` spaces more before the items it
// names: the cells of a table, lined up.
inline vector<OutLine> Lines(const vector<Item> &items, const unordered_map<int, int> &pad) {
    // An open bracket: where its line is indented, where it is, and, for the
    // lines inside it, the first line of the statement or element they are
    // in (its anchor), which a continuation line keeps its offset from.
    struct Open {
        int item;
        bool block, hanging;
        int lineindent, oldcol, newcol;
        bool anchored = false;
        int aold = 0, anew = 0;
    };
    vector<Open> stack;
    Open top { -1, true, true, 0, 0, 0 };   // The file's level.
    auto cur = [&]() -> Open & { return stack.empty() ? top : stack.back(); };
    auto havecode = false;     // A line with code came before.
    auto ended = true;         // That line ended a statement or an element.
    auto prevhang = false;     // The line before ended in a hanging bracket.
    vector<OutLine> out;
    for (size_t i = 0; i < items.size();) {
        auto j = i + 1;
        while (j < items.size() && items[j].nl == 0) j++;
        auto &first = items[i];
        OutLine L;
        L.orignl = first.nl;
        L.depth = (int)stack.size();
        auto closerfirst = Closes(first) > 0;
        L.blank = out.empty() || closerfirst || prevhang ? 0 : min(max(first.nl - 1, 0), 1);
        auto lasttoken = j;   // The line's last token, where it has one.
        for (auto k = i; k < j; k++)
            if (items[k].kind == Item::TOKEN) lasttoken = k;
        auto hascode = lasttoken < j;
        auto oldindent = first.oldcol;
        auto indent = 0;
        // An `else` continues the statement its `if` began, wherever the
        // line before ended.
        auto iselse = IsTok(first, T_ELSE);
        auto cont = havecode && (!ended || iselse);
        // A line that keeps its offset from the bracket it is aligned in
        // does so while it stays to the right of that line's indentation;
        // else it is indented a level past it.
        auto keep = [](int col, int base) { return col > base ? col : base + 4; };
        if (closerfirst) {
            indent = oldindent;
            for (auto s = stack.rbegin(); s != stack.rend(); s++)
                if (s->item == first.match) { indent = s->lineindent; break; }
        } else {
            auto &c = cur();
            if (cont && c.anchored && (IsBlockCurly(first) || iselse)) {
                // A body's `{`, or an `else`, on a line of its own stays
                // where it was relative to its statement.
                indent = max(0, oldindent + c.anew - c.aold);
            } else if (cont && c.anchored) {
                // A line that continues a statement keeps its offset from the
                // statement's first line, but goes no further left than it:
                // `a &&` over `b` stays as it is, as does `b` indented past it.
                indent = max(oldindent + c.anew - c.aold, c.anew);
            } else if (!stack.empty()) {
                indent = c.hanging ? c.lineindent + 4
                                   : keep(oldindent + c.newcol - c.oldcol, c.lineindent);
            }
            if (!cont && hascode) {
                c.anchored = true;
                c.aold = oldindent;
                c.anew = indent;
            }
        }
        // The cells, where the line starts a statement or an element that
        // declares a name with its type.
        auto prefix = i;
        while (prefix < j && DeclKeyword(items[prefix])) prefix++;
        if (!cont && hascode && prefix + 2 < j && IsTok(items[prefix], T_IDENT) &&
            IsTok(items[prefix + 1], T_COLON) && items[prefix + 2].kind == Item::TOKEN) {
            L.cell[0] = (int)prefix + 2;
            auto d = 0;
            for (auto q = prefix + 2; q < j && items[q].kind == Item::TOKEN; q++) {
                d -= Closes(items[q]);
                if (d < 0 || IsTok(items[q], T_SEMI)) break;
                if (d == 0 && IsTok(items[q], T_ASSIGN)) {
                    L.cell[1] = (int)q;
                    break;
                }
                if (IsOpener(items[q])) d++;
            }
            L.cellkey = cat(L.depth, " ", indent);
            for (auto q = i; q < prefix; q++) Append(L.cellkey, " ", items[q].text);
        }
        L.text.assign((size_t)indent, ' ');
        auto col = indent;
        auto lowest = stack.size();
        auto delta = indent - oldindent;
        const Item *prev = nullptr;
        // A bracket opened on this line after one opened on an earlier line
        // closed (the `{` after a parameter list that wrapped) is indented
        // from where that one's line was.
        // A body's `{` on a line that continues its statement (a condition
        // that wrapped) is indented from where the statement starts, as a
        // trailing block's is not.
        auto depth0 = stack.size();
        auto openindent = indent;
        auto bodyindent = !closerfirst && cont && cur().anchored ? cur().anew : indent;
        for (auto k = i; k < j; k++) {
            auto &it = items[k];
            if (it.kind == Item::LINE_COMMENT && k != i) {
                L.comment = it.text;
                L.commentold = it.oldcol;
                L.codeendold = it.oldcodeend;
                break;
            }
            if (prev) {
                auto sp = it.kind == Item::TOKEN && prev->kind == Item::TOKEN ? Space(*prev, it)
                                                                              : (int)it.space;
                if (!sp && it.kind == Item::TOKEN && prev->kind == Item::TOKEN &&
                    Glues(prev->comma ? string_view(",") : prev->text, it.text))
                    sp = 1;
                // A statement after a `;` on its line, set off by more than a
                // space, keeps its column, moved with its line: a table of
                // declarations stays a table.
                if (it.kind == Item::TOKEN && IsTok(*prev, T_SEMI) && it.oldcol > prev->oldcol + 2)
                    sp = max(sp, it.oldcol + delta - col);
                if (auto p = pad.find((int)k); p != pad.end()) sp += p->second;
                L.text.append((size_t)sp, ' ');
                col += sp;
            }
            auto startcol = col;
            for (auto c = 0; c < 2; c++)
                if (L.cell[c] == (int)k) L.cellcol[c] = col;
            if (it.text.find('\n') != string_view::npos) {
                int applied;
                auto s = Shift(it.text, delta, it.kind != Item::TOKEN, applied);
                L.text += s;
                col = EndCol(s, col);
                delta = applied;
            } else {
                L.text += it.text;
                col = Width(it.text, col);
            }
            if (it.comma) {
                L.text += ',';
                col++;
            }
            if (it.kind == Item::TOKEN) {
                for (auto n = Closes(it); n > 0 && !stack.empty(); n--) {
                    if (stack.size() <= depth0) {
                        openindent = stack.back().lineindent;
                        depth0 = stack.size() - 1;
                    }
                    stack.pop_back();
                }
                lowest = min(lowest, stack.size());
                if (IsOpener(it)) {
                    auto last = k == lasttoken;
                    auto block = IsBlockCurly(it);
                    auto lineindent = block && !(it.roles & R_FUNVAL) ? min(openindent, bodyindent)
                                                                     : openindent;
                    stack.push_back(Open { (int)k, block, block || last, lineindent, it.oldcol, startcol });
                }
            }
            prev = &it;
        }
        L.delta = delta;
        L.codeend = col;
        // A table's line is whole: no bracket it closes or leaves open, and
        // no line break inside it.
        if (lowest < (size_t)L.depth || stack.size() != (size_t)L.depth ||
            L.text.find('\n') != string::npos)
            L.cell[0] = L.cell[1] = -1;
        // How the line ends, for the next.
        prevhang = false;
        if (hascode) {
            auto k = lasttoken + 1;
            auto &last = items[k - 1];
            auto inblock = cur().block;
            auto comma = last.comma || IsTok(last, T_COMMA);
            havecode = true;
            ended = IsTok(last, T_SEMI) || IsTok(last, T_RCURLY) || IsOpener(last) ||
                    (comma && !inblock) || (IsTok(last, T_FATARROW) && inblock && !stack.empty());
            prevhang = IsOpener(last) && !stack.empty() && stack.back().item == (int)(k - 1) &&
                       stack.back().hanging;
        }
        out.push_back(std::move(L));
        i = j;
    }
    return out;
}

// The formatted text of `src`, whose tokens carry `roles`.
inline string Layout(const string &src, const vector<pair<const char *, uint8_t>> &roles) {
    auto items = Collect(src, roles);
    auto out = Lines(items, {});
    // Tables, as gofmt lines them up: a run of consecutive lines that declare
    // names with their types, at one depth and indentation and after the same
    // keywords, puts each type a space past the longest name, and each `=`
    // in a run of them a space past the longest type.
    unordered_map<int, int> pad;
    for (size_t a = 0; a < out.size();) {
        if (out[a].cell[0] < 0) { a++; continue; }
        auto b = a + 1;
        while (b < out.size() && out[b].cell[0] >= 0 && out[b].orignl == 1 &&
               out[b].cellkey == out[a].cellkey)
            b++;
        if (b - a >= 2) {
            auto at = 0;
            for (auto k = a; k < b; k++) at = max(at, out[k].cellcol[0]);
            for (auto k = a; k < b; k++)
                if (at > out[k].cellcol[0]) pad[out[k].cell[0]] = at - out[k].cellcol[0];
            auto moved = [&](size_t k) {
                auto p = pad.find(out[k].cell[0]);
                return out[k].cellcol[1] + (p == pad.end() ? 0 : p->second);
            };
            for (auto c = a; c < b;) {
                if (out[c].cell[1] < 0) { c++; continue; }
                auto e = c + 1;
                while (e < b && out[e].cell[1] >= 0) e++;
                if (e - c >= 2) {
                    auto eq = 0;
                    for (auto k = c; k < e; k++) eq = max(eq, moved(k));
                    for (auto k = c; k < e; k++)
                        if (eq > moved(k)) pad[out[k].cell[1]] = eq - moved(k);
                }
                c = e;
            }
        }
        a = b;
    }
    if (!pad.empty()) out = Lines(items, pad);
    // Trailing comments: a run of them on consecutive lines at one depth,
    // which shared a column, shares one still, as far right as its longest
    // line needs; any other keeps its column, moved with its line, or one
    // space after the code where it had that.
    vector<int> commentcol(out.size(), 0);
    for (size_t a = 0; a < out.size();) {
        if (out[a].comment.empty()) { a++; continue; }
        auto b = a + 1;
        while (b < out.size() && !out[b].comment.empty() && out[b].orignl == 1 &&
               out[b].depth == out[a].depth && out[b].commentold == out[a].commentold)
            b++;
        if (b - a >= 2) {
            auto c = 0;
            for (auto k = a; k < b; k++)
                c = max({ c, out[k].commentold + out[k].delta, out[k].codeend + 1 });
            for (auto k = a; k < b; k++) commentcol[k] = c;
        } else {
            auto &l = out[a];
            commentcol[a] = l.commentold == l.codeendold + 1
                                ? l.codeend + 1
                                : max(l.commentold + l.delta, l.codeend + 1);
        }
        a = b;
    }
    string s;
    for (size_t k = 0; k < out.size(); k++) {
        auto &l = out[k];
        s.append((size_t)l.blank, '\n');
        s += l.text;
        if (!l.comment.empty()) {
            s.append((size_t)max(commentcol[k] - l.codeend, 1), ' ');
            s += l.comment;
        }
        s += '\n';
    }
    return s;
}

// A token as the check compares it: a string by its value.
struct CheckTok {
    TType tok;
    string text;
    bool multiline = false;
    int line = 0;
};

// The tokens of `src`, and its comments with each of their lines trimmed.
inline void LexForCheck(const string &src, vector<CheckTok> &toks, vector<string> &comments) {
    Lexer lex("", src.c_str());
    auto scan = [&](const char *b, const char *e) {
        for (auto p = b; p < e;) {
            if (p[0] == '/' && (p[1] == '/' || p[1] == '*')) {
                auto q = p + 2;
                if (p[1] == '/') {
                    while (q < e && *q != '\n') q++;
                } else {
                    for (auto nesting = 1; nesting && q < e;) {
                        if (q[0] == '*' && q[1] == '/') { nesting--; q += 2; }
                        else if (q[0] == '/' && q[1] == '*') { nesting++; q += 2; }
                        else q++;
                    }
                }
                string c;
                for (auto l = p; l < q;) {
                    auto le = l;
                    while (le < q && *le != '\n') le++;
                    auto lb = l, lend = le;
                    while (lb < lend && (*lb == ' ' || *lb == '\t' || *lb == '\r')) lb++;
                    while (lend > lb && (lend[-1] == ' ' || lend[-1] == '\t' || lend[-1] == '\r')) lend--;
                    Append(c, c.empty() && l == p ? "" : "\n", string_view(lb, (size_t)(lend - lb)));
                    l = le < q ? le + 1 : le;
                }
                comments.push_back(c);
                p = q;
            } else {
                p++;
            }
        }
    };
    for (;;) {
        scan(lex.prevend, lex.attr.data());
        if (lex.tok == T_EOF) break;
        CheckTok t { lex.tok, lex.tok == T_STRLIT ? lex.sval : string(lex.attr),
                     lex.tok == T_STRLIT && lex.smultiline, lex.tokline };
        toks.push_back(t);
        lex.Next();
    }
}

// The dump and documentation of a single file's parse, or why it failed.
inline bool ParseForCheck(const string &filename, const string &text, string &dump, string &docs,
                          string &why) {
    Ast ast;
    ast.sink = [](const Diagnostic &) {};
    ast.sources.emplace_back(filename, make_unique<string>(text));
    try {
        Parser p(ast, ast.sources[0].first, ast.sources[0].second->c_str(), 0);
        p.ParseTop();
    } catch (CompileError &e) {
        why = e.msg;
        return false;
    }
    if (!ast.errors.empty()) {
        why = ast.errors[0].text;
        return false;
    }
    ast.Dump(dump);
    string raw;
    DumpDocs(ast, raw);
    // A comment line's trailing whitespace, which the formatter removes.
    for (size_t pos = 0; pos < raw.size();) {
        auto e = raw.find('\n', pos);
        if (e == string::npos) e = raw.size();
        auto l = string_view(raw).substr(pos, e - pos);
        auto t = l.find_last_not_of(" \t\r");
        Append(docs, l.substr(0, t == string_view::npos ? 0 : t + 1), "\n");
        pos = e + 1;
    }
    return true;
}

// Why `out` is not `in` formatted, or "" where it is: the tokens differ
// other than by a comma before a closing bracket, the comments differ
// other than in whitespace, or the parse of `out` differs.
inline string CheckSame(const string &in, const string &out, const string &filename) {
    vector<CheckTok> a, b;
    vector<string> ca, cb;
    try {
        LexForCheck(in, a, ca);
        LexForCheck(out, b, cb);
    } catch (CompileError &e) {
        return cat("the formatted text does not lex: ", e.msg);
    }
    size_t i = 0, j = 0;
    auto closer = [](TType t) { return t == T_RPAREN || t == T_RBRACKET || t == T_RCURLY; };
    while (i < a.size() || j < b.size()) {
        if (i < a.size() && j < b.size() && a[i].tok == b[j].tok && a[i].text == b[j].text &&
            a[i].multiline == b[j].multiline) {
            i++, j++;
            continue;
        }
        if (j + 1 < b.size() && b[j].tok == T_COMMA && closer(b[j + 1].tok) && i < a.size() &&
            a[i].tok == b[j + 1].tok && (i == 0 || a[i - 1].tok != T_COMMA)) {
            j++;
            continue;
        }
        auto line = j < b.size() ? b[j].line : b.empty() ? 1 : b.back().line;
        return cat("the formatted text's tokens differ from the input's at its line ", line);
    }
    if (ca != cb) return "the formatted text's comments differ from the input's";
    string dumpa, docsa, dumpb, docsb, why;
    if (!ParseForCheck(filename, in, dumpa, docsa, why))
        return cat("the input does not parse again: ", why);
    if (!ParseForCheck(filename, out, dumpb, docsb, why))
        return cat("the formatted text does not parse: ", why);
    if (dumpa != dumpb) return "the formatted text parses to a different program";
    if (docsa != docsb) return "the formatted text's documentation comments differ";
    return "";
}

}  // namespace format

// `src` formatted. A file that does not parse is not formatted: its
// diagnostics say why. `verify_idempotent` checks as well that formatting
// the result again changes nothing.
inline FormatResult Format(string_view input, const string &filename, bool verify_idempotent = false) {
    FormatResult r;
    string src(input);
    StripBom(src);
    {
        // Line breaks are \n; a \r of a \r\n is whitespace to the lexer anyway.
        string t;
        t.reserve(src.size());
        for (size_t i = 0; i < src.size(); i++)
            if (!(src[i] == '\r' && i + 1 < src.size() && src[i + 1] == '\n')) t += src[i];
        src = std::move(t);
    }
    Ast ast;
    ast.sink = [](const Diagnostic &) {};
    ast.sources.emplace_back(filename, make_unique<string>(src));
    auto &text = *ast.sources[0].second;
    vector<pair<const char *, uint8_t>> roles;
    try {
        Parser p(ast, ast.sources[0].first, text.c_str(), 0);
        p.roles = &roles;
        p.ParseTop();
    } catch (CompileError &e) {
        if (e.diags.empty()) {
            Diagnostic d;
            d.message = d.text = e.msg;
            r.diags.push_back(d);
        }
        for (auto &d : e.diags) r.diags.push_back(d);
        return r;
    }
    if (!ast.errors.empty()) {
        r.diags = ast.errors;
        return r;
    }
    try {
        r.text = format::Layout(text, roles);
    } catch (CompileError &e) {
        r.internal = e.msg;
        return r;
    }
    r.internal = format::CheckSame(text, r.text, filename);
    if (!r.internal.empty()) return r;
    if (verify_idempotent) {
        auto again = Format(r.text, filename);
        if (!again.ok) {
            r.internal = cat("formatting the result again fails: ",
                             again.internal.empty() ? "it does not parse" : again.internal);
            return r;
        }
        if (again.text != r.text) {
            size_t line = 1;
            for (size_t k = 0; k < r.text.size() && k < again.text.size() && r.text[k] == again.text[k]; k++)
                if (r.text[k] == '\n') line++;
            r.internal = cat("formatting the result again changes its line ", line);
            return r;
        }
    }
    r.ok = true;
    return r;
}

// A change that turns one text into another: bytes [start, end) of the old
// text replaced by `text`.
struct TextChange {
    size_t start, end;
    string text;
};

// The changes that turn `from` into `to`, line by line, as few as a diff of
// their lines finds (Myers' algorithm), each a run of changed lines: what
// an editor applies to keep its cursor and marks on the lines that stay.
// A line ending in \r\n is the same line ending in \n. Where the texts
// differ in more lines than `limit`, one change covers everything between
// the lines they begin and end with in common.
inline vector<TextChange> LineChanges(string_view from, string_view to, int limit = 2000) {
    auto split = [](string_view t, vector<string_view> &lines, vector<size_t> &starts) {
        for (size_t pos = 0; pos < t.size();) {
            auto e = t.find('\n', pos);
            e = e == string_view::npos ? t.size() : e + 1;
            starts.push_back(pos);
            lines.push_back(t.substr(pos, e - pos));
            pos = e;
        }
        starts.push_back(t.size());
    };
    vector<string_view> a, b;
    vector<size_t> as, bs;
    split(from, a, as);
    split(to, b, bs);
    auto content = [](string_view l) {
        if (!l.empty() && l.back() == '\n') {
            l.remove_suffix(1);
            if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
            return pair<string_view, bool>(l, true);
        }
        return pair<string_view, bool>(l, false);
    };
    auto eq = [&](int x, int y) { return content(a[(size_t)x]) == content(b[(size_t)y]); };
    int n = (int)a.size(), m = (int)b.size();
    // The furthest x reached on each diagonal k = x - y, kept for every d
    // as the slice [-d, d], to walk back from the end.
    vector<vector<int>> trace;
    vector<int> v(2 * (size_t)(n + m) + 3, 0);
    auto off = n + m + 1;
    auto found = -1;
    for (int d = 0; d <= n + m && d <= limit && found < 0; d++) {
        trace.emplace_back(v.begin() + (off - d), v.begin() + (off + d + 1));
        for (int k = -d; k <= d; k += 2) {
            auto x = k == -d || (k != d && v[(size_t)(off + k - 1)] < v[(size_t)(off + k + 1)])
                         ? v[(size_t)(off + k + 1)] : v[(size_t)(off + k - 1)] + 1;
            auto y = x - k;
            while (x < n && y < m && eq(x, y)) x++, y++;
            v[(size_t)(off + k)] = x;
            if (x >= n && y >= m) { found = d; break; }
        }
    }
    // Runs of changed lines: old lines [a0, a1) become new lines [b0, b1).
    struct Run { int a0, a1, b0, b1; };
    vector<Run> runs;
    if (found < 0) {
        int p = 0, q = 0;
        while (p < n && p < m && eq(p, p)) p++;
        while (q < n - p && q < m - p && eq(n - 1 - q, m - 1 - q)) q++;
        runs.push_back(Run { p, n - q, p, m - q });
    } else {
        // Walk back, each step one inserted or deleted line after a run of
        // equal ones.
        vector<pair<int, int>> steps;   // (x, y) before each edit, last first, and its kind
        vector<bool> inserts;
        int x = n, y = m;
        for (int d = found; d > 0; d--) {
            auto &tv = trace[(size_t)d];
            auto at = [&](int k) { return tv[(size_t)(k + d)]; };
            auto k = x - y;
            auto prevk = k == -d || (k != d && at(k - 1) < at(k + 1)) ? k + 1 : k - 1;
            auto prevx = at(prevk), prevy = prevx - prevk;
            while (x > prevx && y > prevy) x--, y--;
            steps.push_back({ prevx, prevy });
            inserts.push_back(x == prevx);
            x = prevx, y = prevy;
        }
        for (auto i = steps.size(); i-- > 0;) {
            auto [px, py] = steps[i];
            auto r = Run { px, px + !inserts[i], py, py + (int)inserts[i] };
            if (!runs.empty() && runs.back().a1 == r.a0 && runs.back().b1 == r.b0) {
                runs.back().a1 = r.a1;
                runs.back().b1 = r.b1;
            } else {
                runs.push_back(r);
            }
        }
    }
    vector<TextChange> changes;
    for (auto &r : runs) {
        if (r.a0 == r.a1 && r.b0 == r.b1) continue;
        changes.push_back(TextChange { as[(size_t)r.a0], as[(size_t)r.a1],
                                       string(to.substr(bs[(size_t)r.b0], bs[(size_t)r.b1] - bs[(size_t)r.b0])) });
    }
    return changes;
}

// Writes the formatted text over `path`, the file a link names included, as
// a whole: a file beside it renamed over it, with its permissions. False
// where the file is read-only or cannot be replaced.
inline bool WriteFormatted(const string &path, const string &text) {
    std::error_code ec;
    auto target = filesystem::canonical(filesystem::path(path), ec);
    if (ec) target = filesystem::path(path);
    auto perms = filesystem::status(target, ec).permissions();
    if (ec || (perms & filesystem::perms::owner_write) == filesystem::perms::none) return false;
    auto tmp = target.string() + ".format-tmp";
    auto f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    auto ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok = fclose(f) == 0 && ok;
    if (ok) {
        filesystem::permissions(filesystem::path(tmp), perms, ec);
        ok = !RenamePath(tmp, target.string());
    }
    if (!ok) filesystem::remove(filesystem::path(tmp), ec);
    return ok;
}

// The files `goose format dir` formats: the .goose files under it, sorted,
// leaving out directories whose names start with a dot and build trees (a
// directory holding a CMakeCache.txt), whose files are copies.
inline bool FormatFilesUnder(const string &dir, vector<string> &files) {
    std::error_code ec;
    vector<string> found;
    filesystem::recursive_directory_iterator it(filesystem::path(dir), filesystem::directory_options::none, ec), end;
    if (ec) return false;
    for (; it != end; it.increment(ec)) {
        if (ec) return false;
        auto &e = *it;
        auto name = e.path().filename().string();
        std::error_code ec2;
        if (e.is_directory(ec2)) {
            if ((!name.empty() && name[0] == '.') ||
                filesystem::exists(e.path() / "CMakeCache.txt", ec2))
                it.disable_recursion_pending();
            continue;
        }
        if (e.path().extension() == ".goose" && e.is_regular_file(ec2)) found.push_back(e.path().string());
    }
    sort(found.begin(), found.end());
    files.insert(files.end(), found.begin(), found.end());
    return true;
}

// `goose format [--check] [path...]`, or `goose format [-]` for standard
// input. Exits with 0 where every file is formatted (or now is), 1 where
// --check found one that is not, and 2 on any error, which wins.
inline int RunFormat(const vector<string> &args) {
    auto check = false, verify = false, usestdin = false;
    vector<string> paths;
    for (auto &a : args) {
        if (a == "--check") check = true;
        else if (a == "--verify") verify = true;
        else if (a == "-") usestdin = true;
        else if (a.size() > 1 && a[0] == '-') {
            fprintf(stderr, "goose format takes --check, files and directories, or - for standard "
                            "input, not %s\n", a.c_str());
            return 2;
        } else {
            paths.push_back(a);
        }
    }
    if (usestdin && !paths.empty()) {
        fprintf(stderr, "goose format -: standard input is the only input, with no files\n");
        return 2;
    }
    if (check && verify) {
        fprintf(stderr, "goose format takes --check or --verify, not both\n");
        return 2;
    }
    auto report = [](const string &name, const FormatResult &res) {
        if (!res.diags.empty()) {
            for (auto &d : res.diags) PrintDiagnosticText(d.text);
        } else {
            fprintf(stderr, "internal error formatting %s: %s; file left unchanged\n", name.c_str(),
                    res.internal.c_str());
        }
    };
    if (paths.empty()) {
        SetBinaryStdio();
        string in;
        char buf[65536];
        for (size_t n; (n = fread(buf, 1, sizeof buf, stdin)) > 0;) in.append(buf, n);
        auto res = Format(in, "<stdin>", verify);
        if (!res.ok) {
            report("<stdin>", res);
            return 2;
        }
        if (verify) return 0;
        if (check) {
            if (res.text == in) return 0;
            printf("<stdin>\n");
            return 1;
        }
        fwrite(res.text.data(), 1, res.text.size(), stdout);
        return 0;
    }
    auto code = 0;
    vector<string> files;
    for (auto &p : paths) {
        std::error_code ec;
        if (filesystem::is_directory(filesystem::path(p), ec)) {
            if (!FormatFilesUnder(p, files)) {
                fprintf(stderr, "cannot read the directory %s\n", p.c_str());
                code = 2;
            }
        } else if (filesystem::exists(filesystem::path(p), ec)) {
            files.push_back(p);
        } else {
            fprintf(stderr, "cannot open file: %s\n", p.c_str());
            code = 2;
        }
    }
    auto verified = 0, skipped = 0;
    for (auto &f : files) {
        string in;
        if (!LoadFile(f, in)) {
            fprintf(stderr, "cannot read %s\n", f.c_str());
            code = 2;
            continue;
        }
        auto res = Format(in, f, verify);
        if (!res.ok) {
            if (verify && res.internal.empty()) {
                printf("skip %s: does not parse\n", f.c_str());
                skipped++;
                continue;
            }
            report(f, res);
            code = 2;
            continue;
        }
        if (verify) {
            verified++;
            continue;
        }
        if (res.text == in) continue;
        if (check) {
            printf("%s\n", f.c_str());
            if (!code) code = 1;
            continue;
        }
        if (!WriteFormatted(f, res.text)) {
            fprintf(stderr, "cannot write %s; file left unchanged\n", f.c_str());
            code = 2;
        }
    }
    if (verify) printf("goose format --verify: %d file(s) verified, %d skipped\n", verified, skipped);
    return code;
}

}  // namespace goose
