// Goose compiler — packages (docs/design/packages.md §4): what a sourced
// import, `import name from "source";`, names, and where that package's
// files are. A path source is a directory used in place, relative to the
// file that wrote it; a git source is fetched into GOOSE_HOME (§4.6).
#pragma once

namespace goose {

// In main.cpp.
string RelativeTo(const string &path, const string &dir);

struct PackageSource {
    string text;        // As written.
    bool ispath = false;
    string dir;         // A path source: the package's root, absolute, forward slashes, trailing /.
    string url, ref;    // A git source: what git clones, and the tag, branch or commit.
};

inline string ForwardSlashes(string path) {
    for (auto &c : path) if (c == '\\') c = '/';
    return path;
}

// `dir` absolute, with forward slashes and one trailing slash.
inline string PackageDir(const string &dir) {
    auto abs = ForwardSlashes(AbsolutePath(dir));
    while (abs.size() > 1 && abs.back() == '/') abs.pop_back();
    return abs + "/";
}

inline bool IsPathSource(const string &text) {
    return text.rfind("./", 0) == 0 || text.rfind("../", 0) == 0 || text.rfind(".\\", 0) == 0 ||
           text.rfind("..\\", 0) == 0 || text[0] == '/' || text[0] == '\\' ||
           (text.size() > 1 && text[1] == ':');
}

// Reads a source as written in a file in `fromdir`. A path is resolved from
// there; a git source is `host/owner/repo@ref`, `owner/repo@ref` for
// GitHub, or any URL git takes with `@ref` behind it. Throws for a shape
// that is neither.
inline PackageSource ParsePackageSource(const string &text, const string &fromdir) {
    PackageSource s;
    s.text = text;
    if (text.empty()) throw CompileError { "a package source is a path or a git repository" };
    if (IsPathSource(text)) {
        s.ispath = true;
        auto absolute = text[0] == '/' || text[0] == '\\' || (text.size() > 1 && text[1] == ':');
        s.dir = PackageDir(absolute ? text : cat(fromdir, text));
        return s;
    }
    auto at = text.rfind('@');
    // A `user@host:` prefix is part of the URL, not the ref.
    if (at == string::npos || at == 0 || text.find(':', at) != string::npos)
        throw CompileError { cat("the git source ", text, " needs a ref: host/owner/repo@tag, "
                                 "@branch or @commit") };
    s.ref = text.substr(at + 1);
    auto repo = text.substr(0, at);
    if (repo.find("://") != string::npos || repo.find('@') != string::npos) {
        s.url = repo;
    } else {
        auto slashes = (int)std::count(repo.begin(), repo.end(), '/');
        if (slashes == 1) s.url = cat("https://github.com/", repo);
        else if (slashes >= 2) s.url = cat("https://", repo);
        else throw CompileError { cat("the git source ", text, " is not owner/repo@ref, "
                                      "host/owner/repo@ref or a URL") };
    }
    if (s.ref.empty() || s.url.empty())
        throw CompileError { cat("the git source ", text, " needs a repository and a ref") };
    return s;
}

// Whether two sources name the same place: the same directory, or the same
// repository (refs aside).
inline bool SameSource(const PackageSource &a, const PackageSource &b) {
    if (a.ispath != b.ispath) return false;
    return a.ispath ? a.dir == b.dir : a.url == b.url;
}

// --- fetching (§4.6) ------------------------------------------------------------

// `goose fetch --update`: refs that are not commits are fetched again.
inline bool package_update = false;

inline bool IsCommitHash(const string &ref) {
    if (ref.size() < 7 || ref.size() > 40) return false;
    for (auto c : ref) if (!isxdigit((unsigned char)c)) return false;
    return true;
}

// Where a git source is checked out: GOOSE_HOME/pkg/<host>/<owner>/<repo>/
// <ref> for the usual repository URL, and for any other URL (a file:// one,
// a deeper path) GOOSE_HOME/pkg/<repo>-<hash of the URL>/<ref>, which keeps
// the path short where Windows limits it. A commit is named by its first
// twelve digits, a branch's slashes become underscores.
inline string GitCacheKey(const PackageSource &s) {
    auto key = s.url;
    if (auto p = key.find("://"); p != string::npos) key = key.substr(p + 3);
    while (!key.empty() && key.back() == '/') key.pop_back();
    if (key.size() > 4 && key.compare(key.size() - 4, 4, ".git") == 0) key.resize(key.size() - 4);
    auto safe = true;
    for (auto c : key)
        if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-' || c == '/')) safe = false;
    if (!safe || std::count(key.begin(), key.end(), '/') != 2 || key[0] == '/') {
        auto repo = key.substr(key.find_last_of('/') + 1);
        for (auto &c : repo) if (!(isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-')) c = '_';
        key = cat(repo.empty() ? "repo" : repo, "-", Hex8(s.url));
    }
    auto ref = IsCommitHash(s.ref) ? s.ref.substr(0, 12) : s.ref;
    for (auto &c : ref) if (c == '/' || c == '\\') c = '_';
    return cat(key, "/", ref);
}

inline string GitCacheDir(const PackageSource &s) {
    return cat(GooseHome(), "/pkg/", GitCacheKey(s));
}

// Reads the package's sidecar, `<name>.build` beside its entry, if there is
// one (§4.7): the C it compiles, where to find headers, what to define and
// link, or the CMake target to build; and for the program its executable's
// name. Lists take a _windows, _linux or _macos suffix for a platform's own
// part. Paths are relative to the package root.
inline void LoadSidecar(Package &pkg, bool isprogram) {
    pkg.sidecar = cat(pkg.root, pkg.name, ".build");
    auto bf = LoadBuildFile(pkg.sidecar, pkg.hassidecar);
    if (!pkg.hassidecar) return;
    const char *lists[] = { "c_sources", "c_include", "c_defines", "c_flags_cc", "c_flags_msvc",
                            "c_libs" };
    vector<string> known = { "cmake_target", "cmake_options", "main_thread_only" };
    for (auto key : lists)
        for (auto suffix : { "", "_windows", "_linux", "_macos" }) known.push_back(cat(key, suffix));
    if (isprogram) known.push_back("output");
    bf.CheckKeys(known);
    #ifdef _WIN32
        const char *platform = "_windows";
    #elif defined(__APPLE__)
        const char *platform = "_macos";
    #else
        const char *platform = "_linux";
    #endif
    auto list = [&](const char *key, bool paths) {
        auto v = bf.Strings(key);
        for (auto &e : bf.Strings(cat(key, platform))) v.push_back(e);
        if (paths)
            for (auto &e : v)
                if (!IsPathSource(e) || e.rfind("./", 0) == 0 || e.rfind("../", 0) == 0)
                    e = ForwardSlashes(AbsolutePath(cat(pkg.root, e)));
        return v;
    };
    pkg.csources = list("c_sources", true);
    pkg.cincludes = list("c_include", true);
    pkg.cdefines = list("c_defines", false);
    pkg.cflags_cc = list("c_flags_cc", false);
    pkg.cflags_msvc = list("c_flags_msvc", false);
    pkg.clibs = list("c_libs", false);
    pkg.cmake_target = bf.Str("cmake_target");
    pkg.cmake_options = bf.Strings("cmake_options");
    pkg.main_thread_only = bf.Bool("main_thread_only");
    if (isprogram) pkg.output = bf.Str("output");
    if (!pkg.csources.empty() && !pkg.cmake_target.empty())
        throw CompileError { cat(pkg.sidecar, ": a package has either c_sources or a cmake_target") };
}

// Removes a directory tree. A git checkout holds read-only files, which
// Windows will not delete until they are writable.
inline void RemoveTree(const string &path) {
    std::error_code ec;
    filesystem::path p(path);
    if (!filesystem::exists(p, ec)) return;
    #ifdef _WIN32
        for (auto &e : filesystem::recursive_directory_iterator(p, ec))
            filesystem::permissions(e.path(), filesystem::perms::owner_write,
                                    filesystem::perm_options::add, ec);
    #endif
    filesystem::remove_all(p, ec);
}

// Runs git with `args`, and throws what it printed when it failed. A
// checkout in the cache can lie deep, past the path length Windows allows
// git by default.
inline void RunGit(const string &git, const vector<string> &args) {
    vector<string> argv = { git, "-c", "core.longpaths=true" };
    for (auto &a : args) argv.push_back(a);
    auto r = RunProcess(argv);
    if (r.started && r.code == 0) return;
    throw CompileError { cat(CommandLineText(argv), "\n", r.output) };
}

// The checkout of the git source `s` for the package `name`, fetched into
// the cache if it is not there (or, for a tag or branch, when --update asks
// for it again): a shallow clone at the ref, with its submodules, or a
// fetch of the one commit. An interrupted fetch leaves a .tmp directory,
// never a half checkout under the name.
inline string FetchPackage(const PackageSource &s, const string &name) {
    auto dir = GitCacheDir(s);
    std::error_code ec;
    auto have = filesystem::is_directory(filesystem::path(dir), ec);
    auto update = have && package_update && !IsCommitHash(s.ref);
    if (!have || update) {
        auto git = FindOnPath("git");
        if (git.empty())
            throw CompileError { cat("cannot find `git` on PATH, needed to fetch the package ", name,
                                     " from ", s.text, ". Install git, or clone that repository "
                                     "yourself and declare it as\n    import ", name,
                                     " from \"<the directory>\";") };
        fprintf(stderr, "%s the package %s from %s\n", update ? "updating" : "fetching",
                name.c_str(), s.text.c_str());
        fflush(stderr);
        try {
            if (update) {
                RunGit(git, { "-C", dir, "fetch", "-q", "--depth", "1", s.url, s.ref });
                RunGit(git, { "-C", dir, "checkout", "-q", "--detach", "FETCH_HEAD" });
                RunGit(git, { "-C", dir, "submodule", "update", "-q", "--init", "--recursive",
                              "--depth", "1" });
            } else {
                auto tmp = dir + ".tmp";
                RemoveTree(tmp);
                filesystem::create_directories(filesystem::path(dir).parent_path(), ec);
                try {
                    if (IsCommitHash(s.ref)) {
                        RunGit(git, { "init", "-q", tmp });
                        RunGit(git, { "-C", tmp, "fetch", "-q", "--depth", "1", s.url, s.ref });
                        RunGit(git, { "-C", tmp, "checkout", "-q", "--detach", "FETCH_HEAD" });
                        RunGit(git, { "-C", tmp, "submodule", "update", "-q", "--init",
                                      "--recursive", "--depth", "1" });
                    } else {
                        RunGit(git, { "clone", "-q", "--depth", "1", "--branch", s.ref,
                                      "--recurse-submodules", "--shallow-submodules", s.url, tmp });
                    }
                } catch (CompileError &) {
                    RemoveTree(tmp);
                    throw;
                }
                filesystem::rename(filesystem::path(tmp), filesystem::path(dir), ec);
                if (ec) throw CompileError { cat("cannot move ", tmp, " to ", dir) };
            }
        } catch (CompileError &e) {
            throw CompileError { cat("cannot fetch the package ", name, " from ", s.text, ":\n",
                                     e.msg) };
        }
    }
    return dir + "/";
}

// The root directory of the package `name` from `source`: the directory
// itself for a path, the checkout in the cache for a git source; either
// must hold `<name>.goose`.
inline string PackageRoot(const PackageSource &source, const string &name) {
    string dir;
    if (source.ispath) {
        std::error_code ec;
        if (!filesystem::is_directory(filesystem::path(source.dir), ec))
            throw CompileError { cat("the package ", name, "'s source ", source.text,
                                     " is not a directory (", source.dir, ")") };
        dir = source.dir;
    } else {
        dir = FetchPackage(source, name);
    }
    if (!PathExists(cat(dir, name, ".goose")))
        throw CompileError { cat("the package ", name, " at ", dir, " has no ", name, ".goose") };
    return dir;
}

// The source as the root file in `rootdir` would write it, for the error
// that tells the root what to add: a path relative to there where that can
// be said, else as it is.
inline string SourceForRoot(const PackageSource &source, const string &rootdir) {
    if (!source.ispath) return source.text;
    auto rel = ForwardSlashes(RelativeTo(source.dir.substr(0, source.dir.size() - 1),
                                         PackageDir(rootdir.empty() ? string(".") : rootdir)));
    if (rel == source.dir.substr(0, source.dir.size() - 1)) return rel;   // No relative form.
    if (rel.rfind("../", 0) != 0 && rel.rfind("./", 0) != 0) rel = cat("./", rel);
    return rel;
}

}  // namespace goose
