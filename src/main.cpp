// Goose compiler — driver. Includes establish the project-wide header order.

#include "includes.h"
#include "utils.h"
#include "lexer.h"
#include "ast.h"
#include "dump.h"
#include "clone.h"
#include "parser.h"
#include "resolve.h"
#include "builtins.h"
#include "gfx.h"
#include "typecheck.h"
#include "typecheck_types.h"
#include "typecheck_exprs.h"
#include "typecheck_flow.h"
#include "typecheck_calls.h"
#include "typecheck_builtins.h"
#include "typecheck_nodes.h"
#include "optimize.h"
#include "optimize_basecase.h"
#include "optimize_tre.h"
#include "optimize_loops.h"
#include "bce.h"
#include "codegen.h"
#include "codegen_types.h"
#include "codegen_frames.h"
#include "codegen_values.h"
#include "codegen_construct.h"
#include "codegen_stmts.h"
#include "codegen_calls.h"
#include "codegen_builtins.h"
#include "codegen_render.h"
#include "codegen_emit.h"
#include "codegen_stacks.h"
#include "codegen_nodes.h"
#include "runtime_inline.h"
#include "jit.h"
#include "process.h"
#include "buildfile.h"
#include "toolchain.h"
#include "builtin.h"
#include "build.h"
#include "packages.h"
#include "json.h"
#include "native.h"

