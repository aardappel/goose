// Goose compiler — `goose build` (docs/design/packages.md §6): the C the
// compiler wrote, the runtime object and the native modules' response files,
// compiled and linked into an executable by the toolchain of toolchain.h.
// Intermediates live under GOOSE_HOME; the executable lands beside the root
// source unless -o says otherwise.
#pragma once

namespace goose {

// In main.cpp: the runtime's text hash, the runtime object's C, and a path's directory.
string RuntimeVersion();
string RuntimeObjectSource();
string DirOf(const string &path);

struct BuildOptions {
    string ccrequest;         // --cc
    bool debug = false;       // --debug: GS_DEBUG=1, debug information, no C optimization
    bool verbose = false;     // -v: print every command and what it prints
    bool dryrun = false;      // --dry-run: print the commands and run nothing
    bool standalone = false;  // --standalone: no runtime object
    string exe;               // -o: the executable; "" puts it beside the root source
};

inline string AbsolutePath(const string &path) {
    std::error_code ec;
    auto abs = filesystem::absolute(filesystem::path(path), ec);
    return ec ? path : abs.lexically_normal().string();
}

// GOOSE_HOME/build/prog/<stem>-<hash of the root's absolute path>/: where a
// program's C, objects and logs go.
inline string ProgramBuildDir(const string &rootfile) {
    auto abs = AbsolutePath(rootfile);
    #ifdef _WIN32
        for (auto &c : abs) c = (char)tolower((unsigned char)c);   // One directory per file, however spelled.
    #endif
    return cat(GooseHome(), "/build/prog/", PathStem(rootfile), "-", Hex8(abs));
}

// Runs the toolchain's commands, printing them as -v and --dry-run ask, and
// a failed one with what it printed.
struct Builder {
    const Toolchain &tc;
    const BuildOptions &opts;
    FILE *msgs;

    // The compile flags of both styles: optimization, or debug information
    // and none of it. Defines go into the C itself (main.cpp), never here.
    vector<string> CFlags() const {
        if (tc.style == "msvc") {
            vector<string> f = { "/nologo" };
            if (opts.debug) f.push_back("/Od"), f.push_back("/Zi");
            else f.push_back("/O2");
            return f;
        }
        if (opts.debug) return { "-O0", "-g" };
        return { "-O2" };
    }

    // The system libraries a program links beyond the C library, in the cc
    // style: none on Windows, where they live in the C runtime.
    vector<string> SystemLibs() const {
        #ifdef _WIN32
            return {};
        #else
            if (tc.style == "msvc") return {};
            return { "-lm", "-pthread" };
        #endif
    }

    // Runs `argv`, `what` being what it does ("compiling the runtime"), with
    // its output kept in `logfile`. False when it failed, after saying so.
    bool Run(const vector<string> &argv, const string &what, const string &logfile) const {
        if (opts.dryrun || opts.verbose) fprintf(msgs, "%s\n", CommandLineText(argv).c_str());
        if (opts.dryrun) return true;
        fflush(msgs);
        auto r = RunProcess(argv, "", opts.verbose);
        if (!logfile.empty()) WriteFileMakingDirs(logfile, r.output);
        if (r.started && r.code == 0) return true;
        fprintf(stderr, "%s failed%s:\n    %s\n", what.c_str(),
                r.started ? cat(" (exit ", r.code, ")").c_str() : "", CommandLineText(argv).c_str());
        if (!opts.verbose) fputs(r.output.c_str(), stderr);
        if (!logfile.empty()) fprintf(stderr, "(the output is in %s)\n", logfile.c_str());
        return false;
    }

    // A system library by name, as the style's linker takes it.
    string LibFlag(const string &lib) const {
        return tc.style == "msvc" ? cat(lib, ".lib") : cat("-l", lib);
    }

    // Compiles the C file `src` to the object `obj`, with `extra` flags
    // (a package's includes and defines) after the usual ones.
    bool CompileObject(const string &src, const string &obj, const string &what,
                       const string &logfile, const vector<string> &extra = {}) const {
        vector<string> argv = { tc.cc };
        for (auto &f : CFlags()) argv.push_back(f);
        for (auto &f : extra) argv.push_back(f);
        if (tc.style == "msvc") {
            argv.push_back("/c");
            argv.push_back(src);
            argv.push_back(cat("/Fo:", obj));
            if (opts.debug) argv.push_back(cat("/Fd:", obj, ".pdb"));
        } else {
            argv.push_back("-c");
            argv.push_back(src);
            argv.push_back("-o");
            argv.push_back(obj);
        }
        return Run(argv, what, logfile);
    }

    // Links `objects` and the system libraries `libs` (LibFlag spellings)
    // into the shared library `out`, with `builddir` taking the
    // intermediates: a package's JIT library.
    bool LinkShared(const vector<string> &objects, const vector<string> &libs, const string &out,
                    const string &builddir, const string &logfile, const string &what) const {
        vector<string> argv = { tc.cc };
        if (tc.style == "msvc") {
            argv.push_back("/nologo");
            argv.push_back("/LD");
            argv.push_back(cat("/Fe:", out));
            argv.push_back(cat("/Fo:", builddir, "\\"));
            for (auto &o : objects) argv.push_back(o);
            for (auto &l : libs) argv.push_back(l);
        } else {
            argv.push_back("-shared");
            for (auto &o : objects) argv.push_back(o);
            argv.push_back("-o");
            argv.push_back(out);
            for (auto &l : libs) argv.push_back(l);
            for (auto &l : SystemLibs()) argv.push_back(l);
        }
        return Run(argv, what, logfile);
    }

