// Goose compiler — a package's native part (docs/design/packages.md §5): the
// C its sidecar names, built once per toolchain into what an AOT program
// links (its objects and system libraries, in a response file) and the
// shared library a JIT run loads, which exports one table of the functions
// the package's extern fns name (§5.3). The program itself is package 0 and
// may have a native part of its own.
#pragma once

namespace goose {

// The packages whose native parts a program needs, in link order: its own
// (package 0) when it has one, and those an extern fn it calls (`used`,
// codegen's) was declared in, a package before the ones it requires.
inline vector<int> NativePackagesUsed(const Ast &ast, const vector<FnSpec *> &used) {
    set<int> needed;
    if (ast.packages[0].HasNative()) needed.insert(0);
    for (auto sp : used) {
        auto pkg = ast.filepkg[sp->sf->line.fileidx];
        if (ast.packages[pkg].HasNative()) needed.insert(pkg);
    }
    // A package's depth is one more than the deepest it requires; deeper
    // links first. A cycle of requirements gets whatever depth the walk
    // reaches, which is all an order can mean there.
    map<int, int> depth;
    std::function<int(int)> depthof = [&](int pkg) -> int {
        auto it = depth.find(pkg);
        if (it != depth.end()) return it->second;
        depth[pkg] = 0;
        auto d = 0;
        for (auto req : ast.packages[pkg].required) d = std::max(d, depthof(req) + 1);
        depth[pkg] = d;
        return d;
    };
    vector<int> order(needed.begin(), needed.end());
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return depthof(a) > depthof(b); });
    return order;
}

struct NativeBuild {
    string dir;       // Where it was built.
    string rsp;       // The response file an AOT link takes.
    string jitlib;    // The shared library a JIT run loads.
};

// Where the package `pkg`'s native part is built for the toolchain `tc`:
// under the program's build directory for the program itself, under
// GOOSE_HOME/build/pkg for a package, by its checkout or its path.
inline string NativeBuildDir(const Ast &ast, int pkg, const string &rootfile, const Toolchain &tc) {
    auto &p = ast.packages[pkg];
    if (pkg == 0) return cat(ProgramBuildDir(rootfile), "/native/", tc.id);
    if (!p.cachekey.empty()) return cat(GooseHome(), "/build/pkg/", p.cachekey, "/", tc.id);
    return cat(GooseHome(), "/build/pkg/local/", Hex8(p.root), "/", tc.id);
}

// The C symbols a file's extern fns name: `extern "sym" fn f` gives sym,
// `extern fn f` or `extern fn ns::f` gives the leaf name (§7.10). Read by
// the lexer alone, since this is asked of files the program never parsed.
inline void ExternNamesIn(const string &path, const string &text, set<string> &names) {
    Lexer lex(path, text.c_str());
    while (lex.tok != T_EOF) {
        if (lex.tok != T_EXTERN) {
            lex.Next();
            continue;
        }
        lex.Next();
        string cname;
        if (lex.tok == T_STRLIT) {
            cname = lex.sval;
            lex.Next();
        }
        if (lex.tok != T_FN) continue;
        lex.Next();
        if (lex.tok == T_IDENT) {
            string leaf(lex.attr);
            lex.Next();
            if (lex.tok == T_COLONCOLON) {
                lex.Next();
                if (lex.tok == T_IDENT) leaf = string(lex.attr);
            }
            if (cname.empty()) cname = leaf;
        }
        if (!cname.empty()) names.insert(cname);
    }
}