namespace goose {

string DirOf(const string &path) {
    auto pos = path.find_last_of("/\\");
    return pos == string::npos ? "" : path.substr(0, pos + 1);
}

string ExePath(const char *argv0);

static bool FileExists(const string &path) {
    auto f = fopen(path.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

// A path split on either separator, with "." and cancelled "x/.." dropped. An
// absolute path keeps a leading empty component ("/a" -> {"", "a"}), so two
// absolute paths share it and an absolute and a relative one never do.
static vector<string> PathParts(const string &path) {
    vector<string> parts;
    string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        auto c = i < path.size() ? path[i] : '/';
        if (c != '/' && c != '\\') { cur += c; continue; }
        if (cur == "." || (cur.empty() && !parts.empty())) {}
        else if (cur == ".." && !parts.empty() && parts.back() != "..") parts.pop_back();
        else parts.push_back(cur);
        cur.clear();
    }
    return parts;
}

// `path` as reached from `dir`. Returns it unchanged where no relative path
// exists or one cannot be computed by name alone: an absolute path against a
// relative directory or the other way round, two absolute paths under
// different Windows drives, or a leftover ".." that only the filesystem could
// resolve.
string RelativeTo(const string &path, const string &dir) {
    auto p = PathParts(path), d = PathParts(dir);
    if (p.empty()) return path;
    auto updir = [](const vector<string> &parts) {
        for (auto &part : parts) if (part == "..") return true;
        return false;
    };
    if (updir(p) || updir(d)) return path;
    // A leading "" (rooted) or "C:" (drive) both sit in the first component.
    auto rooted = [](const vector<string> &parts) {
        return !parts.empty() && (parts[0].empty() || parts[0].back() == ':');
    };
    if (rooted(p) != rooted(d)) return path;
    if (rooted(p) && p[0] != d[0]) return path;
    // The last component of `path` is the file itself, never a shared directory.
    size_t i = 0;
    while (i + 1 < p.size() && i < d.size() && p[i] == d[i]) i++;
    string rel;
    for (size_t up = i; up < d.size(); up++) rel += "../";
    for (; i < p.size(); i++) {
        rel += p[i];
        if (i + 1 < p.size()) rel += '/';
    }
    return rel;
}

// Where the standard library lives (§11.1): an explicit --stdlib or
// GOOSE_STDLIB, else the `stdlib/` directory of the source tree the compiler
// was built in, found by walking up from the executable.
vector<string> StdlibDirs(const string &stdlibdir, const string &exe) {
    vector<string> dirs;
    if (!stdlibdir.empty()) dirs.push_back(cat(stdlibdir, "/"));
    if (auto env = getenv("GOOSE_STDLIB")) dirs.push_back(cat(env, "/"));
    auto exedir = DirOf(exe);
    dirs.push_back(cat(exedir, "stdlib/"));
    dirs.push_back(cat(exedir, "../stdlib/"));
    dirs.push_back(cat(exedir, "../../stdlib/"));
    dirs.push_back(cat(exedir, "../../../stdlib/"));
    dirs.push_back("stdlib/");
    return dirs;
}

// A package the root file declared with `import name from "source";`
// (docs/design/packages.md §4.3).
struct DeclaredPackage {
    PackageSource source;
    int pkg = 0;             // Its index in Ast::packages.
    int line = 0;            // Where the root declared it.
};

[[noreturn]] static void ImportError(const string &file, int line, const string &msg) {
    throw CompileError { cat(file, ":", line, ": error: ", msg) };
}

// Parses a root file and, transitively, everything it imports (each file once).
// `import a.b;` resolves relative to the importing file's package root (§11.1):
// the root file's directory for the program's own files, a library's
// directory for its modules; then as a module of a package the root
// declared; then in the standard library. `import .a.b;` resolves relative to
// the importing file's directory. `import name from "source";` declares a
// package in the root file and is a requirement anywhere else, which the
// root's declaration must satisfy (docs/design/packages.md §4.3). A
// `rootsource` is the root file's text, which is then not read from disk.
void ParseProgram(Ast &ast, const string &rootpath, const vector<string> &stdlibdirs,
                  const string *rootsource = nullptr) {
    // The program is package 0; a standard library directory becomes a
    // package the first time a module in it is reached.
    auto rootdir = DirOf(rootpath);
    ast.packages.push_back(Package { PathStem(rootpath), rootdir, false });
    LoadSidecar(ast.packages[0], true);
    auto stdlibpkg = [&](const string &dir) {
        for (size_t i = 0; i < ast.packages.size(); i++)
            if (ast.packages[i].stdlib && ast.packages[i].root == dir) return (int)i;
        ast.packages.push_back(Package { "stdlib", dir, true });
        return (int)ast.packages.size() - 1;
    };
    map<string, DeclaredPackage> declared;
    // Loads a blessed package (§4.5) once, under its package name among the
    // declared ones: from where the compiler's build put it when it is
    // compiled in, else from its source as a declared package would be (a
    // path in the table is relative to the source tree the standard
    // library is found in).
    auto loadblessed = [&](const BlessedEntry &be) -> int {
        auto it = declared.find(be.package);
        if (it != declared.end()) return it->second.pkg;
        auto builtin = BuiltinLookup(be.package);
        PackageSource src;
        src.text = be.source;
        string root;
        if (builtin && PathExists(cat(builtin->srcdir, be.package, ".goose"))) {
            src.ispath = true;
            src.dir = root = builtin->srcdir;
        } else if (IsPathSource(be.source)) {
            auto rel = be.source.rfind("./", 0) == 0 ? be.source.substr(2) : be.source;
            for (auto &dir : stdlibdirs) {
                auto cand = cat(dir, "../", rel);
                if (PathExists(cat(cand, "/", be.package, ".goose"))) {
                    src.ispath = true;
                    src.dir = PackageDir(cand);
                    break;
                }
            }
            if (src.dir.empty())
                throw CompileError { cat("cannot find the blessed package ", be.package,
                                         " at ", be.source, " of the source tree") };
            root = PackageRoot(src, be.package);
        } else {
            src = ParsePackageSource(cat(be.source, "@", be.ref), "");
            root = PackageRoot(src, be.package);
        }
        ast.packages.push_back(Package { be.package, root, false });
        auto &p = ast.packages.back();
        if (!src.ispath) p.cachekey = GitCacheKey(src);
        if (builtin && root == builtin->srcdir) p.builtin = BuiltinIndex(builtin);
        LoadSidecar(p, false);
        declared.emplace(be.package, DeclaredPackage { src, (int)ast.packages.size() - 1, 0 });
        return (int)ast.packages.size() - 1;
    };
    vector<pair<string, int>> queue = { { rootpath, 0 } };   // (path, its package).
    set<string> loaded = { rootpath };
    map<string, int> fileof;                 // Resolved path -> index in ast.sources.
    vector<vector<string>> importsof;        // Per source, the paths it imports.
    while (!queue.empty()) {
        auto [path, pkg] = queue.back();
        queue.pop_back();
        auto contents = make_unique<string>();
        if (rootsource && path == rootpath) *contents = *rootsource;
        else if (!LoadSource(path, *contents))
            throw CompileError { cat("cannot open file: ", path) };
        auto fileidx = (int)ast.sources.size();
        ast.sources.emplace_back(path, std::move(contents));
        ast.filepkg.push_back(pkg);
        fileof[path] = fileidx;
        importsof.emplace_back();
        auto &[filename, source] = ast.sources.back();
        Parser parser(ast, filename, source->c_str(), fileidx);
        parser.ParseTop();
        for (auto &imp : parser.imports) {
            auto imppkg = pkg;
            string imppath;
            if (!imp.source.empty()) {
                PackageSource src;
                try {
                    src = ParsePackageSource(imp.source, DirOf(path));
                } catch (CompileError &e) {
                    ImportError(filename, imp.line, e.msg);
                }
                auto it = declared.find(imp.path);
                if (fileidx == 0) {
                    // The root declares the package, and imports its entry.
                    if (it != declared.end())
                        ImportError(filename, imp.line, cat("the package ", imp.path,
                                                            " is declared twice"));
                    string root;
                    try {
                        root = PackageRoot(src, imp.path);
                    } catch (CompileError &e) {
                        ImportError(filename, imp.line, e.msg);
                    }
                    ast.packages.push_back(Package { imp.path, root, false });
                    if (!src.ispath) ast.packages.back().cachekey = GitCacheKey(src);
                    LoadSidecar(ast.packages.back(), false);
                    it = declared.emplace(imp.path, DeclaredPackage { src, (int)ast.packages.size() - 1,
                                                                      imp.line }).first;
                } else {
                    // A requirement: satisfied by the root's declaration, or
                    // by the blessed table where the root declared nothing.
                    if (it == declared.end())
                        if (auto be = BlessedLookup(imp.path); be && be->package == imp.path) {
                            loadblessed(*be);
                            it = declared.find(imp.path);
                        }
                }
                if (it == declared.end()) {
                    // A requirement the root does not satisfy: the error is
                    // the line to add there.
                    ImportError(filename, imp.line,
                                cat(ast.packages[pkg].name, " needs the package ", imp.path,
                                    ": add\n    import ", imp.path, " from \"",
                                    SourceForRoot(src, rootdir), "\";\nto ", rootpath,
                                    " (packages are declared by the root file only)"));
                } else if (fileidx != 0 && !SameSource(it->second.source, src)) {
                    ImportError(filename, imp.line,
                                cat(ast.packages[pkg].name, " needs the package ", imp.path,
                                    " from ", src.text, ", which ", rootpath, ":", it->second.line,
                                    " declared from a different source, ", it->second.source.text));
                } else if (fileidx != 0 && it->second.source.ref != src.ref) {
                    fprintf(stderr, "%s:%d: warning: %s needs the package %s at %s, which %s:%d "
                                    "declares at %s\n",
                            filename.c_str(), imp.line, ast.packages[pkg].name.c_str(),
                            imp.path.c_str(), src.ref.c_str(), rootpath.c_str(), it->second.line,
                            it->second.source.ref.c_str());
                }
                imppkg = it->second.pkg;
                // What a package requires links after it (§5.5).
                if (fileidx != 0 && pkg != imppkg) {
                    auto &req = ast.packages[pkg].required;
                    if (std::find(req.begin(), req.end(), imppkg) == req.end())
                        req.push_back(imppkg);
                }
                imppath = cat(ast.packages[imppkg].root, imp.path, ".goose");
            } else if (imp.relative) {
                imppath = cat(DirOf(path), imp.path, ".goose");
            } else {
                // A file under this package's root; a module of a package the
                // root declared (`import pkg;` is its entry, `import pkg.sub;`
                // its sub.goose); a blessed name, its package loaded as if
                // the root had declared it (§4.5); the standard library.
                auto local = cat(ast.packages[pkg].root, imp.path, ".goose");
                auto first = imp.path.substr(0, imp.path.find('/'));
                auto rest = imp.path.size() > first.size() ? imp.path.substr(first.size() + 1)
                                                           : first;
                auto it = declared.find(first);
                // A package's own name, from inside it, is its own file.
                if (it != declared.end() && it->second.pkg == pkg) it = declared.end();
                auto blessed = it == declared.end() && !FileExists(local) ? BlessedLookup(first)
                                                                           : nullptr;
                if (it != declared.end()) {
                    if (FileExists(local))
                        ImportError(filename, imp.line,
                                    cat(imp.path, " is both a file of this package (", local,
                                        ") and the declared package ", first, "'s module"));
                    imppkg = it->second.pkg;
                    imppath = cat(ast.packages[imppkg].root, rest, ".goose");
                } else if (blessed) {
                    imppkg = loadblessed(*blessed);
                    imppath = cat(ast.packages[imppkg].root, rest, ".goose");
                } else {
                    imppath = local;
                    if (!FileExists(local)) {
                        for (auto &dir : stdlibdirs) {
                            auto cand = cat(dir, imp.path, ".goose");
                            if (FileExists(cand)) {
                                imppath = cand;
                                imppkg = stdlibpkg(dir);
                                break;
                            }
                        }
                    }
                }
            }
            importsof[fileidx].push_back(imppath);
            if (loaded.insert(imppath).second) queue.push_back({ imppath, imppkg });
        }
    }
    // Globals initialize in import order: everything a file imports runs before
    // the file's own, so a global initializer may name an imported global
    // (`let IDENT = Xf { ..., t: float3_0 }`). Files parse in worklist order,
    // which is not that, so rank them by a post-order walk of the import graph
    // and stable-sort the initialization list by rank. A file in an import
    // cycle is visited once, and its rank is then whatever the walk reached
    // first, which is all that order can mean there.
    vector<int> rank(ast.sources.size(), 0);
    vector<char> state(ast.sources.size(), 0);   // 0 unvisited, 1 on the stack, 2 ranked.
    auto next = 0;
    std::function<void(int)> visit = [&](int f) {
        if (state[f]) return;
        state[f] = 1;
        for (auto &p : importsof[f]) {
            auto it = fileof.find(p);
            if (it != fileof.end()) visit(it->second);
        }
        state[f] = 2;
        rank[f] = next++;
    };
    for (size_t i = 0; i < ast.sources.size(); i++) visit((int)i);
    std::stable_sort(ast.globals.begin(), ast.globals.end(),
                     [&](VarDecl *a, VarDecl *b) {
                         return rank[a->line.fileidx] < rank[b->line.fileidx];
                     });
}

// Parses a program's dump as a program of its own and requires that dumping
// that gives back the same text: what `goose --dump` of the dump written to a
// file would print. `path` is where the dump's diagnostics say they are.
void CheckRoundtrip(const string &dumped, const string &path,
                    const vector<string> &stdlibdirs) {
    Ast again;
    try {
        ParseProgram(again, path, stdlibdirs, &dumped);
    } catch (CompileError &e) {
        throw CompileError { cat("the dump does not parse again:\n", e.msg) };
    }
    string redumped;
    again.Dump(redumped);
    if (redumped == dumped) return;
    // The first line that differs, which is where the two parses part.
    size_t pos = 0, line = 1;
    while (pos < dumped.size() && pos < redumped.size() && dumped[pos] == redumped[pos]) {
        if (dumped[pos] == '\n') line++;
        pos++;
    }
    auto lineat = [&](const string &s) {
        auto start = pos ? s.rfind('\n', pos - 1) : string::npos;
        start = start == string::npos ? 0 : start + 1;
        auto end = s.find('\n', pos);
        return s.substr(start, end == string::npos ? string::npos : end - start);
    };
    throw CompileError { cat("the dump does not roundtrip: its line ", line, " reads\n    ",
                             lineat(dumped), "\nand dumps again as\n    ", lineat(redumped)) };
}

void DumpTokens(const string &path) {
    string contents;
    if (!LoadSource(path, contents)) throw CompileError { cat("cannot open file: ", path) };
    Lexer lex(path, contents.c_str());
    string s;
    while (lex.tok != T_EOF) {
        Append(s, TName(lex.tok));
        switch (lex.tok) {
            case T_IDENT:  Append(s, " ", lex.attr); break;
            case T_INTLIT: Append(s, " ", lex.ival, " (", lex.attr, ")"); break;
            case T_FLTLIT: Append(s, " ", lex.fval); break;
            case T_STRLIT: s += " "; EscapeString(s, lex.sval, '"'); break;
            default: break;
        }
        s += "\n";
        lex.Next();
    }
    fputs(s.c_str(), stdout);
}

// The runtime C sources embedded into the compiler (runtime_inline.h), in the
// order a standalone program holds them (runtime.h has how they divide).
static const char *runtimefiles[] = { "runtime.h", "runtime_impl.h", "runtime_threads.h",
                                      "runtime_ext.h", "runtime_os.h" };

static string RuntimeSections(std::initializer_list<const char *> names) {
    string s;
    for (auto name : names)
        for (auto &rf : runtime_files)
            if (string_view(rf.name) == name)
                Append(s, "/* ==== ", rf.name, " ==== */\n", rf.text, "\n");
    return s;
}

// What a separately built program and its runtime object must agree on: a
// hash of the runtime's text, which names the runtime's entry point.
string RuntimeVersion() {
    uint64_t h = 14695981039346656037ull;
    for (auto &rf : runtime_files)
        for (auto c : string_view(rf.text)) h = (h ^ (uint8_t)c) * 1099511628211ull;
    char buf[17];
    snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)(h >> 16));
    return buf;
}

