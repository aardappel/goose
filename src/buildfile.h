// Goose compiler — build files: the compiler's settings file (GOOSE_HOME/
// config) and, later, the sidecar beside a program or a package's entry
// (docs/design/packages.md §4.7). Read with the Goose lexer, so they look
// like Goose: `//` comments, `key = value;`, strings, integers, true and
// false, and `[a, b, c]` lists.
#pragma once

namespace goose {

struct BuildValue {
    enum Kind { STR, INT, BOOL, LIST } kind = STR;
    string s;
    int64_t i = 0;
    bool b = false;
    vector<BuildValue> list;
};

struct BuildFile {
    string path;
    map<string, BuildValue> entries;
    vector<string> order;            // The keys as written, for messages that list them.

    const BuildValue *Get(const string &key) const {
        auto it = entries.find(key);
        return it == entries.end() ? nullptr : &it->second;
    }

    // A string entry, or `def` where there is none. An entry of another kind
    // is a CompileError naming the key.
    string Str(const string &key, const string &def = "") const {
        auto v = Get(key);
        if (!v) return def;
        if (v->kind != BuildValue::STR)
            throw CompileError { cat(path, ": ", key, " must be a string") };
        return v->s;
    }

    bool Bool(const string &key, bool def = false) const {
        auto v = Get(key);
        if (!v) return def;
        if (v->kind != BuildValue::BOOL)
            throw CompileError { cat(path, ": ", key, " must be true or false") };
        return v->b;
    }

    // A list of strings; a single string counts as a list of one.
    vector<string> Strings(const string &key) const {
        vector<string> out;
        auto v = Get(key);
        if (!v) return out;
        if (v->kind == BuildValue::STR) {
            out.push_back(v->s);
            return out;
        }
        if (v->kind != BuildValue::LIST)
            throw CompileError { cat(path, ": ", key, " must be a list of strings") };
        for (auto &e : v->list) {
            if (e.kind != BuildValue::STR)
                throw CompileError { cat(path, ": ", key, " must be a list of strings") };
            out.push_back(e.s);
        }
        return out;
    }

    // Rejects a key outside `known`, naming the known ones.
    void CheckKeys(const vector<string> &known) const {
        for (auto &k : order) {
            if (std::find(known.begin(), known.end(), k) != known.end()) continue;
            string list;
            for (auto &n : known) Append(list, list.empty() ? "" : ", ", n);
            throw CompileError { cat(path, ": unknown key ", k, " (the keys are ", list, ")") };
        }
    }
};

inline BuildValue ParseBuildValue(Lexer &lex) {
    BuildValue v;
    switch (lex.tok) {
        case T_STRLIT:
            v.kind = BuildValue::STR;
            v.s = lex.sval;
            lex.Next();
            return v;
        case T_INTLIT:
            v.kind = BuildValue::INT;
            v.i = lex.ival;
            lex.Next();
            return v;
        case T_TRUE:
        case T_FALSE:
            v.kind = BuildValue::BOOL;
            v.b = lex.tok == T_TRUE;
            lex.Next();
            return v;
        case T_LBRACKET:
            v.kind = BuildValue::LIST;
            lex.Next();
            while (lex.tok != T_RBRACKET) {
                v.list.push_back(ParseBuildValue(lex));
                if (lex.tok == T_COMMA) lex.Next();
                else if (lex.tok != T_RBRACKET) lex.Error("expected , or ] in a list");
            }
            lex.Next();
            return v;
        default:
            lex.Error("expected a string, an integer, true, false or a [list]");
    }
}

// Parses `source`, the text of the build file at `path`, whose name
// diagnostics carry (CompileError, as the lexer reports them).
inline BuildFile ParseBuildFile(const string &path, const string &source) {
    BuildFile bf;
    bf.path = path;
    Lexer lex(path, source.c_str());
    while (lex.tok != T_EOF) {
        if (lex.tok != T_IDENT) lex.Error("expected a key (an identifier) at the start of an entry");
        string key(lex.attr);
        lex.Next();
        if (lex.tok != T_ASSIGN) lex.Error(cat("expected = after the key ", key));
        lex.Next();
        auto value = ParseBuildValue(lex);
        if (lex.tok != T_SEMI) lex.Error(cat("expected ; after the value of ", key));
        lex.Next();
        if (bf.entries.count(key)) lex.Error(cat("the key ", key, " is given twice"));
        bf.entries[key] = std::move(value);
        bf.order.push_back(key);
    }
    return bf;
}

// The build file at `path`, or none (an empty one) when there is no such
// file. A file that does not parse is a CompileError.
inline BuildFile LoadBuildFile(const string &path, bool &found) {
    string source;
    found = LoadSource(path, source);
    if (!found) {
        BuildFile bf;
        bf.path = path;
        return bf;
    }
    return ParseBuildFile(path, source);
}

}  // namespace goose