// Every C symbol the package's extern fns name: for the program, those of
// the files it parsed; for a library, those of every .goose file under its
// root, so that the library built once serves whatever modules a program
// imports from it.
inline vector<string> PackageExternNames(const Ast &ast, int pkg) {
    set<string> names;
    if (pkg == 0) {
        for (size_t i = 0; i < ast.sources.size(); i++)
            if (ast.filepkg[i] == pkg) {
                try {
                    ExternNamesIn(ast.sources[i].first, *ast.sources[i].second, names);
                } catch (CompileError &) {}
            }
    } else {
        std::error_code ec;
        for (auto &e : filesystem::recursive_directory_iterator(filesystem::path(ast.packages[pkg].root), ec)) {
            if (!e.is_regular_file() || e.path().extension() != ".goose") continue;
            auto path = ForwardSlashes(e.path().string());
            string text;
            if (!LoadSource(path, text)) continue;
            try {
                ExternNamesIn(path, text, names);
            } catch (CompileError &) {}   // A file that does not lex has no externs to offer.
        }
    }
    return vector<string>(names.begin(), names.end());
}

// goose_symbols.c: the one exported function of a package's JIT library,
// handing out the table of its functions by name. Taking a function's
// address needs no signature, so each is declared `void f(void)`.
inline string SymbolTableSource(const string &pkgname, const vector<string> &names) {
    string s = cat("/* Generated by the Goose compiler for the package ", pkgname,
                   ": the C functions its\n   extern fns name, which a JIT run looks up through "
                   "goose_symbols. Do not edit. */\n\n"
                   "typedef struct { const char *name; const void *addr; } goose_symbol;\n\n");
    for (auto &n : names) Append(s, "void ", n, "(void);\n");
    Append(s, "\n#ifdef _WIN32\n__declspec(dllexport)\n#endif\n"
              "const goose_symbol *goose_symbols(void) {\n"
              "    static const goose_symbol table[] = {\n");
    for (auto &n : names) Append(s, "        { \"", n, "\", (const void *)&", n, " },\n");
    s += "        { 0, 0 }\n    };\n    return table;\n}\n";
    return s;
}

// --- CMake packages (§5.2) ----------------------------------------------------------