// The runtime object's C (goose --emit-runtime): the runtime that programs
// written with -o declare extern, for any program this compiler writes.
string RuntimeObjectSource() {
    return cat("/* The Goose runtime, written by `goose --emit-runtime`. Do not edit.\n"
               "   Compile it once, with the C compiler and flags of the programs it is\n"
               "   for, and link the object with every program `goose -o` writes (all\n"
               "   but --standalone ones). It needs no -D: a program hands the runtime\n"
               "   its configuration as it starts. A program from another version of\n"
               "   the compiler fails to link against it, missing gs_rt_start_<version>. */\n\n"
               "#define GS_SEPARATE_RUNTIME 1\n#define GS_RUNTIME_OBJECT 1\n"
               "#define GS_NEED_THREADS 1\n#define GS_RUNTIME_VERSION ", RuntimeVersion(),
               "\n\n", RuntimeSections({ "runtime.h", "runtime_impl.h", "runtime_threads.h",
                                          "runtime_ext.h", "runtime_os.h" }));
}

// Locates the src/runtime/ directory (only needed by --gen-runtime-header):
// an explicit env override, next to the executable, or relative to it in the
// source tree layout.
string FindRuntimeDir(const char *exe) {
    vector<string> cands;
    if (auto env = getenv("GOOSE_RUNTIME")) cands.push_back(cat(env, "/"));
    auto exedir = DirOf(exe);
    cands.push_back(cat(exedir, "runtime/"));
    cands.push_back(cat(exedir, "../../src/runtime/"));
    cands.push_back(cat(exedir, "../src/runtime/"));
    cands.push_back("src/runtime/");
    for (auto &dir : cands) {
        string probe;
        if (LoadFile(cat(dir, runtimefiles[0]), probe)) return dir;
    }
    throw CompileError { "cannot locate the runtime sources (src/runtime); "
                         "set GOOSE_RUNTIME" };
}

// Regenerates src/runtime_inline.h from src/runtime/, so the compiler binary
// is self-contained. Anyone editing the runtime re-runs this and rebuilds.
void GenRuntimeHeader(const char *exe) {
    auto dir = FindRuntimeDir(exe);
    string out = "// Generated by `goose --gen-runtime-header` from src/runtime/. "
                 "Do not edit.\n#pragma once\n\nnamespace goose {\n\n"
                 "struct RuntimeFile {\n    const char *name;\n    string_view text;\n"
                 "};\n\ninline const RuntimeFile runtime_files[] = {\n";
    for (auto rf : runtimefiles) {
        string src;
        if (!LoadFile(cat(dir, rf), src))
            throw CompileError { cat("cannot read runtime file: ", dir, rf) };
        if (src.find(")GSRT\"") != string::npos)
            throw CompileError { cat(rf, " contains the raw-string delimiter )GSRT\"") };
        Append(out, "    { \"", rf, "\", string_view(\n");
        // Chunked adjacent raw strings: MSVC caps single literals at ~16K.
        size_t pos = 0;
        while (pos < src.size()) {
            auto end = std::min(pos + 8000, src.size());
            if (end < src.size()) {
                auto nl = src.rfind('\n', end);
                if (nl != string::npos && nl > pos) end = nl + 1;
            }
            Append(out, "R\"GSRT(", string_view(src).substr(pos, end - pos), ")GSRT\"\n");
            pos = end;
        }
        if (src.empty()) out += "\"\"\n";
        out += "    ) },\n";
    }
    out += "};\n\n}  // namespace goose\n";
    auto path = cat(dir, "../runtime_inline.h");
    auto f = fopen(path.c_str(), "wb");
    if (!f) throw CompileError { cat("cannot write ", path) };
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    printf("wrote %s (%d bytes)\n", path.c_str(), (int)out.size());
}

