// Goose compiler — other processes (process.h), over subprocess.h. This is
// the one translation unit besides main.cpp, kept apart so that the platform
// headers it needs stay out of the compiler's.

#include "includes.h"
#include "utils.h"
#include "process.h"
#include "progress.h"

#ifdef _MSC_VER
    #pragma warning(push, 0)
#endif
#include "subprocess.h"
#ifdef _MSC_VER
    #pragma warning(pop)
#endif

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <io.h>
#else
    #include <unistd.h>
    #include <dlfcn.h>
    #include <fcntl.h>
    #include <sys/file.h>
    #include <sys/stat.h>
#endif

namespace goose {

ProcessResult RunProcess(const vector<string> &argv, const string &cwd, bool echo) {
    ProcessResult r;
    vector<const char *> args;
    for (auto &a : argv) args.push_back(a.c_str());
    args.push_back(nullptr);
    // Everything the program prints comes back through one pipe, in order,
    // which is also what keeps it from filling the other one while this
    // process reads the first. Async mode is what makes the output readable
    // as it comes rather than only after the exit.
    auto options = subprocess_option_combined_stdout_stderr | subprocess_option_inherit_environment |
                   subprocess_option_search_user_path | subprocess_option_enable_async |
                   subprocess_option_no_window;
    subprocess_s p;
    if (subprocess_create_ex(args.data(), options, nullptr, cwd.empty() ? nullptr : cwd.c_str(),
                             &p)) {
        // subprocess.h leaves the reason in GetLastError on Windows, in
        // errno elsewhere.
        auto what = cat("cannot start ", argv.empty() ? string("(nothing)") : argv[0]);
        #ifdef _WIN32
            r.output = cat(what, " (Windows error ", (unsigned)GetLastError(), ")");
        #else
            r.output = cat(what, ": ", strerror(errno));
        #endif
        return r;
    }
    r.started = true;
    // No input is sent. Give the child EOF before reading its output, since
    // it may wait for input before it prints anything or exits.
    fclose(p.stdin_file);
    p.stdin_file = nullptr;
    char buf[4096];
    for (;;) {
        auto n = subprocess_read_stdout(&p, buf, sizeof buf);
        if (!n) break;
        r.output.append(buf, n);
        if (echo) fwrite(buf, 1, n, stderr);
        ProgressOutput(buf, n);
    }
    int code = -1;
    if (subprocess_join(&p, &code)) code = -1;
    subprocess_destroy(&p);
    r.code = code;
    return r;
}

static bool IsFile(const string &path) {
    #ifdef _WIN32
        auto attr = GetFileAttributesA(path.c_str());
        return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
    #else
        // A directory one may search is X_OK too.
        struct stat st;
        return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
               access(path.c_str(), X_OK) == 0;
    #endif
}

string FindOnPath(const string &name, const char *path) {
    if (name.find_first_of("/\\") != string::npos) return IsFile(name) ? name : "";
    vector<string> exts = { "" };
    #ifdef _WIN32
        constexpr char sep = ';';
        if (name.find('.') == string::npos) {
            auto pathext = getenv("PATHEXT");
            string list = pathext ? pathext : ".COM;.EXE;.BAT;.CMD";
            for (size_t i = 0; i < list.size();) {
                auto end = list.find(';', i);
                if (end == string::npos) end = list.size();
                auto ext = list.substr(i, end - i);
                for (auto &c : ext) c = (char)tolower((unsigned char)c);
                if (!ext.empty()) exts.push_back(ext);
                i = end + 1;
            }
        }
    #else
        constexpr char sep = ':';
    #endif
    if (!path) path = getenv("PATH");
    if (!path) return "";
    string dirs = path;
    for (size_t i = 0; i <= dirs.size();) {
        auto end = dirs.find(sep, i);
        if (end == string::npos) end = dirs.size();
        auto dir = dirs.substr(i, end - i);
        i = end + 1;
        if (dir.empty()) continue;
        for (auto &ext : exts) {
            #ifdef _WIN32
                const char *slash = "\\";
            #else
                const char *slash = "/";
            #endif
            auto cand = cat(dir, dir.back() == '/' || dir.back() == '\\' ? "" : slash, name, ext);
            if (IsFile(cand)) return cand;
        }
    }
    return "";
}

void SetEnvVar(const string &name, const string &value) {
    #ifdef _WIN32
        // Both the C runtime's copy, which getenv reads, and the process's
        // own block, which a started process inherits.
        _putenv_s(name.c_str(), value.c_str());
        SetEnvironmentVariableA(name.c_str(), value.c_str());
    #else
        setenv(name.c_str(), value.c_str(), 1);
    #endif
}

FileLock::FileLock(const string &path, const string &waiting) {
    ProgressStep step(waiting);
    #ifdef _WIN32
        auto h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
        auto error = h == INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
        if (h != INVALID_HANDLE_VALUE) {
            OVERLAPPED offset = {};
            if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK, 0, 1, 0, &offset)) {
                handle = reinterpret_cast<intptr_t>(h);
                return;
            }
            error = GetLastError();
            CloseHandle(h);
        }
        throw CompileError { cat("cannot lock ", path, " (Windows error ", (unsigned)error, ")") };
    #else
        auto fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (fd >= 0) {
            int result;
            do { result = flock(fd, LOCK_EX); } while (result < 0 && errno == EINTR);
            if (result == 0) {
                handle = fd;
                return;
            }
            auto error = errno;
            close(fd);
            errno = error;
        }
        throw CompileError { cat("cannot lock ", path, ": ", strerror(errno)) };
    #endif
}

