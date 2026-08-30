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
#include "typecheck.h"
#include "optimize.h"
#include "codegen.h"

namespace goose {

string DirOf(const string &path) {
    auto pos = path.find_last_of("/\\");
    return pos == string::npos ? "" : path.substr(0, pos + 1);
}

// Parses a root file and, transitively, everything it imports (each file once).
// `import a.b;` resolves relative to the root file's directory, `import .a.b;`
// relative to the importing file's.
void ParseProgram(Ast &ast, const string &rootpath) {
    auto rootdir = DirOf(rootpath);
    vector<string> queue = { rootpath };
    set<string> loaded = { rootpath };
    while (!queue.empty()) {
        auto path = queue.back();
        queue.pop_back();
        auto contents = make_unique<string>();
        if (!LoadFile(path, *contents))
            throw CompileError { cat("cannot open file: ", path) };
        auto fileidx = (int)ast.sources.size();
        ast.sources.emplace_back(path, std::move(contents));
        auto &[filename, source] = ast.sources.back();
        Parser parser(ast, filename, source->c_str(), fileidx);
        parser.ParseTop();
        for (auto &imp : parser.imports) {
            auto imppath = cat(imp.relative ? DirOf(path) : rootdir, imp.path, ".goose");
            if (loaded.insert(imppath).second) queue.push_back(imppath);
        }
    }
    ResolveTypeNames(ast);
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

// The runtime C sources prepended to every generated file, in order.
static const char *runtimefiles[] = { "runtime.h", "runtime_threads.h" };

// Locates the src/runtime/ directory: an explicit env override, next to the
// executable, or relative to it in the source tree layout.
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

int Main(int argc, char **argv) {
    string filename, outfile;
    auto dump = false, tokens = false, parseonly = false, specs = false, nocgen = false;
    auto optlevel = 1;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--dump") dump = true;
        else if (arg == "--tokens") tokens = true;
        else if (arg == "--parse") parseonly = true;
        else if (arg == "--specs") specs = true;
        else if (arg == "--check") nocgen = true;
        else if (arg == "-O0") optlevel = 0;
        else if (arg == "-O1") optlevel = 1;
        else if (arg == "-O2") optlevel = 2;
        else if (arg == "-o" && i + 1 < argc) outfile = argv[++i];
        else if (!arg.empty() && arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return 1;
        }
        else if (filename.empty()) filename = arg;
        else { fprintf(stderr, "multiple input files given\n"); return 1; }
    }
    if (filename.empty()) {
        fprintf(stderr, "usage: goose [--dump] [--parse] [--tokens] [--specs] [--check] "
                        "[-O0|-O1|-O2] [-o out.c] file.goose\n");
        return 1;
    }
    try {
        if (tokens) {
            DumpTokens(filename);
            return 0;
        }
        Ast ast;
        ParseProgram(ast, filename);
        if (dump) {
            // Dump is parse-level output: no typecheck, so parse-only test
            // files can roundtrip.
            string s;
            ast.Dump(s);
            fputs(s.c_str(), stdout);
            return 0;
        }
        if (parseonly) {
            printf("parsed ok: %d top-level declarations, %d file(s)\n",
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
        printf("typechecked ok: %d specialization(s), %d struct/%d enum instance(s); "
               "optimized -O%d: %d inlined, %d folded, %d propagated\n",
               (int)ast.fnspecs.size(), (int)ast.structinsts.size(),
               (int)ast.enuminsts.size(), optlevel, opt.inlined, opt.folded,
               opt.propagated);
        if (nocgen) return 0;
        CodeGen cg(ast);
        // Assemble: compiler-set feature defines, the runtime verbatim, then
        // the generated program.
        auto rtdir = FindRuntimeDir(argv[0]);
        string out = cat("/* Generated by the Goose compiler from ", filename,
                         ". Do not edit. */\n\n", cg.predefs);
        for (auto rf : runtimefiles) {
            string src;
            if (!LoadFile(cat(rtdir, rf), src))
                throw CompileError { cat("cannot read runtime file: ", rtdir, rf) };
            Append(out, "/* ==== ", rf, " ==== */\n", src, "\n");
        }
        out += cg.result;
        if (outfile.empty()) {
            auto dot = filename.find_last_of('.');
            outfile = cat(dot == string::npos ? filename : filename.substr(0, dot), ".c");
        }
        auto f = fopen(outfile.c_str(), "wb");
        if (!f) throw CompileError { cat("cannot write output file: ", outfile) };
        fwrite(out.data(), 1, out.size(), f);
        fclose(f);
        printf("wrote %s (%d bytes)\n", outfile.c_str(), (int)out.size());
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