    // Compiles `srcs` and links them with `objects` and the response files
    // `rsps` into `exe`, with `builddir` taking the intermediates.
    bool Link(const vector<string> &srcs, const vector<string> &objects, const vector<string> &rsps,
              const string &exe, const string &builddir, const string &logfile) const {
        vector<string> argv = { tc.cc };
        for (auto &f : CFlags()) argv.push_back(f);
        if (tc.style == "msvc") {
            argv.push_back(cat("/Fe:", exe));
            argv.push_back(cat("/Fo:", builddir, "\\"));
            if (opts.debug) argv.push_back(cat("/Fd:", builddir, "\\", PathStem(exe), ".pdb"));
            for (auto &s : srcs) argv.push_back(s);
            for (auto &o : objects) argv.push_back(o);
            for (auto &r : rsps) argv.push_back(cat("@", r));
        } else {
            for (auto &s : srcs) argv.push_back(s);
            for (auto &o : objects) argv.push_back(o);
            argv.push_back("-o");
            argv.push_back(exe);
            for (auto &r : rsps) argv.push_back(cat("@", r));
            for (auto &l : SystemLibs()) argv.push_back(l);
        }
        return Run(argv, cat("building ", exe), logfile);
    }
};

// The runtime object for this toolchain and configuration, compiled from
// `goose --emit-runtime`'s C on first use: GOOSE_HOME/build/runtime/<runtime
// hash>/<toolchain id>-<config>/goose_runtime.o. "" when it could not be
// built, after the builder said why.
inline string RuntimeObject(const Builder &b) {
    auto dir = cat(GooseHome(), "/build/runtime/", RuntimeVersion(), "/", b.tc.id, "-",
                   b.opts.debug ? "debug" : "release");
    auto obj = cat(dir, "/goose_runtime.", b.tc.style == "msvc" ? "obj" : "o");
    if (PathExists(obj) && !b.opts.dryrun) return obj;
    auto src = cat(dir, "/goose_runtime.c");
    auto log = cat(dir, "/cc.log");
    if (b.opts.dryrun) {
        b.CompileObject(src, obj, "compiling the runtime", log);
        return obj;
    }
    std::error_code ec;
    filesystem::create_directories(filesystem::path(dir), ec);
    if (ec) throw CompileError { cat("cannot create ", dir, ": ", ec.message()) };
    // Every first build shares these paths. Wait before writing the source
    // or invoking the compiler, then recheck what the previous builder left.
    FileLock lock(cat(dir, "/build.lock"));
    if (PathExists(obj)) return obj;
    if (!WriteFileMakingDirs(src, RuntimeObjectSource()))
        throw CompileError { cat("cannot write ", src) };
    // A failed or interrupted compiler may leave a partial object. Only a
    // successful compile publishes the name readers use, by atomic rename.
    auto tmp = cat(dir, "/goose_runtime.tmp.", b.tc.style == "msvc" ? "obj" : "o");
    filesystem::remove(filesystem::path(tmp), ec);
    if (ec) throw CompileError { cat("cannot remove ", tmp, ": ", ec.message()) };
    if (!b.CompileObject(src, tmp, "compiling the runtime", log)) return "";
    filesystem::rename(filesystem::path(tmp), filesystem::path(obj), ec);
    if (ec) throw CompileError { cat("cannot publish ", obj, ": ", ec.message()) };
    return obj;
}

// What `goose build` does once the compiler has written the program's C to
// `cfiles` in `builddir`: finds the toolchain, the runtime object and the
// response files of the native parts `nativersps` (native.h and builtin.h,
// found by the caller), and links the executable, named by -o, the
// program's sidecar (`output`) or the root file. The exit code.
inline int BuildExecutable(const string &rootfile, const vector<string> &cfiles,
                           const string &builddir,
                           const BuildOptions &opts, const string &exedir, FILE *msgs,
                           const vector<string> &nativersps, const string &output) {
    Toolchain tc;
    string why;
    if (!FindToolchain(opts.ccrequest, tc, why)) {
        fprintf(stderr, "%s\n", why.c_str());
        return 1;
    }
    Builder b { tc, opts, msgs };
    vector<string> objects;
    if (!opts.standalone) {
        auto obj = RuntimeObject(b);
        if (obj.empty()) return 1;
        objects.push_back(obj);
    }
    // The response files in link order: a package's before the packages it
    // requires, the program's own C first of all.
    auto rsps = nativersps;
    (void)exedir;
    auto exe = opts.exe;
    if (exe.empty()) {
        exe = cat(DirOf(rootfile), output.empty() ? PathStem(rootfile) : output);
        #ifdef _WIN32
            if (exe.size() < 4 || exe.compare(exe.size() - 4, 4, ".exe") != 0) exe += ".exe";
        #endif
    }
    // A split program's files compile side by side, then link as objects.
    auto srcs = cfiles;
    if (cfiles.size() > 1) {
        vector<string> objs;
        for (auto &c : cfiles)
            objs.push_back(cat(c.substr(0, c.size() - 2), tc.style == "msvc" ? ".obj" : ".o"));
        vector<char> ok(cfiles.size());
        vector<std::thread> jobs;
        for (size_t i = 0; i < cfiles.size(); i++)
            jobs.emplace_back([&, i] {
                ok[i] = b.CompileObject(cfiles[i], objs[i], cat("compiling ", cfiles[i]),
                                        cat(objs[i], ".log"));
            });
        for (auto &j : jobs) j.join();
        for (auto k : ok)
            if (!k) return 1;
        objects.insert(objects.begin(), objs.begin(), objs.end());
        srcs.clear();
    }
    if (!b.Link(srcs, objects, rsps, exe, builddir, cat(builddir, "/cc.log"))) return 1;
    if (!opts.dryrun) fprintf(msgs, "built %s\n", exe.c_str());
    return 0;
}

}  // namespace goose
