// Goose compiler — the tools a build calls (docs/design/packages.md §6.1): a
// C compiler with its C++ driver, cmake and git, found on this machine. A
// toolchain has a style, msvc or cc, which decides how flags and response
// files are spelled, and an id, which names what is built with it in the
// cache. GOOSE_HOME is where that cache and the compiler's settings live.
#pragma once

#include <filesystem>

namespace goose {

// The environment variable, else ~/.goose. With forward slashes throughout,
// which every tool on Windows takes, so that paths made from it are all of
// one kind.
inline string GooseHome() {
    string home;
    if (auto env = getenv("GOOSE_HOME"); env && *env) home = env;
    else {
        #ifdef _WIN32
            auto profile = getenv("USERPROFILE");
        #else
            auto profile = getenv("HOME");
        #endif
        home = cat(profile && *profile ? profile : ".", "/.goose");
    }
    for (auto &c : home) if (c == '\\') c = '/';
    while (home.size() > 1 && home.back() == '/') home.pop_back();
    return home;
}

struct Toolchain {
    string name;      // msvc, clang-cl, clang, gcc or cc.
    string style;     // msvc or cc: how flags and response files are spelled.
    string cc;        // The C driver's path.
    string cxx;       // The C++ driver's path, "" where there is none.
    string version;
    string desc;      // For people: "MSVC 19.44.35207 (toolset 14.44.35207)".
    string id;        // For the cache: name-version-hash of the path and style.
    string how;       // Where it was found: on PATH, in Visual Studio, --cc, ...
};

struct ToolInfo {
    string path;      // "" when not found.
    string version;
};

struct Tools {
    bool havecc = false;
    Toolchain cc;
    string ccwhy;     // Without a C compiler: what was looked for and what to do.
    ToolInfo cmake, git;
    string home;
};

// --- helpers -----------------------------------------------------------------

inline string FirstLine(const string &text) {
    size_t i = 0;
    while (i < text.size()) {
        auto end = text.find('\n', i);
        if (end == string::npos) end = text.size();
        auto line = text.substr(i, end - i);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (!line.empty()) return line;
        i = end + 1;
    }
    return "";
}

// The version a tool's banner names: what follows "version " (in any case),
// else the line's last word.
inline string VersionIn(const string &line) {
    string lower = line;
    for (auto &c : lower) c = (char)tolower((unsigned char)c);
    auto at = lower.find("version ");
    if (at != string::npos) {
        auto start = at + 8;
        auto end = line.find_first_of(" \t,", start);
        return line.substr(start, end == string::npos ? string::npos : end - start);
    }
    auto sp = line.find_last_of(' ');
    return sp == string::npos ? line : line.substr(sp + 1);
}

inline string Hex8(const string &s) {
    uint64_t h = 14695981039346656037ull;
    for (auto c : s) h = (h ^ (uint8_t)c) * 1099511628211ull;
    char buf[9];
    snprintf(buf, sizeof buf, "%08x", (unsigned)(h >> 32));
    return buf;
}

inline string BaseName(const string &path) {
    auto pos = path.find_last_of("/\\");
    return pos == string::npos ? path : path.substr(pos + 1);
}

inline string LowerNoExe(string name) {
    for (auto &c : name) c = (char)tolower((unsigned char)c);
    if (name.size() > 4 && name.compare(name.size() - 4, 4, ".exe") == 0)
        name.resize(name.size() - 4);
    return name;
}

inline bool PathExists(const string &path) {
    std::error_code ec;
    return filesystem::exists(filesystem::path(path), ec);
}

// A file whose directory may not exist yet. False when it cannot be written.
inline bool WriteFileMakingDirs(const string &path, const string &text) {
    std::error_code ec;
    filesystem::create_directories(filesystem::path(path).parent_path(), ec);
    auto f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

// The program `name`'s version line, from the arguments that make it print
// one (`--version` for the gcc-style drivers and cmake and git; cl prints
// its banner when given nothing).
inline string ToolVersion(const string &path, const vector<string> &args) {
    vector<string> argv = { path };
    for (auto &a : args) argv.push_back(a);
    auto r = RunProcess(argv);
    return r.started ? VersionIn(FirstLine(r.output)) : "";
}

inline ToolInfo FindTool(const string &name, const vector<string> &extradirs = {}) {
    ToolInfo t;
    t.path = FindOnPath(name);
    for (auto &dir : extradirs) {
        if (!t.path.empty()) break;
        auto cand = cat(dir, "/", name);
        #ifdef _WIN32
            cand += ".exe";
        #endif
        if (PathExists(cand)) t.path = cand;
    }
    if (!t.path.empty()) t.version = ToolVersion(t.path, { "--version" });
    return t;
}

// --- Visual Studio -------------------------------------------------------------

#ifdef _WIN32

// The newest Visual Studio's installation directory, by vswhere; "" for none.
inline string VsInstallDir() {
    static string found;
    static bool probed = false;
    if (probed) return found;
    probed = true;
    auto pf = getenv("ProgramFiles(x86)");
    auto vswhere = cat(pf ? pf : "C:\\Program Files (x86)",
                       "\\Microsoft Visual Studio\\Installer\\vswhere.exe");
    if (!PathExists(vswhere)) return found;
    auto r = RunProcess({ vswhere, "-latest", "-prerelease", "-products", "*",
                          "-property", "installationPath" });
    if (r.started && r.code == 0) found = FirstLine(r.output);
    return found;
}

// Imports the environment vcvars64.bat sets up (PATH, INCLUDE, LIB, ...)
// into this process, so that cl and link work from here on. Running the
// batch file costs a second or two, so what it printed is kept under
// GOOSE_HOME against the file's path and modification time; `refresh`
// ignores that. False when there is no Visual Studio with a C++ toolset.
inline bool ImportVcvars(bool refresh) {
    auto vs = VsInstallDir();
    if (vs.empty()) return false;
    auto vcvars = cat(vs, "\\VC\\Auxiliary\\Build\\vcvars64.bat");
    if (!PathExists(vcvars)) return false;
    std::error_code ec;
    auto mtime = filesystem::last_write_time(filesystem::path(vcvars), ec).time_since_epoch().count();
    auto header = cat("# ", vcvars, " ", (long long)mtime, "\n");
    auto cachedir = cat(GooseHome(), "/toolchain");
    auto cachefile = cat(cachedir, "/vcvars64-", Hex8(vcvars), ".env");
    string env;
    if (!refresh && LoadFile(cachefile, env) && env.compare(0, header.size(), header) == 0) {
        env.erase(0, header.size());
    } else {
        // Through a batch file of our own: cmd's own quoting would otherwise
        // have to agree with the runner's.
        auto bat = cat(cachedir, "/vcvars64-probe.bat");
        if (!WriteFileMakingDirs(bat, cat("@echo off\r\ncall \"", vcvars, "\" >nul 2>&1\r\nset\r\n")))
            return false;
        auto r = RunProcess({ "cmd.exe", "/c", bat });
        if (!r.started || r.code != 0) return false;
        env = r.output;
        WriteFileMakingDirs(cachefile, cat(header, env));
    }
    for (size_t i = 0; i < env.size();) {
        auto end = env.find('\n', i);
        if (end == string::npos) end = env.size();
        auto line = env.substr(i, end - i);
        i = end + 1;
        while (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        // cmd's hidden variables start with "=" and are not for a child.
        if (eq == string::npos || eq == 0) continue;
        SetEnvVar(line.substr(0, eq), line.substr(eq + 1));
    }
    return true;
}

inline string VsLlvmTool(const string &name) {
    auto vs = VsInstallDir();
    if (vs.empty()) return "";
    auto path = cat(vs, "\\VC\\Tools\\Llvm\\x64\\bin\\", name, ".exe");
    return PathExists(path) ? path : "";
}

inline vector<string> VsCmakeDirs() {
    auto vs = VsInstallDir();
    if (vs.empty()) return {};
    return { cat(vs, "\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin") };
}

#endif

// cmake and ninja, on PATH or where Visual Studio installs its own.
inline string CMakePath() {
    #ifdef _WIN32
        return FindTool("cmake", VsCmakeDirs()).path;
    #else
        return FindOnPath("cmake");
    #endif
}

inline string NinjaPath() {
    auto p = FindOnPath("ninja");
    #ifdef _WIN32
        if (p.empty()) {
            auto vs = VsInstallDir();
            if (!vs.empty()) {
                auto cand = cat(vs, "\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja\\ninja.exe");
                if (PathExists(cand)) p = cand;
            }
        }
    #endif
    return p;
}

// --- the C toolchain -----------------------------------------------------------

// Fills in what the driver at `cc` says about itself: its version, the
// description, the id.
inline Toolchain DescribeToolchain(const string &name, const string &style, const string &cc,
                                   const string &cxx, const string &how) {
    Toolchain t;
    t.name = name;
    t.style = style;
    t.cc = cc;
    t.cxx = cxx;
    t.how = how;
    if (name == "msvc") {
        t.version = ToolVersion(cc, {});
        auto toolset = getenv("VCToolsVersion");
        t.desc = cat("MSVC ", t.version, toolset ? cat(" (toolset ", toolset, ")") : string());
    } else {
        t.version = ToolVersion(cc, { "--version" });
        t.desc = cat(name, " ", t.version);
    }
    t.id = cat(name, "-", t.version, "-", Hex8(cat(cc, "|", style)));
    return t;
}

// What the gcc-style driver at `path` is, by its banner: clang, gcc, or
// just cc.
inline string DriverKind(const string &path) {
    auto r = RunProcess({ path, "--version" });
    if (!r.started) return "cc";
    string lower = r.output;
    for (auto &c : lower) c = (char)tolower((unsigned char)c);
    if (lower.find("clang") != string::npos) return "clang";
    if (lower.find("gcc") != string::npos || lower.find("free software foundation") != string::npos)
        return "gcc";
    return "cc";
}

// The C++ driver beside a gcc-style C driver: the same name with ++ (clang++,
// g++), next to it or on PATH; c++ on PATH for a plain cc.
inline string CxxBeside(const string &kind, const string &cc) {
    auto plus = kind == "clang" ? "clang++" : kind == "gcc" ? "g++" : "c++";
    auto dir = cc.substr(0, cc.find_last_of("/\\") + 1);
    for (auto ext : { "", ".exe" }) {
        auto cand = cat(dir, plus, ext);
        if (!dir.empty() && PathExists(cand)) return cand;
    }
    return FindOnPath(plus);
}

inline Toolchain GccStyle(const string &cc, const string &how, const string &kind = "") {
    auto k = kind.empty() ? DriverKind(cc) : kind;
    return DescribeToolchain(k, "cc", cc, CxxBeside(k, cc), how);
}

inline Toolchain MsvcStyle(const string &name, const string &cc, const string &how) {
    return DescribeToolchain(name, "msvc", cc, cc, how);
}

// The C toolchain to build with: the one `request` asks for (a name: msvc,
// clang-cl, clang, gcc, cc; or a path), else GOOSE_CC, else `cc` in
// GOOSE_HOME/config, else the first found on this machine (the order in
// docs/design/packages.md §6.1). False, with `why` saying what was looked
// for, when there is none.
inline bool FindToolchain(string request, Toolchain &out, string &why,
                          [[maybe_unused]] bool refresh = false) {
    string how = "--cc";
    if (request.empty()) {
        if (auto env = getenv("GOOSE_CC"); env && *env) {
            request = env;
            how = "GOOSE_CC";
        }
    }
    if (request.empty()) {
        bool found;
        auto cfg = LoadBuildFile(cat(GooseHome(), "/config"), found);
        if (found) {
            cfg.CheckKeys({ "cc" });
            request = cfg.Str("cc");
            how = "GOOSE_HOME/config";
        }
    }
    auto lowered = request;
    for (auto &c : lowered) c = (char)tolower((unsigned char)c);
    auto bypath = request.find_first_of("/\\") != string::npos ||
                  (lowered.size() > 4 && lowered.compare(lowered.size() - 4, 4, ".exe") == 0);
    if (!request.empty() && bypath) {
        if (!PathExists(request)) {
            why = cat("the C compiler ", request, " (", how, ") is not there");
            return false;
        }
        auto base = LowerNoExe(BaseName(request));
        if (base == "cl" || base == "clang-cl") {
            #ifdef _WIN32
                if (base == "cl" && FindOnPath("cl").empty()) ImportVcvars(refresh);
            #endif
            out = MsvcStyle(base == "cl" ? "msvc" : base, request, how);
        } else {
            out = GccStyle(request, how);
        }
        return true;
    }
    auto name = request;
    if (name == "cl") name = "msvc";
    auto wanted = [&](const char *n) { return name.empty() || name == n; };
    #ifdef _WIN32
        if (wanted("msvc")) {
            if (auto cl = FindOnPath("cl"); !cl.empty()) {
                out = MsvcStyle("msvc", cl, "on PATH");
                return true;
            }
        }
        if (wanted("clang-cl")) {
            if (auto p = VsLlvmTool("clang-cl"); !p.empty()) {
                out = MsvcStyle("clang-cl", p, "in Visual Studio");
                return true;
            }
            if (auto p = FindOnPath("clang-cl"); !p.empty()) {
                out = MsvcStyle("clang-cl", p, "on PATH");
                return true;
            }
        }
        if (wanted("clang")) {
            if (auto p = FindOnPath("clang"); !p.empty()) {
                out = GccStyle(p, "on PATH", "clang");
                return true;
            }
            if (auto p = VsLlvmTool("clang"); !p.empty()) {
                out = GccStyle(p, "in Visual Studio", "clang");
                return true;
            }
        }
        if (wanted("msvc")) {
            // cl through vcvars64.bat, which also brings link, the headers
            // and the libraries: the one path that takes a second to find.
            if (ImportVcvars(refresh)) {
                if (auto cl = FindOnPath("cl"); !cl.empty()) {
                    out = MsvcStyle("msvc", cl, "in Visual Studio (vcvars64)");
                    return true;
                }
            }
        }
        if (wanted("gcc")) {
            if (auto p = FindOnPath("gcc"); !p.empty()) {
                out = GccStyle(p, "on PATH", "gcc");
                return true;
            }
        }
        if (wanted("cc")) {
            if (auto p = FindOnPath("cc"); !p.empty()) {
                out = GccStyle(p, "on PATH");
                return true;
            }
        }
        if (!name.empty() && name != "msvc" && name != "clang-cl" && name != "clang" &&
            name != "gcc" && name != "cc") {
            why = cat("unknown C compiler name ", request, " (", how,
                      "): the names are msvc, clang-cl, clang, gcc and cc, or give a path");
            return false;
        }
        why = name.empty()
            ? "cannot find a C compiler (looked for cl, clang-cl and clang on PATH and in "
              "Visual Studio); install Visual Studio with C++ or LLVM, or set GOOSE_CC"
            : cat("cannot find the C compiler ", request, " (", how, ") on PATH or in Visual Studio");
        return false;
    #else
        for (auto n : { "cc", "gcc", "clang" }) {
            if (!wanted(n)) continue;
            if (auto p = FindOnPath(n); !p.empty()) {
                // A `cc` is whatever it turns out to be; gcc and clang by
                // name are what they say.
                out = GccStyle(p, "on PATH", string(n) == "cc" ? "" : n);
                return true;
            }
        }
        if (!name.empty() && name != "clang" && name != "gcc" && name != "cc") {
            why = cat("unknown C compiler name ", request, " (", how,
                      "): the names are clang, gcc and cc, or give a path");
            return false;
        }
        why = name.empty()
            ? "cannot find a C compiler (looked for cc, gcc and clang on PATH); install one, "
              "or set GOOSE_CC"
            : cat("cannot find the C compiler ", request, " (", how, ") on PATH");
        return false;
    #endif
}

// Everything `goose toolchain` reports: the C toolchain, cmake and git.
inline Tools FindTools(const string &ccrequest, bool refresh = false) {
    Tools t;
    t.home = GooseHome();
    t.havecc = FindToolchain(ccrequest, t.cc, t.ccwhy, refresh);
    #ifdef _WIN32
        t.cmake = FindTool("cmake", VsCmakeDirs());
    #else
        t.cmake = FindTool("cmake");
    #endif
    t.git = FindTool("git");
    return t;
}

inline string JsonString(const string &s) {
    string out = "\"";
    for (auto c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((uint8_t)c < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out + "\"";
}

// What `goose toolchain` prints: for people, or as JSON for scripts.
// `builtin` names the modules whose packages are compiled into this
// compiler (builtin.h), for the runners to know what runs without a build.
inline string ToolsReport(const Tools &t, bool json, const vector<string> &builtin) {
    string s;
    if (json) {
        auto tool = [](const ToolInfo &ti) {
            return ti.path.empty() ? string("null")
                 : cat("{\"path\": ", JsonString(ti.path), ", \"version\": ", JsonString(ti.version), "}");
        };
        s += "{\n";
        if (t.havecc) {
            auto &c = t.cc;
            Append(s, "  \"cc\": {\"name\": ", JsonString(c.name), ", \"style\": ", JsonString(c.style),
                   ", \"path\": ", JsonString(c.cc), ", \"cxx\": ", JsonString(c.cxx),
                   ", \"version\": ", JsonString(c.version), ", \"desc\": ", JsonString(c.desc),
                   ", \"id\": ", JsonString(c.id), ", \"how\": ", JsonString(c.how), "},\n");
        } else {
            Append(s, "  \"cc\": null,\n  \"cc_error\": ", JsonString(t.ccwhy), ",\n");
        }
        Append(s, "  \"cmake\": ", tool(t.cmake), ",\n  \"git\": ", tool(t.git), ",\n");
        s += "  \"builtin\": [";
        for (size_t i = 0; i < builtin.size(); i++) Append(s, i ? ", " : "", JsonString(builtin[i]));
        Append(s, "],\n  \"home\": ", JsonString(t.home), "\n}\n");
        return s;
    }
    if (t.havecc) {
        Append(s, "C compiler:   ", t.cc.desc, ", found ", t.cc.how, "\n              ", t.cc.cc, "\n");
        Append(s, "C++ compiler: ", t.cc.cxx.empty() ? "none" : t.cc.cxx, "\n");
        Append(s, "style:        ", t.cc.style, "\nid:           ", t.cc.id, "\n");
    } else {
        Append(s, "C compiler:   ", t.ccwhy, "\n");
    }
    auto tool = [&](const char *name, const ToolInfo &ti) {
        Append(s, name, ti.path.empty() ? string("not found on PATH")
                                              : cat(ti.path, " (", ti.version, ")"), "\n");
    };
    tool("cmake:       ", t.cmake);
    tool("git:         ", t.git);
    string mods;
    for (auto &m : builtin) Append(mods, mods.empty() ? "" : ", ", m);
    Append(s, "built in:     ", mods.empty() ? "no packages" : mods, "\n");
    Append(s, "GOOSE_HOME:   ", t.home, "\n");
    return s;
}

}  // namespace goose