// At the end of this file, past the system headers it needs.
int RunOnCompilerStack(const function<int()> &fn);

// What --multi-test prints on a line of its own on stdout and on stderr after
// each file, with the exit code a run on the file alone would have had and
// the file's name as given.
constexpr const char *MULTIMARK = "==== goose --multi-test: exit";

// A size with an optional K, M, G or T suffix (--stack-reserve).
bool ParseSize(string s, uint64_t &out) {
    if (!s.empty() && toupper((unsigned char)s.back()) == 'B') s.pop_back();
    if (s.empty()) return false;
    uint64_t mult = 1;
    switch (toupper((unsigned char)s.back())) {
        case 'K': mult = 1ull << 10; s.pop_back(); break;
        case 'M': mult = 1ull << 20; s.pop_back(); break;
        case 'G': mult = 1ull << 30; s.pop_back(); break;
        case 'T': mult = 1ull << 40; s.pop_back(); break;
    }
    if (s.empty() || s.size() > 18 || s.find_first_not_of("0123456789") != string::npos)
        return false;
    auto n = strtoull(s.c_str(), nullptr, 10);
    if (n > UINT64_MAX / mult) return false;
    out = n * mult;
    return true;
}

// The value of a runtime configuration macro as runtime.h spells its
// defaults and a -D may: an integer with C suffixes, or `(N << M)`.
bool ParseConfigValue(string s, uint64_t &out) {
    auto strip = [](string &t) {
        while (!t.empty() && isspace((unsigned char)t.back())) t.pop_back();
        size_t i = 0;
        while (i < t.size() && isspace((unsigned char)t[i])) i++;
        t.erase(0, i);
    };
    strip(s);
    while (s.size() >= 2 && s.front() == '(' && s.back() == ')') {
        s = s.substr(1, s.size() - 2);
        strip(s);
    }
    auto num = [&](string t, uint64_t &v) {
        strip(t);
        while (!t.empty() && (tolower((unsigned char)t.back()) == 'u' ||
                              tolower((unsigned char)t.back()) == 'l'))
            t.pop_back();
        if (t.empty() || t.size() > 19 || t.find_first_not_of("0123456789") != string::npos)
            return false;
        v = strtoull(t.c_str(), nullptr, 10);
        return true;
    };
    if (auto sh = s.find("<<"); sh != string::npos) {
        uint64_t a, b;
        if (!num(s.substr(0, sh), a) || !num(s.substr(sh + 2), b) || b > 63) return false;
        out = a << b;
        return true;
    }
    return num(s, out);
}

// The data stack configuration the generated program compiles with, for
// the --stacks report: the runtime's defaults (runtime.h), then the -D
// given, --stack-reserve's among them. A -D the compiler cannot read keeps
// the default in the report; the C compiler sees it either way.
CodeGen::StackConfig StackConfigOf(const vector<string> &cdefines) {
    string_view runtime;
    for (auto &rf : runtime_files)
        if (string_view(rf.name) == "runtime.h") runtime = rf.text;
    auto value = [&](const char *name, uint64_t &out) {
        auto key = cat("#define ", name, " ");
        if (auto at = runtime.find(key); at != string_view::npos) {
            auto line = runtime.substr(at + key.size());
            line = line.substr(0, line.find('\n'));
            if (auto c = line.find("/*"); c != string_view::npos) line = line.substr(0, c);
            ParseConfigValue(string(line), out);
        }
        for (auto &d : cdefines) {
            auto eq = d.find('=');
            if (d.substr(0, eq) == name)
                ParseConfigValue(eq == string::npos ? string("1") : d.substr(eq + 1), out);
        }
    };
    CodeGen::StackConfig cfg { 0, 0, 0, 0 };
    uint64_t maxstacks = 0;
    value("GS_STACK_RESERVE", cfg.reserve);
    value("GS_STACK_GAP", cfg.gap);
    value("GS_STACK_BUDGET", cfg.budget);
    value("GS_MAX_STACKS", maxstacks);
    cfg.maxstacks = (int64_t)maxstacks;
    return cfg;
}

