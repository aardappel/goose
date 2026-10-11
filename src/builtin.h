// Goose compiler — the blessed packages (docs/design/packages.md §4.5) and
// the ones compiled into this compiler (§7.1): the table src/blessed.txt,
// embedded at the compiler's build, and what cmake/package.cmake built in,
// each with a symbol table for JIT runs, where its sources are, and where
// the response file a program using it links was written.
#pragma once

namespace goose {

// An entry of a package's symbol table, as native.h's goose_symbols.c and
// cmake/package.cmake's goose_symbols_<name>.c lay it out.
struct goose_symbol {
    const char *name;
    const void *addr;
};

struct BuiltinPackage {
    const char *name;
    const goose_symbol *(*symbols)();   // nullptr for a package without a native part.
    const char *srcdir;                 // Its root, with a trailing slash.
    const char *rspdir;                 // Where its link-{cc,msvc}.rsp are; "" without a native part.
};

}  // namespace goose

#include "builtin_packages.h"

namespace goose {

struct BlessedEntry {
    string import;      // The name a program imports.
    string package;     // The package holding the module of that name.
    string source;      // A path relative to the source tree, or a git repository.
    string ref;         // Its ref; "-" for a path.
};

inline const vector<BlessedEntry> &BlessedTable() {
    static const vector<BlessedEntry> table = [] {
        vector<BlessedEntry> t;
        string text = blessed_table;
        for (size_t i = 0; i < text.size();) {
            auto end = text.find('\n', i);
            if (end == string::npos) end = text.size();
            auto line = text.substr(i, end - i);
            i = end + 1;
            if (auto hash = line.find('#'); hash != string::npos) line.resize(hash);
            vector<string> fields;
            string cur;
            for (auto c : line) {
                if (c == ' ' || c == '\t' || c == '\r') {
                    if (!cur.empty()) fields.push_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            if (!cur.empty()) fields.push_back(cur);
            if (fields.size() == 4) t.push_back({ fields[0], fields[1], fields[2], fields[3] });
        }
        return t;
    }();
    return table;
}

inline const BlessedEntry *BlessedLookup(const string &import) {
    for (auto &e : BlessedTable()) if (e.import == import) return &e;
    return nullptr;
}

inline const BuiltinPackage *BuiltinLookup(const string &package) {
    for (auto b = builtin_packages; b->name; b++) if (package == b->name) return b;
    return nullptr;
}

inline int BuiltinIndex(const BuiltinPackage *b) {
    return b ? (int)(b - builtin_packages) : -1;
}

// The response file a program using the builtin package `b` links: in the
// directory the compiler's build recorded, else in pkg/<name> beside the
// compiler, for a copied build tree.
inline string BuiltinPackageLinkFile(const BuiltinPackage &b, const string &style,
                                     const string &exedir) {
    if (style != "msvc" && style != "cc")
        throw CompileError { cat("the link style is msvc or cc, not ", style) };
    if (!*b.rspdir)
        throw CompileError { cat("the package ", b.name, " has no native part to link") };
    auto name = cat("link-", style, ".rsp");
    auto beside = cat(exedir, "pkg/", b.name);
    for (auto &dir : { string(b.rspdir), beside }) {
        auto path = cat(dir, "/", name);
        if (PathExists(path)) return path;
    }
    throw CompileError { cat("cannot find ", name, " of the builtin package ", b.name,
                             " (looked in ", b.rspdir, " and ", beside, ")") };
}

// `--<import>-link msvc|cc`: the response file of the blessed package
// imported as `import`, when it is compiled into this compiler.
inline string BuiltinLinkFile(const string &import, const string &style, const string &exedir) {
    auto be = BlessedLookup(import);
    if (!be) throw CompileError { cat("--", import, "-link: no blessed package is imported as ", import) };
    auto b = BuiltinLookup(be->package);
    if (!b)
        throw CompileError { cat("this compiler was built without the package ", be->package,
                                 " (configure with -DGOOSE_BUILTIN_LIBS=all); goose build and "
                                 "goose run fetch and build it instead") };
    return BuiltinPackageLinkFile(*b, style, exedir);
}

}  // namespace goose
