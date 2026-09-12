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
#include "typecheck_cycles.h"
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
#include "codegen_nodes.h"
#include "runtime_inline.h"
#include "jit.h"

namespace goose {

string DirOf(const string &path) {
    auto pos = path.find_last_of("/\\");
    return pos == string::npos ? "" : path.substr(0, pos + 1);
}

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
static string RelativeTo(const string &path, const string &dir) {
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
vector<string> StdlibDirs(const string &stdlibdir, const string &argv0) {
    vector<string> dirs;
    if (!stdlibdir.empty()) dirs.push_back(cat(stdlibdir, "/"));
    if (auto env = getenv("GOOSE_STDLIB")) dirs.push_back(cat(env, "/"));
    auto exedir = DirOf(argv0);
    dirs.push_back(cat(exedir, "stdlib/"));
    dirs.push_back(cat(exedir, "../stdlib/"));
    dirs.push_back(cat(exedir, "../../stdlib/"));
    dirs.push_back(cat(exedir, "../../../stdlib/"));
    dirs.push_back("stdlib/");
    return dirs;
}

// Parses a root file and, transitively, everything it imports (each file once).
// `import a.b;` resolves relative to the root file's directory, then in the
// standard library; `import .a.b;` relative to the importing file's.
void ParseProgram(Ast &ast, const string &rootpath, const vector<string> &stdlibdirs) {
    auto rootdir = DirOf(rootpath);
    vector<string> queue = { rootpath };
    set<string> loaded = { rootpath };
    map<string, int> fileof;                 // Resolved path -> index in ast.sources.
    vector<vector<string>> importsof;        // Per source, the paths it imports.
    while (!queue.empty()) {
        auto path = queue.back();
        queue.pop_back();
        auto contents = make_unique<string>();
        if (!LoadFile(path, *contents))
            throw CompileError { cat("cannot open file: ", path) };
        auto fileidx = (int)ast.sources.size();
        ast.sources.emplace_back(path, std::move(contents));
        fileof[path] = fileidx;
        importsof.emplace_back();
        auto &[filename, source] = ast.sources.back();
        Parser parser(ast, filename, source->c_str(), fileidx);
        parser.ParseTop();
        for (auto &imp : parser.imports) {
            auto imppath = cat(imp.relative ? DirOf(path) : rootdir, imp.path, ".goose");
            if (!imp.relative && !FileExists(imppath)) {
                for (auto &dir : stdlibdirs) {
                    auto cand = cat(dir, imp.path, ".goose");
                    if (FileExists(cand)) { imppath = cand; break; }
                }
            }
            importsof[fileidx].push_back(imppath);
            if (loaded.insert(imppath).second) queue.push_back(imppath);
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

void DumpTokens(const string &path) {
    string contents;
    if (!LoadFile(path, contents)) throw CompileError { cat("cannot open file: ", path) };
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

// The runtime C sources embedded into the compiler (runtime_inline.h) and
// prepended to every generated file, in order.
static const char *runtimefiles[] = { "runtime.h", "runtime_threads.h" , "runtime_os.h" };

// Locates the src/runtime/ directory (only needed by --gen-runtime-header):
// an explicit env override, next to the executable, or relative to it in the
// source tree layout.
string FindRuntimeDir(const char *argv0) {
    vector<string> cands;
    if (auto env = getenv("GOOSE_RUNTIME")) cands.push_back(cat(env, "/"));
    auto exedir = DirOf(argv0);
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
void GenRuntimeHeader(const char *argv0) {
    auto dir = FindRuntimeDir(argv0);
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

int Main(int argc, char **argv) {
    string filename, outfile, stdlibdir;
    auto dump = false, tokens = false, parseonly = false, specs = false, nocgen = false;
    auto nobce = false, bcetest = false, bcelines = false, norfcheck = false;
    auto forcejit = false;
    auto optlevel = 1;
    vector<string> cdefines, progargs, includes;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        // Everything past `--` belongs to the program being run, not here.
        if (arg == "--") {
            for (int j = i + 1; j < argc; j++) progargs.push_back(argv[j]);
            break;
        }
        if (arg == "--dump") dump = true;
        else if (arg == "--tokens") tokens = true;
        else if (arg == "--parse") parseonly = true;
        else if (arg == "--specs") specs = true;
        else if (arg == "--check") nocgen = true;
        else if (arg == "--no-bce") nobce = true;
        else if (arg == "--bce-test") bcetest = true;
        else if (arg == "--bce-lines") bcelines = true;
        // Unsound; a measurement aid only (see CodeGen::norfcheck).
        else if (arg == "--unsafe-no-rf-check") norfcheck = true;
        else if (arg == "--jit") forcejit = true;
        else if (arg == "--gen-runtime-header") { GenRuntimeHeader(argv[0]); return 0; }
        else if (arg == "-O0") optlevel = 0;
        else if (arg == "-O1") optlevel = 1;
        else if (arg == "-O2") optlevel = 2;
        else if (arg == "-o" && i + 1 < argc) outfile = argv[++i];
        else if (arg == "--include" && i + 1 < argc) includes.push_back(argv[++i]);
        else if (arg == "--stdlib" && i + 1 < argc) stdlibdir = argv[++i];
        // A -D lands in the generated C itself rather than on some backend's
        // command line, so a JIT run and a compiled one see the same source.
        else if (arg == "-D" && i + 1 < argc) cdefines.push_back(argv[++i]);
        else if (arg.rfind("-D", 0) == 0 && arg.size() > 2) cdefines.push_back(arg.substr(2));
        else if (!arg.empty() && arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return 1;
        }
        else if (filename.empty()) filename = arg;
        else { fprintf(stderr, "multiple input files given\n"); return 1; }
    }
    if (filename.empty()) {
        fprintf(stderr, "usage: goose [--dump] [--parse] [--tokens] [--specs] [--check] "
                        "[--no-bce] [--bce-test] [--bce-lines] [--unsafe-no-rf-check] [-O0|-O1|-O2] "
                        "[-o out.c] [--jit] [-DNAME=VALUE]... [--include header.h]... [--stdlib dir] "
                        "file.goose [-- program args...] | --gen-runtime-header\n");
        fprintf(stderr, "without -o the program is compiled and run in this process%s.\n",
                have_jit ? " by TinyCC" : " -- unavailable in this build, so the .c is written");
        return 1;
    }
    // With no output file the program is compiled into this process and run,
    // which is what --jit asks for explicitly. A build without the backend
    // keeps writing the .c next to the source instead.
    auto jit = forcejit || (outfile.empty() && have_jit);
    if (outfile.empty() && !jit) {
        auto dot = filename.find_last_of('.');
        outfile = cat(dot == string::npos ? filename : filename.substr(0, dot), ".c");
    }
    // The program shares stdout in JIT mode; progress goes to stderr.
    auto msgs = jit ? stderr : stdout;
    try {
        if (tokens) {
            DumpTokens(filename);
            return 0;
        }
        Ast ast;
        ParseProgram(ast, filename, StdlibDirs(stdlibdir, argv[0]));
        if (dump) {
            // Dump is parse-level output: no name resolution or typecheck,
            // so parse-only test files can roundtrip, and every name shows
            // as written.
            string s;
            ast.Dump(s);
            fputs(s.c_str(), stdout);
            return 0;
        }
        ResolveTypeNames(ast);
        if (parseonly) {
            fprintf(msgs, "parsed ok: %d top-level declarations, %d file(s)\n",
                    (int)ast.topdecls.size(), (int)ast.sources.size());
            return 0;
        }
        TypeCheckProgram(ast);
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
        if (nocgen) return 0;
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
        // so codegen splices it in after them rather than up front.
        string_view runtime_os_text;
        for (auto &rf : runtime_files)
            if (string_view(rf.name) == "runtime_os.h") runtime_os_text = rf.text;
        CodeGen cg(ast, runtime_os_text, includes, norfcheck);
        // Assemble: compiler-set feature defines, the embedded runtime, then
        // the generated program.
        string out = cat("/* Generated by the Goose compiler from ", filename,
                         ". Do not edit.\n"
                         "   Names from the program carry a _g suffix, which keeps them clear of\n"
                         "   the C keywords, of the runtime's gs_ names and of whatever this\n"
                         "   platform's headers declare; a namespaced name ns::x is ns_x_g followed\n"
                         "   by the namespace's length. An --include header names them that way. */\n\n",
                         cg.predefs);
        // -D goes into the source rather than onto a backend's command line,
        // so both backends compile the same text.
        for (auto &d : cdefines) {
            auto eq = d.find('=');
            Append(out, "#define ", eq == string::npos ? d : d.substr(0, eq), " ",
                   eq == string::npos ? string("1") : d.substr(eq + 1), "\n");
        }
        for (auto &rf : runtime_files) {
            if (string_view(rf.name) == "runtime_os.h") continue;
            Append(out, "/* ==== ", rf.name, " ==== */\n", rf.text, "\n");
        }
        out += cg.result;
        if (!outfile.empty()) {
            auto f = fopen(outfile.c_str(), "wb");
            if (!f) throw CompileError { cat("cannot write output file: ", outfile) };
            fwrite(out.data(), 1, out.size(), f);
            fclose(f);
            fprintf(msgs, "wrote %s (%d bytes)\n", outfile.c_str(), (int)out.size());
        }
        if (jit) {
            // TinyCC's in-memory runner rejects a thread-local section, and
            // the runtime keeps each worker's data stacks in one.
            if (cg.usesthreads)
                throw CompileError { "JIT mode does not support threads yet (TinyCC cannot "
                                     "place thread-local storage in an in-memory run); "
                                     "compile with -o and a C compiler instead" };
            // The program shares this process, so its exit code becomes ours
            // and whatever it wrote is already on the same streams.
            fflush(msgs);
            return RunJit(out, JitLibPath(DirOf(argv[0])), filename, progargs);
        }
    } catch (CompileError &e) {
        fprintf(stderr, "%s\n", e.msg.c_str());
        return 1;
    }
    return 0;
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