int Main(int argc, char **argv) {
    auto exe = ExePath(argv[0]);
    // The verb forms, `goose run|build|check|fetch|toolchain|version ...`. A
    // first argument that is none of them is read as an option or a file, as
    // it always was, so `goose foo.goose` is `goose run foo.goose`.
    string verb;
    auto firstarg = 1;
    if (argc > 1) {
        string v = argv[1];
        if (v == "run" || v == "build" || v == "check" || v == "fetch" || v == "toolchain" ||
            v == "version") {
            verb = v;
            firstarg = 2;
        }
    }
    if (verb == "version") {
        printf("goose runtime %s\n", RuntimeVersion().c_str());
        return 0;
    }
    string outname, headername, stdlibdir, shaderfile, shadersource, dumpfile;
    auto dump = false, tokens = false, parseonly = false, specs = false, nocgen = false;
    auto roundtrip = false, multitest = false;
    auto nobce = false, bcetest = false, bcelines = false, norfcheck = false;
    auto stacks = false;
    auto forcejit = false, standalone = false;
    // --split: the number of C files -o writes; 0 is one, -1 as many as the
    // program and the machine's cores call for (a debug build's choice).
    int64_t splitn = 0;
    auto json = false, refresh = false, debug = false, verbose = false, dryrun = false;
    string ccrequest;
    // A build is for a deliverable: the inlining and the C compiler's
    // optimization both at their highest.
    auto optlevel = verb == "build" ? 2 : 1;
    vector<string> cdefines, progargs, includenames, files;
    for (int i = firstarg; i < argc; i++) {
        string arg = argv[i];
        // Everything past `--` belongs to the program being run, not here.
        if (arg == "--") {
            for (int j = i + 1; j < argc; j++) progargs.push_back(argv[j]);
            break;
        }
        if (arg == "--dump") dump = true;
        else if (arg == "--tokens") tokens = true;
        else if (arg == "--parse") parseonly = true;
        else if (arg == "--roundtrip") roundtrip = true;
        else if (arg == "--dump-file" && i + 1 < argc) dumpfile = argv[++i];
        else if (arg == "--specs") specs = true;
        else if (arg == "--stacks") stacks = true;
        else if (arg == "--stack-reserve" && i + 1 < argc) {
            uint64_t size;
            if (!ParseSize(argv[++i], size) || !size || size > (1ull << 48)) {
                fprintf(stderr, "--stack-reserve takes a size from 1 byte to 256 TB (2^48, "
                                "goose_spec.md 10.4), with an optional K, M, G or T suffix: %s\n",
                        argv[i]);
                return 1;
            }
            cdefines.push_back(cat("GS_STACK_RESERVE=", size, "ull"));
        }
        else if (arg == "--check") nocgen = true;
        else if (arg == "--no-bce") nobce = true;
        else if (arg == "--bce-test") bcetest = true;
        else if (arg == "--bce-lines") bcelines = true;
        // Unsound; a measurement aid only (see CodeGen::norfcheck).
        else if (arg == "--unsafe-no-rf-check") norfcheck = true;
        else if (arg == "--jit") forcejit = true;
        else if (arg == "--standalone") standalone = true;
        else if (arg == "--split" && i + 1 < argc) {
            splitn = atoll(argv[++i]);
            if (splitn < 1 || splitn > 256) {
                fprintf(stderr, "--split takes a number of C files from 1 to 256: %s\n", argv[i]);
                return 1;
            }
        }
        else if (arg == "--multi-test") multitest = true;
        else if (arg == "--gen-runtime-header") { GenRuntimeHeader(exe.c_str()); return 0; }
        else if (arg == "--emit-runtime" && i + 1 < argc) {
            auto path = argv[++i];
            auto src = RuntimeObjectSource();
            auto f = fopen(path, "wb");
            if (!f) {
                fprintf(stderr, "cannot write output file: %s\n", path);
                return 1;
            }
            fwrite(src.data(), 1, src.size(), f);
            fclose(f);
            printf("wrote %s (%d bytes)\n", path, (int)src.size());
            return 0;
        }
        // Hidden: what a shader compiles to, without a program around it.
        else if (arg == "--compile-shader" && i + 1 < argc) shaderfile = argv[++i];
        else if (arg == "--shader-source" && i + 1 < argc) shadersource = argv[++i];
        // `--<name>-link msvc|cc`: the response file of a blessed package
        // compiled into this compiler (builtin.h).
        else if (arg.size() > 7 && arg.rfind("--", 0) == 0 &&
                 arg.compare(arg.size() - 5, 5, "-link") == 0 && i + 1 < argc) {
            try {
                auto style = argv[++i];
                auto path = BuiltinLinkFile(arg.substr(2, arg.size() - 7), style, DirOf(exe));
                printf("%s\n", path.c_str());
            } catch (CompileError &e) {
                fprintf(stderr, "%s\n", e.msg.c_str());
                return 1;
            }
            return 0;
        }
        else if (arg == "-O0") optlevel = 0;
        else if (arg == "-O1") optlevel = 1;
        else if (arg == "-O2") optlevel = 2;
        else if (arg == "-o" && i + 1 < argc) outname = argv[++i];
        else if (arg == "--header" && i + 1 < argc) headername = argv[++i];
        else if (arg == "--include" && i + 1 < argc) includenames.push_back(argv[++i]);
        else if (arg == "--stdlib" && i + 1 < argc) stdlibdir = argv[++i];
        else if (arg == "--cc" && i + 1 < argc) ccrequest = argv[++i];
        else if (arg == "--json") json = true;
        else if (arg == "--refresh") refresh = true;
        else if (arg == "--debug") {
            debug = true;
            cdefines.push_back("GS_DEBUG=1");
        }
        else if (arg == "-v") verbose = true;
        else if (arg == "--dry-run") dryrun = true;
        else if (arg == "--update") package_update = true;
        // A -D lands in the generated C itself rather than on some backend's
        // command line, so a JIT run and a compiled one see the same source.
        else if (arg == "-D" && i + 1 < argc) cdefines.push_back(argv[++i]);
        else if (arg.rfind("-D", 0) == 0 && arg.size() > 2) cdefines.push_back(arg.substr(2));
        else if (!arg.empty() && arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return 1;
        }
        else files.push_back(arg);
    }
    if (verb == "toolchain") {
        try {
            auto tools = FindTools(ccrequest, refresh);
            // The modules a program can import without a build: those of
            // the blessed packages compiled in.
            vector<string> builtin;
            for (auto &e : BlessedTable())
                if (BuiltinLookup(e.package)) builtin.push_back(e.import);
            fputs(ToolsReport(tools, json, builtin).c_str(), stdout);
            return tools.havecc ? 0 : 1;
        } catch (CompileError &e) {
            fprintf(stderr, "%s\n", e.msg.c_str());
            return 1;
        }
    }
    // What `goose build` builds with; its -o names the executable, and the
    // C goes to the program's build directory (build.h).
    BuildOptions buildopts;
    string builddir;
    if (verb == "run") {
        if (!outname.empty()) {
            fprintf(stderr, "goose run takes no -o: it compiles and runs the program in this "
                            "process\n");
            return 1;
        }
        forcejit = true;
    } else if (verb == "check") {
        nocgen = true;
    } else if (verb == "fetch") {
        // Parsing is what fetches: every package the root declares is
        // fetched as its entry is reached.
        parseonly = true;
    } else if (verb == "build") {
        if (multitest || !headername.empty()) {
            fprintf(stderr, "goose build takes one program and no --header; the flag form "
                            "(-o out.c) writes the C for a C host\n");
            return 1;
        }
        buildopts = BuildOptions { ccrequest, debug, verbose, dryrun, standalone, outname };
        outname.clear();
        // A debug build is for the edit-build loop: C compiled in parallel.
        // A release one stays in one file, where the C compiler can inline.
        // MSVC compiles one file at /Od faster than it starts and links
        // several, so its debug builds stay in one file too.
        if (debug && !standalone && !splitn) {
            Toolchain tc;
            string why;
            if (!FindToolchain(ccrequest, tc, why) || tc.name != "msvc") splitn = -1;
        }
    }
    if (splitn && (standalone || multitest || forcejit)) {
        fprintf(stderr, "--split needs -o and the separate runtime: no --standalone, "
                        "--multi-test or --jit\n");
        return 1;
    }
    if (files.size() > 1 && !multitest) {
        fprintf(stderr, "multiple input files given\n");
        return 1;
    }
    if (!shaderfile.empty()) {
        try {
            DumpShader(shaderfile, shadersource);
        } catch (CompileError &e) {
            fprintf(stderr, "%s\n", e.msg.c_str());
            return 1;
        }
        return 0;
    }
    if (files.empty()) {
        fprintf(stderr, "usage: goose run file.goose [options] [-- program args...] | "
                        "goose check file.goose [options] | goose fetch [--update] file.goose | "
                        "goose version |\n"
                        "       goose build file.goose [options] [-o exe] [--debug] [--standalone] "
                        "[--cc name|path] [-v] [--dry-run] |\n"
                        "       goose toolchain [--cc name|path] [--json] [--refresh]\n");
        fprintf(stderr, "   or: goose [--dump] [--parse] [--tokens] [--roundtrip] "
                        "[--dump-file out.goose] [--specs] [--stacks] [--check] "
                        "[--no-bce] [--bce-test] [--bce-lines] [--unsafe-no-rf-check] [-O0|-O1|-O2] "
                        "[-o out.c [--standalone | --split n]] [--jit] [-DNAME=VALUE]... "
                        "[--stack-reserve size] "
                        "[--include header.h]... "
                        "[--header out.h] "
                        "[--stdlib dir] file.goose [-- program args...] | "
                        "--multi-test [options] file.goose... | --emit-runtime runtime.c | "
                        "--gen-runtime-header | "
                        "--<package>-link msvc|cc\n");
        fprintf(stderr, "without -o the program is compiled and run in this process by TinyCC.\n");
        fprintf(stderr, "the .c that -o writes links with the runtime that --emit-runtime "
                        "writes, compiled once;\n--standalone writes one that holds the "
                        "runtime itself.\n");
        fprintf(stderr, "--stacks reports the data stacks the program and each worker can take "
                        "and the thread cap they\ngive hardware_threads(); --stack-reserve sets "
                        "the address space reserved per stack (K/M/G/T suffixes).\n");
        fprintf(stderr, "--multi-test compiles each file as a run of its own would, one after "
                        "another, ending what each\nprinted with a line `%s <exit code> <file>` "
                        "on stdout and on stderr; an -o names\neach file's C with a %% "
                        "standing for the file's name without its extension.\n", MULTIMARK);
        return 1;
    }
    // With no output file the program is compiled into this process and run,
    // which is what --jit asks for explicitly; a build writes the C to its
    // build directory instead.
    auto jit = verb != "build" && (forcejit || outname.empty());
    if (!headername.empty() && (jit || multitest)) {
        fprintf(stderr, "--header requires C output for a single input file\n");
        return 1;
    }
    // A header is for a C host, which supplies its own main and starts the
    // program through goose_init.
    auto library = !headername.empty();
    if (multitest && (tokens || !dumpfile.empty() || !progargs.empty() ||
                      (!outname.empty() && outname.find('%') == string::npos) ||
                      (jit && !nocgen && !parseonly && !dump))) {
        fprintf(stderr, "--multi-test runs no programs and writes no dump files: it takes "
                        "--check, --parse, --dump or an -o with a %%, and no --tokens, "
                        "--dump-file, --jit or program arguments\n");
        return 1;
    }
    // The program shares stdout in JIT mode; progress goes to stderr.
    auto msgs = jit ? stderr : stdout;
    // The file being compiled, the C file it goes to, if any, and the
    // --include headers as that C file names them.
    string filename, outfile;
    vector<string> includes;
    // The C files -o wrote: outfile, then the rest of a split program's.
    vector<string> cfiles;
    // What a JIT run compiles and starts, once the compile produced it.
    string program;
    // The program's Ast outlives the compile: a build and a JIT run read
    // its packages, for the native parts the program needs (native.h).
    unique_ptr<Ast> astp;
    vector<int> nativepkgs;
    auto compile = [&]() -> int {
        if (tokens) {
            DumpTokens(filename);
            return 0;
        }
        astp = make_unique<Ast>();
        auto &ast = *astp;
        auto stdlibdirs = StdlibDirs(stdlibdir, exe);
        ParseProgram(ast, filename, stdlibdirs);
        if (dump) {
            // Dump is parse-level output: no name resolution or typecheck,
            // so parse-only test files can roundtrip, and every name shows
            // as written.
            string s;
            ast.Dump(s);
            fputs(s.c_str(), stdout);
            return 0;
        }
        // The dump as --dump prints it, taken before resolution.
        string dumped;
        if (roundtrip || !dumpfile.empty()) ast.Dump(dumped);
        if (!dumpfile.empty()) {
            auto f = fopen(dumpfile.c_str(), "wb");
            if (!f) throw CompileError { cat("cannot write dump file: ", dumpfile) };
            fwrite(dumped.data(), 1, dumped.size(), f);
            fclose(f);
        }
        ResolveTypeNames(ast);
        // Printed by every run that gets this far, so that one which goes on
        // to fail says by itself which side of resolution it failed on.
        fprintf(msgs, "parsed ok: %d top-level declarations, %d file(s)\n",
                (int)ast.topdecls.size(), (int)ast.sources.size());
        // After resolution, so that a program that does not resolve fails as
        // it would without --roundtrip.
        if (roundtrip) {
            CheckRoundtrip(dumped, dumpfile.empty() ? cat(filename, ".dump") : dumpfile,
                           stdlibdirs);
            fprintf(msgs, "roundtrip ok: %d bytes of dump\n", (int)dumped.size());
        }
        if (parseonly) return 0;
        TypeCheckProgram(ast, library);
        Optimizer opt(ast, optlevel);
        if (specs) {
            string s;
            opt.DumpSpecs(s);
            fputs(s.c_str(), stdout);
        }
        fprintf(msgs, "typechecked ok: %d specialization(s), %d struct/%d enum instance(s); "
                "optimized -O%d: %d inlined, %d base case(s), %d folded, %d propagated, "
                "%d tail loop(s)\n",
                (int)ast.fnspecs.size(), (int)ast.structinsts.size(),
                (int)ast.enuminsts.size(), optlevel, opt.inlined, opt.basecases,
                opt.folded, opt.propagated, opt.tailloops);
        BCE bce(ast);
        if (!nobce) {
            bce.RunAll();
            fprintf(msgs, "bce: elided %d/%d index and %d/%d slice checks\n",
                    bce.idxelided, bce.idxtotal, bce.slelided, bce.sltotal);
        }
        // Per-line outcomes, for comparing two builds of the pass.
        if (bcelines)
            for (auto &[where, counts] : bce.lineout)
                fprintf(msgs, "bce-line: %s:%d: %d elided, %d kept\n",
                        ast.sources[where.first].first.c_str(), where.second,
                        counts.first, counts.second);
        if (bcetest) {
            auto fails = bce.VerifyAnnotations();
            if (fails) {
                fprintf(stderr, "bce-test: %d annotation failure(s)\n", fails);
                return 1;
            }
            fprintf(msgs, "bce-test: all annotations verified\n");
        }
        // --stacks needs the generated program's bounds, so --check runs
        // the backend for them and writes nothing.
        if (nocgen && !stacks) return 0;
        // A quoted include resolves against the including file's own directory
        // first, so the --include headers are written relative to where the .c
        // goes: the generated file then compiles wherever the tree sits,
        // instead of carrying this machine's absolute paths. A name that is
        // not a file from here is one the C compiler is meant to find on its
        // own include path, and is left alone. A JIT run has no file to be
        // relative to and resolves them from the working directory.
        if (!outfile.empty())
            for (auto &inc : includes)
                if (FileExists(inc)) inc = RelativeTo(inc, DirOf(outfile));
        // The extern-support runtime is written against the generated types,
        // so it goes in after them rather than up front.
        string_view runtime_ext_text;
        for (auto &rf : runtime_files)
            if (string_view(rf.name) == "runtime_ext.h") runtime_ext_text = rf.text;
        auto stackcfg = StackConfigOf(cdefines);
        CodeGen cg(ast, runtime_ext_text, includes, norfcheck, library, stackcfg.maxstacks);
        if (stacks) cg.StackReport(msgs, stackcfg);
        // The packages whose native parts the program needs, which a JIT
        // run takes functions from and a build links: its own, when it has
        // one, and every package an extern fn it calls was declared in,
        // each before the packages it requires, which is the order a
        // linker resolves their archives in.
        nativepkgs = NativePackagesUsed(ast, cg.usedexterns);
        // Assemble: compiler-set feature defines, the embedded runtime, then
        // the generated program. A program built with a separate runtime
        // holds what runtime.h has of it and the extern support's
        // declarations; a standalone one, as a JIT run builds, all of it.
        auto assemble = [&](bool separate) {
            string out = cat("/* Generated by the Goose compiler from ", filename,
                             ". Do not edit.\n"
                             "   Names from the program carry a _g suffix, which keeps them clear of\n"
                             "   the C keywords, of the runtime's gs_ names and of whatever this\n"
                             "   platform's headers declare; a namespaced name ns::x is ns_x_g followed\n"
                             "   by the namespace's length. An --include header names them that way.",
                             separate ? "\n   Link it with the runtime that `goose --emit-runtime` "
                                        "writes. */\n\n#define GS_SEPARATE_RUNTIME 1\n"
                                        "#define GS_RUNTIME_VERSION " + RuntimeVersion() + "\n"
                                      : " */\n\n",
                             cg.predefs);
            // -D goes into the source rather than onto a backend's command
            // line, so both backends compile the same text.
            for (auto &d : cdefines) {
                auto eq = d.find('=');
                Append(out, "#define ", eq == string::npos ? d : d.substr(0, eq), " ",
                       eq == string::npos ? string("1") : d.substr(eq + 1), "\n");
            }
            if (separate)
                Append(out, RuntimeSections({ "runtime.h" }), cg.head,
                       RuntimeSections({ "runtime_ext.h" }), cg.result);
            else
                Append(out, RuntimeSections({ "runtime.h", "runtime_impl.h", "runtime_threads.h" }),
                       cg.head, RuntimeSections({ "runtime_ext.h", "runtime_os.h" }), cg.result);
            return out;
        };
        auto write = [&](const string &path, const string &text) {
            auto f = fopen(path.c_str(), "wb");
            if (!f) throw CompileError { cat("cannot write output file: ", path) };
            fwrite(text.data(), 1, text.size(), f);
            fclose(f);
        };
        cfiles.clear();
        if (!outfile.empty() && !nocgen && splitn) {
            // A split program: everything before the code in a header, and the
            // functions dealt to the files, the largest first to the smallest.
            auto out = assemble(true);
            auto codeat = out.size() - cg.result.size() + cg.codestart;
            vector<pair<size_t, size_t>> fns;
            size_t from = codeat;
            for (auto cut : cg.cuts) {
                if (codeat + cut > from) fns.push_back({ from, codeat + cut });
                from = codeat + cut;
            }
            if (out.size() > from) fns.push_back({ from, out.size() });
            // Each file pays for the header, so a small program takes fewer.
            auto n = splitn > 0 ? splitn
                     : std::min<int64_t>(std::max(1u, std::thread::hardware_concurrency()),
                                         std::max<int64_t>(1, (out.size() - codeat) >> 17));
            n = std::min<int64_t>(n, std::max<size_t>(1, fns.size()));
            vector<size_t> order(fns.size());
            for (size_t i = 0; i < order.size(); i++) order[i] = i;
            stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return fns[a].second - fns[a].first > fns[b].second - fns[b].first;
            });
            vector<size_t> load(n);
            vector<vector<size_t>> units(n);
            for (auto i : order) {
                auto u = std::min_element(load.begin(), load.end()) - load.begin();
                load[u] += fns[i].second - fns[i].first;
                units[u].push_back(i);
            }
            auto base = outfile.size() > 2 && outfile.compare(outfile.size() - 2, 2, ".c") == 0
                        ? outfile.substr(0, outfile.size() - 2) : outfile;
            auto header = cat(base, "_decls.h");
            auto headerfile = header.substr(header.find_last_of("/\\") + 1);
            write(header, cat("#pragma once\n#define GS_SPLIT 1\n", out.substr(0, codeat)));
            for (int64_t u = 0; u < n; u++) {
                // The first file defines the program's globals, which the others declare.
                string text = cat(u ? "" : "#define GS_SPLIT_OWNER 1\n", "#include \"", headerfile,
                                  "\"\n");
                sort(units[u].begin(), units[u].end());
                for (auto i : units[u])
                    text.append(out, fns[i].first, fns[i].second - fns[i].first);
                auto path = u ? cat(base, "_", u, ".c") : outfile;
                write(path, text);
                cfiles.push_back(path);
            }
            fprintf(msgs, "wrote %s and %d C file(s) from %s (%d bytes)\n", header.c_str(), (int)n,
                    outfile.c_str(), (int)out.size());
        } else if (!outfile.empty() && !nocgen) {
            auto out = assemble(!standalone);
            write(outfile, out);
            cfiles.push_back(outfile);
            fprintf(msgs, "wrote %s (%d bytes)\n", outfile.c_str(), (int)out.size());
        }
        if (!headername.empty()) {
            auto header = cg.ExportHeader();
            auto f = fopen(headername.c_str(), "wb");
            if (!f) throw CompileError { cat("cannot write export header: ", headername) };
            fwrite(header.data(), 1, header.size(), f);
            fclose(f);
            fprintf(msgs, "wrote %s (%d bytes)\n", headername.c_str(), (int)header.size());
        }
        if (jit && !nocgen) {
            // TinyCC's in-memory runner rejects a thread-local section, and
            // the runtime keeps each worker's data stacks in one.
            if (cg.usesthreads)
                throw CompileError { "JIT mode does not support threads yet (TinyCC cannot "
                                     "place thread-local storage in an in-memory run); "
                                     "compile with -o and a C compiler instead" };
            program = assemble(false);
        }
        return 0;
    };
    auto run = [&](const string &file) -> int {
        filename = file;
        outfile = outname;
        if (multitest && !outfile.empty()) {
            auto base = file.substr(file.find_last_of("/\\") + 1);
            auto stem = base.substr(0, base.find_last_of('.'));
            for (auto pos = outfile.find('%'); pos != string::npos;
                 pos = outfile.find('%', pos + stem.size()))
                outfile.replace(pos, 1, stem);
        }
        includes = includenames;
        if (verb == "build") {
            builddir = ProgramBuildDir(file);
            std::error_code ec;
            filesystem::create_directories(filesystem::path(builddir), ec);
            if (ec) {
                fprintf(stderr, "cannot create the build directory %s\n", builddir.c_str());
                return 1;
            }
            outfile = cat(builddir, "/", PathStem(file), ".c");
            // The C is written far from where the command was given, so a
            // header named relative to here is named relative to nowhere
            // else: absolute, and the C names it relative to itself.
            for (auto &inc : includes)
                if (FileExists(inc)) inc = AbsolutePath(inc);
        }
        program.clear();
        astp.reset();
        nativepkgs.clear();
        try {
            // The compile has a thread of its own. The program a JIT run
            // starts runs back on this one, the main thread, which a window
            // on macOS has to be made on, and whose stack the link reserves
            // as large as the compile thread's where the platform lets it
            // (CMakeLists.txt).
            auto code = RunOnCompilerStack(compile);
            if (code) return code;
            if (verb == "build") {
                if (nocgen) return 0;
                // The native parts first, so that the link can take their
                // response files: a builtin package's from the compiler's
                // build, the others' built now.
                Toolchain tc;
                string why;
                vector<string> rsps;
                if (!nativepkgs.empty()) {
                    if (!FindToolchain(buildopts.ccrequest, tc, why)) {
                        fprintf(stderr, "%s\n", why.c_str());
                        return 1;
                    }
                    Builder b { tc, buildopts, msgs };
                    for (auto pkg : nativepkgs) {
                        auto &p = astp->packages[pkg];
                        if (p.builtin >= 0) {
                            rsps.push_back(BuiltinPackageLinkFile(builtin_packages[p.builtin],
                                                                  tc.style, DirOf(exe)));
                            continue;
                        }
                        NativeBuild nb;
                        if (!BuildNativePart(*astp, pkg, filename, b, false, nb)) return 1;
                        rsps.push_back(nb.rsp);
                    }
                }
                return BuildExecutable(filename, cfiles, builddir, buildopts, DirOf(exe), msgs,
                                       rsps, astp->packages[0].output);
            }
            if (program.empty()) return code;
            // The native parts the program reaches hand the run their
            // functions by name: a builtin package's from the table compiled
            // in with it, the others' from libraries built now and loaded.
            vector<pair<string, const void *>> symbols;
            Toolchain tc;
            auto havetc = false;
            BuildOptions jitopts;
            jitopts.ccrequest = ccrequest;
            jitopts.verbose = verbose;
            for (auto pkg : nativepkgs) {
                auto &p = astp->packages[pkg];
                if (p.builtin >= 0) {
                    if (auto table = builtin_packages[p.builtin].symbols)
                        for (auto e = table(); e->name; e++) symbols.emplace_back(e->name, e->addr);
                    continue;
                }
                string why;
                if (!havetc && !FindToolchain(ccrequest, tc, why)) {
                    fprintf(stderr, "%s\n", why.c_str());
                    return 1;
                }
                havetc = true;
                Builder b { tc, jitopts, msgs };
                NativeBuild nb;
                if (!BuildNativePart(*astp, pkg, filename, b, true, nb)) return 1;
                string err;
                if (!LoadSharedSymbols(nb.jitlib, symbols, err)) {
                    fprintf(stderr, "cannot load the package %s's library: %s\n", p.name.c_str(),
                            err.c_str());
                    return 1;
                }
            }
            // The program shares this process, so its exit code becomes ours
            // and whatever it wrote is already on the same streams.
            fflush(msgs);
            return RunJit(program, JitLibPath(DirOf(exe)), filename, progargs, symbols);
        } catch (CompileError &e) {
            fprintf(stderr, "%s\n", e.msg.c_str());
            return 1;
        }
    };
    if (!multitest) return run(files[0]);
    // Each file has an Ast and a compile thread of its own, which is all the
    // state a compile has. The mark goes after a line break of its own, so a
    // reader drops the one before it and gets what the file printed exactly.
    // Whatever is not a CompileError ends the process, as it would a run on
    // the file alone; a reader then has no mark for that file or any after.
    for (auto &file : files) {
        auto code = run(file);
        for (auto f : { stdout, stderr }) {
            fflush(f);
            fprintf(f, "\n%s %d %s\n", MULTIMARK, code, file.c_str());
            fflush(f);
        }
    }
    return 0;
}

}  // namespace goose

