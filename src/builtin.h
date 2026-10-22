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

// The root of the builtin package `b`, with a trailing slash: where the
// compiler's build put it, else pkg/<name>/ beside the compiler, where a
// release (scripts/package_release.py) keeps it.
inline string BuiltinSrcDir(const BuiltinPackage &b, const string &exedir) {
    if (PathExists(cat(b.srcdir, b.name, ".goose"))) return b.srcdir;
    auto beside = cat(exedir, "pkg/", b.name, "/");
    for (auto &c : beside) if (c == '\\') c = '/';
    return beside;
}

// A response file beside the compiler names the libraries a release ships
// relative to its own directory, which a linker would take relative to the
// directory it runs in instead. The link takes a copy under GOOSE_HOME with
// those made absolute, one per place the compiler is installed.
inline string AbsoluteLinkFile(const string &rsp, const string &pkgname, const string &style) {
    std::error_code ec;
    auto dir = filesystem::absolute(filesystem::path(rsp), ec).parent_path();
    string text;
    if (ec || !LoadFile(rsp, text)) throw CompileError { cat("cannot read ", rsp) };
    string out;
    for (size_t i = 0; i < text.size();) {
        auto end = text.find('\n', i);
        if (end == string::npos) end = text.size();
        auto item = text.substr(i, end - i);
        i = end + 1;
        if (!item.empty() && item.back() == '\r') item.pop_back();
        if (item.empty()) continue;
        if (item.size() > 1 && item.front() == '"' && item.back() == '"')
            item = item.substr(1, item.size() - 2);
        auto path = filesystem::path(item);
        if (item[0] != '-' && path.is_relative() && filesystem::exists(dir / path, ec))
            item = (dir / path).lexically_normal().generic_string();
        Append(out, item.find(' ') != string::npos ? cat("\"", item, "\"") : item, "\n");
    }
    auto dest = cat(GooseHome(), "/build/builtin/", pkgname, "-", Hex8(dir.generic_string()),
                    "/link-", style, ".rsp");
    string old;
    if (LoadFile(dest, old) && old == out) return dest;
    filesystem::create_directories(filesystem::path(dest).parent_path(), ec);
    // Another goose may be linking with this file: it is replaced whole.
    FileLock lock(cat(dest, ".lock"),
                  cat("waiting for another goose writing the link file of ", pkgname));
    if (!WriteFileIfChanged(dest, out)) throw CompileError { cat("cannot write ", dest) };
    return dest;
}

// The response file a program using the builtin package `b` links: in the
// directory the compiler's build recorded, else in pkg/<name> beside the
// compiler, for a release or a copied build tree.
inline string BuiltinPackageLinkFile(const BuiltinPackage &b, const string &style,
                                     const string &exedir) {
    if (style != "msvc" && style != "cc")
        throw CompileError { cat("the link style is msvc or cc, not ", style) };
    if (!*b.rspdir)
        throw CompileError { cat("the package ", b.name, " has no native part to link") };
    auto name = cat("link-", style, ".rsp");
    auto recorded = cat(b.rspdir, "/", name);
    if (PathExists(recorded)) return recorded;
    auto beside = cat(exedir, "pkg/", b.name);
    if (auto shipped = cat(beside, "/", name); PathExists(shipped))
        return AbsoluteLinkFile(shipped, b.name, style);
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
