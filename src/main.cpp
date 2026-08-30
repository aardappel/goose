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

int Main(int argc, char **argv) {
    string filename;
    auto dump = false, tokens = false, parseonly = false;
    for (int i = 1; i < argc; i++) {
        string arg = argv[i];
        if (arg == "--dump") dump = true;
        else if (arg == "--tokens") tokens = true;
        else if (arg == "--parse") parseonly = true;
        else if (!arg.empty() && arg[0] == '-') {
            fprintf(stderr, "unknown option: %s\n", arg.c_str());
            return 1;
        }
        else if (filename.empty()) filename = arg;
        else { fprintf(stderr, "multiple input files given\n"); return 1; }
    }
    if (filename.empty()) {
        fprintf(stderr, "usage: goose [--dump] [--parse] [--tokens] file.goose\n");
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
        } else if (parseonly) {
            printf("parsed ok: %d top-level declarations, %d file(s)\n",
                   (int)ast.topdecls.size(), (int)ast.sources.size());
        } else {
            TypeCheckProgram(ast);
            printf("typechecked ok: %d specialization(s), %d struct/%d enum instance(s)\n",
                   (int)ast.fnspecs.size(), (int)ast.structinsts.size(),
                   (int)ast.enuminsts.size());
        }
    } catch (CompileError &e) {
        fprintf(stderr, "%s\n", e.msg.c_str());
        return 1;
    }
    return 0;
}

}  // namespace goose

int main(int argc, char **argv) { return goose::Main(argc, argv); }