// The thread the compile runs on. The system headers come after the whole
// compiler, so that none of their macros reach it.
#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <process.h>
#else
    #include <pthread.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

namespace goose {

// The running executable's path with links resolved, so that what is found
// relative to the compiler (the standard library, the runtime sources) is found
// however it was started: by a path, through PATH or through a link to it.
// argv[0] where the system cannot say.
string ExePath(const char *argv0) {
    #ifdef _WIN32
        char path[4096];
        auto n = GetModuleFileNameA(nullptr, path, sizeof path);
        if (n && n < sizeof path) return string(path, n);
    #elif defined(__linux__)
        char path[4096];
        auto n = readlink("/proc/self/exe", path, sizeof path);
        if (n > 0 && (size_t)n < sizeof path) return string(path, (size_t)n);
    #elif defined(__APPLE__)
        char path[4096];
        uint32_t size = sizeof path;
        if (_NSGetExecutablePath(path, &size) == 0) {
            if (auto resolved = realpath(path, nullptr)) {
                string s = resolved;
                free(resolved);
                return s;
            }
        }
    #endif
    return argv0;
}

// The compile's native stack: address space reserved up front, of which only
// what a program's depth reaches is ever committed. The typechecker stops at
// stackfloor, STACKHEADROOM short of the end, which leaves room for the frames
// a body nests below its last check and for reporting the error.
constexpr size_t COMPILERSTACK = (size_t)64 << 20;
constexpr size_t STACKHEADROOM = (size_t)4 << 20;

struct CompilerThread {
    const function<int()> &fn;
    int code = 0;
    exception_ptr failure;