// A link command fragment's words, as CMake's file API gives them: several
// libraries may share one fragment, and a quoted path holds its spaces.
inline vector<string> SplitFragment(const string &f) {
    vector<string> out;
    string cur;
    auto quoted = false;
    for (auto c : f) {
        if (c == '"') { quoted = !quoted; continue; }
        if (!quoted && (c == ' ' || c == '\t')) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
            continue;
        }
        cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// The link line of `target` in the CMake build at `builddir`, from the file
// API's codemodel reply (the query is written before configuring): the
// libraries, library paths and framework paths, in order, paths relative to
// the build directory made absolute. For the probe executable linked to a
// package's target, that is the package's whole link closure.
inline vector<string> CMakeLinkClosure(const string &builddir, const string &target) {
    auto replydir = cat(builddir, "/.cmake/api/v1/reply");
    string indexfile;
    std::error_code ec;
    for (auto &e : filesystem::directory_iterator(filesystem::path(replydir), ec)) {
        auto name = e.path().filename().string();
        if (name.rfind("index-", 0) == 0 && name > indexfile) indexfile = name;   // The newest.
    }
    if (indexfile.empty())
        throw CompileError { cat("CMake wrote no file API reply under ", replydir) };
    auto load = [&](const string &name) {
        string text;
        if (name.empty() || !LoadFile(cat(replydir, "/", name), text))
            throw CompileError { cat("cannot read CMake's file API reply ", replydir, "/", name) };
        return ParseJson(text);
    };
    auto index = load(indexfile);
    auto reply = index.Get("reply");
    auto codemodelref = reply ? reply->Get("codemodel-v2") : nullptr;
    auto codemodel = load(codemodelref ? codemodelref->Str("jsonFile") : string());
    const Json *config = nullptr;
    for (auto &c : codemodel.Arr("configurations"))
        if (!config || c.Str("name") == "Release") config = &c;
    if (!config) throw CompileError { "CMake's codemodel reply names no configuration" };
    string targetfile;
    for (auto &t : config->Arr("targets"))
        if (t.Str("name") == target) targetfile = t.Str("jsonFile");
    auto tj = load(targetfile);
    vector<string> items;
    auto link = tj.Get("link");
    if (!link) throw CompileError { cat("CMake's codemodel reply has no link line for ", target) };
    for (auto &frag : link->Arr("commandFragments")) {
        auto role = frag.Str("role");
        if (role != "libraries" && role != "libraryPath" && role != "frameworkPath") continue;
        for (auto &tok : SplitFragment(frag.Str("fragment"))) {
            auto abs = cat(builddir, "/", tok);
            items.push_back(tok.find_first_of("/\\") != string::npos && PathExists(abs) ? abs : tok);
        }
    }
    return items;
}

// Builds a CMake package's native part: a wrapper project around the
// package's own, with the JIT library as a shared target linked to the
// package's and a probe executable whose link line is the package's link
// closure. Configured once (CMake reconfigures by itself as the project
// changes), built with the discovered compilers through Ninja where there
// is one, else the platform's generator.
inline bool BuildCMakePart(const Ast &ast, int pkg, const Builder &b, bool wantjit,
                           NativeBuild &out) {
    auto &p = ast.packages[pkg];
    auto cmake = CMakePath();
    if (cmake.empty())
        throw CompileError { cat("cannot find `cmake` on PATH, needed to build the C part of the "
                                 "package ", p.name, " (target ", p.cmake_target, " of ", p.root,
                                 "CMakeLists.txt). Install CMake 3.20 or later.") };
    #ifdef _WIN32
        // The MSVC environment (rc, link, the headers), for whichever
        // compiler builds it.
        ImportVcvars(false);
    #endif
    auto wrapper = cat(out.dir, "/wrapper");
    auto builddir = cat(out.dir, "/cmake");
    auto symsrc = cat(out.dir, "/goose_symbols.c");
    auto probesrc = cat(out.dir, "/goose_linkprobe.c");
    auto jitname = cat("goose_jit_", p.name);
    auto root = p.root.substr(0, p.root.size() - 1);
    auto lists = cat(
        "# Generated by the Goose compiler for the package ", p.name, ". Do not edit.\n"
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(goose_pkg_", p.name, " C)\n"
        "set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded\")\n"
        "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n"
        "add_subdirectory(\"", root, "\" pkg EXCLUDE_FROM_ALL)\n"
        "# The JIT library: the symbol table pulls what it names out of the target.\n"
        "add_library(", jitname, " SHARED \"", symsrc, "\")\n"
        "target_link_libraries(", jitname, " PRIVATE ", p.cmake_target, ")\n"
        "set_target_properties(", jitname, " PROPERTIES PREFIX \"\" OUTPUT_NAME \"", jitname, "\"\n"
        "    LIBRARY_OUTPUT_DIRECTORY \"", out.dir, "\" RUNTIME_OUTPUT_DIRECTORY \"", out.dir, "\"\n"
        "    LIBRARY_OUTPUT_DIRECTORY_RELEASE \"", out.dir, "\" RUNTIME_OUTPUT_DIRECTORY_RELEASE \"",
        out.dir, "\")\n"
        "# A probe whose link line is the package's whole link closure.\n"
        "add_executable(goose_linkprobe \"", probesrc, "\")\n"
        "target_link_libraries(goose_linkprobe PRIVATE ", p.cmake_target, ")\n");
    if (!b.opts.dryrun) {
        if (!WriteFileMakingDirs(symsrc, SymbolTableSource(p.name, PackageExternNames(ast, pkg))) ||
            !WriteFileMakingDirs(probesrc, "/* Its link line is the package's link closure. */\n"
                                           "int main(void) { return 0; }\n"))
            throw CompileError { cat("cannot write into ", out.dir) };
        string old;
        LoadFile(cat(wrapper, "/CMakeLists.txt"), old);
        if (old != lists && !WriteFileMakingDirs(cat(wrapper, "/CMakeLists.txt"), lists))
            throw CompileError { cat("cannot write into ", wrapper) };
        WriteFileMakingDirs(cat(builddir, "/.cmake/api/v1/query/codemodel-v2"), "");
    }
    if (b.opts.dryrun || !PathExists(cat(builddir, "/CMakeCache.txt"))) {
        vector<string> argv = { cmake, "-S", wrapper, "-B", builddir, "-DCMAKE_BUILD_TYPE=Release" };
        auto ninja = NinjaPath();
        if (!ninja.empty()) {
            argv.push_back("-G");
            argv.push_back("Ninja");
            argv.push_back(cat("-DCMAKE_MAKE_PROGRAM=", ninja));
            argv.push_back(cat("-DCMAKE_C_COMPILER=", b.tc.cc));
            argv.push_back(cat("-DCMAKE_CXX_COMPILER=", b.tc.cxx.empty() ? b.tc.cc : b.tc.cxx));
        } else {
            #ifdef _WIN32
                // The Visual Studio generator, with its own cl, or clang-cl
                // as a toolset.
                if (b.tc.name == "clang-cl") {
                    argv.push_back("-T");
                    argv.push_back("ClangCL");
                }
            #else
                argv.push_back(cat("-DCMAKE_C_COMPILER=", b.tc.cc));
                argv.push_back(cat("-DCMAKE_CXX_COMPILER=", b.tc.cxx.empty() ? b.tc.cc : b.tc.cxx));
            #endif
        }
        for (auto &o : p.cmake_options) argv.push_back(cat("-D", o));
        if (!b.Run(argv, cat("configuring the package ", p.name, " with CMake"),
                   cat(out.dir, "/cmake-configure.log")))
            return false;
    }
    vector<string> build = { cmake, "--build", builddir, "--config", "Release", "--target",
                             "goose_linkprobe" };
    if (wantjit) build.push_back(jitname);
    if (!b.Run(build, cat("building the package ", p.name, " with CMake"),
               cat(out.dir, "/cmake-build.log")))
        return false;
    if (b.opts.dryrun) return true;
    string rsp;
    for (auto &i : CMakeLinkClosure(builddir, "goose_linkprobe"))
        Append(rsp, i.find(' ') != string::npos ? cat("\"", i, "\"") : i, "\n");
    if (!WriteFileMakingDirs(out.rsp, rsp)) throw CompileError { cat("cannot write ", out.rsp) };
    return true;
}

inline int64_t NewestMtime(const vector<string> &paths) {
    int64_t newest = 0;
    for (auto &p : paths) {
        std::error_code ec;
        auto t = filesystem::last_write_time(filesystem::path(p), ec);
        if (!ec) newest = std::max(newest, (int64_t)t.time_since_epoch().count());
    }
    return newest;
}

inline string ReadStamp(const string &path) {
    string s;
    LoadFile(path, s);
    return s;
}

// Builds the package's native part for `b`'s toolchain, unless the build
// there is up to date with its inputs (the sidecar, the C sources, the
// toolchain), and names what was built in `out`. The JIT library is built
// only when `wantjit`. False after a failed command, which the builder
// reports; a dry run prints the commands and reports success.
inline bool BuildNativePart(const Ast &ast, int pkg, const string &rootfile, const Builder &b,
                            bool wantjit, NativeBuild &out) {
    auto &p = ast.packages[pkg];
    auto dir = NativeBuildDir(ast, pkg, rootfile, b.tc);
    out.dir = dir;
    out.rsp = cat(dir, "/link-", b.tc.style, ".rsp");
    #ifdef _WIN32
        auto jitext = ".dll";
    #elif defined(__APPLE__)
        auto jitext = ".dylib";
    #else
        auto jitext = ".so";
    #endif
    out.jitlib = cat(dir, "/goose_jit_", p.name, jitext);
    auto inputs = p.csources;
    inputs.push_back(p.sidecar);
    string sidecartext;
    LoadFile(p.sidecar, sidecartext);
    auto stampwant = cat(b.tc.id, "\n", NewestMtime(inputs), "\n", Hex8(sidecartext), "\n",
                         wantjit ? "jit" : "aot", "\n");
    auto stampfile = cat(dir, "/stamp");
    auto have = ReadStamp(stampfile);
    // A JIT build holds everything an AOT build has, and more. A CMake
    // package from a path is handed to CMake every time, whose own build is
    // incremental and sees the sources this stamp does not; a checkout
    // never changes.
    auto fresh = have == stampwant || (!wantjit && have == cat(stampwant.substr(0, stampwant.size() - 4), "jit\n"));
    if (!p.cmake_target.empty() && p.cachekey.empty()) fresh = false;
    if (fresh && PathExists(out.rsp) && (!wantjit || PathExists(out.jitlib)) && !b.opts.dryrun)
        return true;
    std::error_code ec;
    filesystem::create_directories(filesystem::path(dir), ec);
    if (!p.cmake_target.empty()) {
        if (!BuildCMakePart(ast, pkg, b, wantjit, out)) return false;
        if (!b.opts.dryrun) WriteFileMakingDirs(stampfile, stampwant);
        return true;
    }
    // The flags the package's C compiles with: its includes, defines and
    // flags for this style; position independent where a shared library
    // needs it.
    vector<string> flags;
    auto msvc = b.tc.style == "msvc";
    for (auto &i : p.cincludes) flags.push_back(cat(msvc ? "/I" : "-I", i));
    for (auto &d : p.cdefines) flags.push_back(cat(msvc ? "/D" : "-D", d));
    for (auto &f : msvc ? p.cflags_msvc : p.cflags_cc) flags.push_back(f);
    #ifndef _WIN32
        if (!msvc) flags.push_back("-fPIC");
    #endif
    vector<string> objects;
    for (auto &src : p.csources) {
        auto obj = cat(dir, "/", PathStem(src), msvc ? ".obj" : ".o");
        if (!b.CompileObject(src, obj, cat("compiling ", src, " for the package ", p.name),
                             cat(dir, "/", PathStem(src), ".cc.log"), flags))
            return false;
        objects.push_back(obj);
    }
    vector<string> libs;
    for (auto &l : p.clibs) libs.push_back(b.LibFlag(l));
    string rsp;
    for (auto &o : objects) Append(rsp, "\"", o, "\"\n");
    for (auto &l : libs) Append(rsp, l, "\n");
    if (!b.opts.dryrun && !WriteFileMakingDirs(out.rsp, rsp))
        throw CompileError { cat("cannot write ", out.rsp) };
    if (wantjit) {
        auto symsrc = cat(dir, "/goose_symbols.c");
        auto symobj = cat(dir, "/goose_symbols", msvc ? ".obj" : ".o");
        if (!b.opts.dryrun &&
            !WriteFileMakingDirs(symsrc, SymbolTableSource(p.name, PackageExternNames(ast, pkg))))
            throw CompileError { cat("cannot write ", symsrc) };
        // Warnings off: a C library's function declared `void f(void)` here
        // is what the compiler knows as a builtin with another shape.
        vector<string> symflags = { msvc ? "/w" : "-w" };
        #ifndef _WIN32
            if (!msvc) symflags.push_back("-fPIC");
        #endif
        if (!b.CompileObject(symsrc, symobj, cat("compiling the symbol table of the package ", p.name),
                             cat(dir, "/goose_symbols.cc.log"), symflags))
            return false;
        objects.push_back(symobj);
        if (!b.LinkShared(objects, libs, out.jitlib, dir, cat(dir, "/jit.cc.log"),
                          cat("building the package ", p.name, "'s library for the JIT")))
            return false;
    }
    if (!b.opts.dryrun) WriteFileMakingDirs(stampfile, stampwant);
    return true;
}

}  // namespace goose
