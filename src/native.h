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
        // One bound from a shared library at run time comes from no package's C.
        if (!sp->sf->dylib.empty()) continue;
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

// Where the package `pkg`'s native part is built by `b`: under the
// program's build directory for the program itself, a --debug build apart,
// and under GOOSE_HOME/build/pkg for a package, by its checkout or its path.
inline string NativeBuildDir(const Ast &ast, int pkg, const string &rootfile, const Builder &b) {
    auto &p = ast.packages[pkg];
    if (pkg == 0)
        return cat(ProgramBuildDir(rootfile), "/native/", b.tc.id, b.opts.debug ? "-debug" : "");
    if (!p.cachekey.empty()) return cat(GooseHome(), "/build/pkg/", p.cachekey, "/", b.tc.id);
    return cat(GooseHome(), "/build/pkg/local/", Hex8(p.root), "/", b.tc.id);
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
        // An extern block's functions come from its shared library (§7.10),
        // not from the native part: skipped through its `}`.
        if (lex.tok == T_LCURLY) {
            auto depth = 0;
            do {
                if (lex.tok == T_LCURLY) depth++;
                else if (lex.tok == T_RCURLY) depth--;
                lex.Next();
            } while (depth > 0 && lex.tok != T_EOF);
            continue;
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

// Whether the directory `dir` holds a sidecar, which makes it a program or
// package of its own.
inline bool HasSidecar(const filesystem::path &dir) {
    std::error_code ec;
    for (filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        if (it->path().extension() == ".build" && it->is_regular_file(ec)) return true;
    return false;
}

// Every C symbol the package's extern fns name: for the program, those of
// the files it parsed; for a library, those of every .goose file under its
// root, so that the library built once serves whatever modules a program
// imports from it. That leaves out the directories its sidecar excludes and
// those below the root with a sidecar of their own, such as an example
// program with C of its own.
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
        auto &p = ast.packages[pkg];
        std::error_code ec;
        filesystem::recursive_directory_iterator it(filesystem::path(p.root), ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            auto path = ForwardSlashes(it->path().string());
            std::error_code fec;
            if (it->is_directory(fec)) {
                auto dir = path + "/";
                if (std::find(p.exclude.begin(), p.exclude.end(), dir) != p.exclude.end() ||
                    HasSidecar(it->path()))
                    it.disable_recursion_pending();
                continue;
            }
            if (it->path().extension() != ".goose" || !it->is_regular_file(fec)) continue;
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

// goose.h (src/runtime/goose.h): what C compiled on its own needs to take
// the values an extern fn passes that it cannot spell itself, a builder's
// gs_rref (spec §7.10). Every native part's C finds it in the part's build
// directory, under include/.
inline string GooseHeader() {
    for (auto &rf : runtime_files)
        if (string_view(rf.name) == "goose.h") return string(rf.text);
    return {};
}

inline string NativeIncludeDir(const NativeBuild &out) { return cat(out.dir, "/include"); }

inline void WriteGooseHeader(const NativeBuild &out) {
    auto path = cat(NativeIncludeDir(out), "/goose.h");
    if (!WriteFileIfChanged(path, GooseHeader())) throw CompileError { cat("cannot write ", path) };
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
    for (filesystem::directory_iterator it(filesystem::path(replydir), ec), end; !ec && it != end;
         it.increment(ec)) {
        auto name = it->path().filename().string();
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
// closure. Configured afresh whenever the configure command differs from
// the last one that succeeded (CMake reconfigures by itself as the project
// changes), built with the discovered compilers through Ninja where there
// is one, else the platform's generator.
inline bool BuildCMakePart(const Ast &ast, int pkg, const Builder &b, bool wantjit,
                           const vector<string> &externs, NativeBuild &out) {
    auto &p = ast.packages[pkg];
    // The package's root absolute, with forward slashes, which a quoted CMake
    // argument takes as they are: the program's own is as the command line
    // named it, "" for the working directory.
    auto root = PackageDir(p.root.empty() ? "." : p.root);
    root.pop_back();
    auto cmake = CMakePath();
    if (cmake.empty())
        throw CompileError { cat("cannot find `cmake` on PATH, needed to build the C part of the "
                                 "package ", p.name, " (target ", p.cmake_target, " of ", root,
                                 "/CMakeLists.txt). Install CMake 3.20 or later.") };
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
    auto lists = cat(
        "# Generated by the Goose compiler for the package ", p.name, ". Do not edit.\n"
        "cmake_minimum_required(VERSION 3.20)\n"
        "project(goose_pkg_", p.name, " C)\n"
        "# The languages the package's targets may use or publish, where there is\n"
        "# a compiler for them.\n"
        "include(CheckLanguage)\n"
        "check_language(CXX)\n"
        "if(CMAKE_CXX_COMPILER)\n"
        "    enable_language(CXX)\n"
        "endif()\n"
        "if(APPLE)\n"
        "    check_language(OBJC)\n"
        "    if(CMAKE_OBJC_COMPILER)\n"
        "        enable_language(OBJC)\n"
        "    endif()\n"
        "    check_language(OBJCXX)\n"
        "    if(CMAKE_OBJCXX_COMPILER)\n"
        "        enable_language(OBJCXX)\n"
        "    endif()\n"
        "endif()\n"
        "set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded\")\n"
        "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n"
        "# goose.h, for every target of the package (GooseHeader).\n"
        "include_directories(\"", NativeIncludeDir(out), "\")\n"
        "add_subdirectory(\"", root, "\" pkg EXCLUDE_FROM_ALL)\n"
        "if(NOT TARGET ", p.cmake_target, ")\n"
        "    message(FATAL_ERROR \"the package ", p.name, "'s sidecar names the CMake target ",
        p.cmake_target, ", which ", root, "/CMakeLists.txt does not define\")\n"
        "endif()\n"
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
    // What CMake's build goes by keeps its time unless it changed, so that a
    // build with nothing new compiles and relinks nothing: a JIT library
    // another process has loaded could not be written over on Windows.
    if (!b.opts.dryrun) {
        if (!WriteFileIfChanged(symsrc, SymbolTableSource(p.name, externs)) ||
            !WriteFileIfChanged(probesrc, "/* Its link line is the package's link closure. */\n"
                                          "int main(void) { return 0; }\n"))
            throw CompileError { cat("cannot write into ", out.dir) };
        if (!WriteFileIfChanged(cat(wrapper, "/CMakeLists.txt"), lists))
            throw CompileError { cat("cannot write into ", wrapper) };
        WriteFileMakingDirs(cat(builddir, "/.cmake/api/v1/query/codemodel-v2"), "");
    }
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
    // The configure command that last succeeded. Another one starts from a
    // fresh cache, as `cmake --fresh` would, so that an option taken out of
    // cmake_options does not stay in force.
    auto configured = cat(builddir, "/goose-configured.txt");
    string argvtext, have;
    for (auto &a : argv) Append(argvtext, a, "\n");
    LoadFile(configured, have);
    if (b.opts.dryrun || have != argvtext) {
        if (!b.opts.dryrun) {
            std::error_code ec;
            filesystem::remove(filesystem::path(configured), ec);
            filesystem::remove(filesystem::path(cat(builddir, "/CMakeCache.txt")), ec);
            filesystem::remove_all(filesystem::path(cat(builddir, "/CMakeFiles")), ec);
        }
        if (!b.Run(argv, cat("configuring the package ", p.name, " with CMake"),
                   cat(out.dir, "/cmake-configure.log")))
            return false;
        if (!b.opts.dryrun && !WriteFileMakingDirs(configured, argvtext))
            throw CompileError { cat("cannot write ", configured) };
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
    if (!WriteFileIfChanged(out.rsp, rsp)) throw CompileError { cat("cannot write ", out.rsp) };
    return true;
}

// What the files at `paths` are now, as far as a rebuild cares: a hash of
// each one's path, last write time and size. The times are only compared
// for equality, since libstdc++'s file clock counts today's as negative.
inline string FilesHash(const vector<string> &paths) {
    string s;
    for (auto &p : paths) {
        std::error_code tec, sec;
        auto t = filesystem::last_write_time(filesystem::path(p), tec);
        auto size = filesystem::file_size(filesystem::path(p), sec);
        Append(s, p, "\n", tec ? 0 : (int64_t)t.time_since_epoch().count(), " ",
               sec ? 0 : (uint64_t)size, "\n");
    }
    return Hex8(s);
}

inline string ReadStamp(const string &path) {
    string s;
    LoadFile(path, s);
    return s;
}

// Builds the package's native part with the toolchain the program is built
// with, unless the build there is up to date with its inputs (the sidecar,
// the C sources, the toolchain), and names what was built in `out`. The JIT
// library is built only when `wantjit`. Every program and process that uses
// the package builds it in that one directory: a lock there keeps them to
// one build at a time, and the response file and the stamp, which the
// others read without it, are replaced whole, the stamp last. False after a
// failed command, which the builder reports; a dry run prints the commands
// and reports success.
inline bool BuildNativePart(const Ast &ast, int pkg, const string &rootfile,
                            const Builder &program, bool wantjit, NativeBuild &out) {
    auto &p = ast.packages[pkg];
    // A package is built release whatever the program is (§5.5), one build
    // serving every program, and so is a CMake part. The program's own C
    // sources follow --debug, built apart.
    auto opts = program.opts;
    opts.debug = opts.debug && pkg == 0 && p.cmake_target.empty();
    Builder b { program.tc, opts, program.msgs };
    auto dir = NativeBuildDir(ast, pkg, rootfile, b);
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
    // The sidecar's path absolute, as the sources' are: the program's own
    // is as the command line named the program, and a build that named it
    // another way is the same build.
    auto inputs = p.csources;
    inputs.push_back(ForwardSlashes(AbsolutePath(p.sidecar)));
    string sidecartext;
    LoadFile(p.sidecar, sidecartext);
    // The JIT library's symbol table names every extern fn, so a new one
    // rebuilds it.
    auto externs = PackageExternNames(ast, pkg);
    string externtext;
    for (auto &n : externs) Append(externtext, n, "\n");
    // The stamp: what the build was made from, then whether it holds the
    // JIT library besides everything an AOT build has.
    auto made = cat(b.tc.id, "\n", FilesHash(inputs), "\n", Hex8(sidecartext), "\n",
                    Hex8(externtext), "\n", Hex8(GooseHeader()), "\n");
    auto aotstamp = cat(made, "aot\n"), jitstamp = cat(made, "jit\n");
    auto stampfile = cat(dir, "/stamp");
    // Whether the build there serves this one. A CMake package from a path
    // is handed to CMake every time, whose own build is incremental and
    // sees the sources this stamp does not; a checkout never changes.
    auto serves = [&](const string &have) {
        if (!p.cmake_target.empty() && p.cachekey.empty()) return false;
        return (have == jitstamp || (!wantjit && have == aotstamp)) && PathExists(out.rsp) &&
               (!wantjit || PathExists(out.jitlib));
    };
    if (!b.opts.dryrun && serves(ReadStamp(stampfile))) return true;
    std::error_code ec;
    filesystem::create_directories(filesystem::path(dir), ec);
    // One build at a time, and one that waited finds what the one before it
    // built. A dry run builds nothing, and needs no lock.
    optional<FileLock> lock;
    string have;
    if (!b.opts.dryrun) {
        lock.emplace(cat(dir, "/build.lock"),
                     cat("waiting for another goose building the package ", p.name, "'s C"));
        have = ReadStamp(stampfile);
        if (serves(have)) return true;
    }
    ProgressStep step(cat("building the package ", p.name, "'s C",
                          p.cmake_target.empty() ? "" : " with CMake"), !b.opts.dryrun);
    if (!b.opts.dryrun) WriteGooseHeader(out);
    if (!p.cmake_target.empty()) {
        if (!BuildCMakePart(ast, pkg, b, wantjit, externs, out)) return false;
        if (!b.opts.dryrun) WriteFileIfChanged(stampfile, wantjit ? jitstamp : aotstamp);
        return true;
    }
    // Where only the JIT library is missing, the objects already made from
    // these inputs serve as they are: a link in another process may be
    // reading them.
    auto objectsbuilt = (have == aotstamp || have == jitstamp) && PathExists(out.rsp);
    // The flags the package's C compiles with: its includes, defines and
    // flags for this style; position independent where a shared library
    // needs it.
    vector<string> flags;
    auto msvc = b.tc.style == "msvc";
    for (auto &i : p.cincludes) flags.push_back(cat(msvc ? "/I" : "-I", i));
    flags.push_back(cat(msvc ? "/I" : "-I", NativeIncludeDir(out)));
    for (auto &d : p.cdefines) flags.push_back(cat(msvc ? "/D" : "-D", d));
    for (auto &f : msvc ? p.cflags_msvc : p.cflags_cc) flags.push_back(f);
    #ifndef _WIN32
        if (!msvc) flags.push_back("-fPIC");
    #endif
    vector<string> objects;
    for (auto &src : p.csources) {
        // Named for the source's path too: two files in different
        // directories may share a name.
        auto name = cat(PathStem(src), "-", Hex8(src));
        auto obj = cat(dir, "/", name, msvc ? ".obj" : ".o");
        if (!objectsbuilt &&
            !b.CompileObject(src, obj, cat("compiling ", src, " for the package ", p.name),
                             cat(dir, "/", name, ".cc.log"), flags))
            return false;
        objects.push_back(obj);
    }
    vector<string> libs;
    for (auto &l : p.clibs) libs.push_back(b.LibFlag(l));
    string rsp;
    for (auto &o : objects) Append(rsp, "\"", o, "\"\n");
    for (auto &l : libs) Append(rsp, l, "\n");
    if (!b.opts.dryrun && !WriteFileIfChanged(out.rsp, rsp))
        throw CompileError { cat("cannot write ", out.rsp) };
    if (wantjit) {
        auto symsrc = cat(dir, "/goose_symbols.c");
        auto symobj = cat(dir, "/goose_symbols", msvc ? ".obj" : ".o");
        if (!b.opts.dryrun &&
            !WriteFileMakingDirs(symsrc, SymbolTableSource(p.name, externs)))
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
    if (!b.opts.dryrun) WriteFileIfChanged(stampfile, wantjit ? jitstamp : aotstamp);
    return true;
}

}  // namespace goose