    void Run() {
        stackfloor = StackPointer() - (COMPILERSTACK - STACKHEADROOM);
        try {
            code = fn();
        } catch (...) {
            failure = current_exception();
        }
    }
};

#ifdef _WIN32
static unsigned __stdcall CompilerThreadMain(void *t) {
    ((CompilerThread *)t)->Run();
    return 0;
}
#else
static void *CompilerThreadMain(void *t) {
    ((CompilerThread *)t)->Run();
    return nullptr;
}
#endif

// Runs fn on a thread with a COMPILERSTACK stack, and returns or throws here
// what it returned or threw. Where no such thread can be had, fn runs on this
// one, and nothing checks its depth.
int RunOnCompilerStack(const function<int()> &fn) {
    CompilerThread t { fn };
    #ifdef _WIN32
        // Without the flag, the size would be committed rather than reserved.
        auto h = (HANDLE)_beginthreadex(nullptr, (unsigned)COMPILERSTACK, CompilerThreadMain,
                                        &t, STACK_SIZE_PARAM_IS_A_RESERVATION, nullptr);
        if (!h) return fn();
        WaitForSingleObject(h, INFINITE);
        CloseHandle(h);
    #else
        pthread_attr_t attr;
        pthread_t th;
        if (pthread_attr_init(&attr)) return fn();
        auto failed = pthread_attr_setstacksize(&attr, COMPILERSTACK) ||
                      pthread_create(&th, &attr, CompilerThreadMain, &t);
        pthread_attr_destroy(&attr);
        if (failed) return fn();
        pthread_join(th, nullptr);
    #endif
    if (t.failure) rethrow_exception(t.failure);
    return t.code;
}

}  // namespace goose

int main(int argc, char **argv) {
    #ifdef _MSC_VER
        // Asserts and aborts go to stderr and exit, never a dialog, so failing
        // debug runs terminate cleanly under test automation.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    #endif
    return goose::Main(argc, argv);
}