FileLock::~FileLock() {
    #ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(handle));
    #else
        close((int)handle);
    #endif
}

// The table's entry, as native.h's goose_symbols.c lays it out.
struct GooseSymbolEntry {
    const char *name;
    const void *addr;
};

bool LoadSharedSymbols(const string &path, vector<pair<string, const void *>> &out, string &err) {
    typedef const GooseSymbolEntry *(*TableFn)(void);
    TableFn table = nullptr;
    #ifdef _WIN32
        auto wide = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
        vector<wchar_t> wpath((size_t)wide + 1);
        MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wide);
        auto h = LoadLibraryW(wpath.data());
        if (!h) {
            err = cat("cannot load ", path, " (Windows error ", (unsigned)GetLastError(), ")");
            return false;
        }
        table = (TableFn)GetProcAddress(h, "goose_symbols");
    #else
        auto h = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            auto why = dlerror();
            err = cat("cannot load ", path, ": ", why ? why : "unknown error");
            return false;
        }
        table = (TableFn)dlsym(h, "goose_symbols");
    #endif
    if (!table) {
        err = cat(path, " has no goose_symbols table");
        return false;
    }
    for (auto e = table(); e->name; e++) out.emplace_back(e->name, e->addr);
    return true;
}

string MacSdkPath() {
    string sdk;
    #ifdef __APPLE__
        // As TinyCC finds the SDK's libraries (tccmacho.c's
        // tcc_add_macos_sdkpath): the SDK xcode-select made active, which
        // libxcselect knows without running xcrun.
        if (auto xcs = dlopen("libxcselect.dylib", RTLD_GLOBAL | RTLD_LAZY)) {
            auto f = (int (*)(unsigned, char **))dlsym(xcs, "xcselect_host_sdk_path");
            char *path = nullptr;
            if (f) f(1, &path);
            if (path) {
                sdk = path;
                free(path);
            }
        }
    #endif
    return sdk;
}

string CommandLineText(const vector<string> &argv) {
    string s;
    for (auto &a : argv) {
        if (!s.empty()) s += ' ';
        if (!a.empty() && a.find_first_of(" \t\"'&|<>^();$`\\") == string::npos) {
            s += a;
            continue;
        }
        // Backslashes stand, as Windows paths are full of them and only a
        // quote needs the escape on either platform's reading.
        s += '"';
        for (auto c : a) {
            if (c == '"') s += '\\';
            s += c;
        }
        s += '"';
    }
    return s;
}

}  // namespace goose
