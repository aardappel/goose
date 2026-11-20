// Goose compiler - the lower service layer's path and stdio functions
// (runtime_svc.h). A translation unit of its own (as process.cpp and
// progress.cpp are): readlink, realpath, GetModuleFileNameA and the binary
// stdio modes are platform headers' to give, and none of them reaches the
// compiler's own.

#include "runtime_svc.h"

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <unistd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
#endif

namespace goose {

// A path's directory, with its trailing separator; "" for a bare file name.
string DirOf(const string &path) {
    auto pos = path.find_last_of("/\\");
    return pos == string::npos ? "" : path.substr(0, pos + 1);
}

bool FileExists(const string &path) {
    auto f = fopen(path.c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
}

// A path split on either separator, with "." and cancelled "x/.." dropped. A
// path with a leading separator keeps a leading empty component ("/a" ->
// {"", "a"}), so two rooted paths share it and a rooted and a relative one
// never do; "" and "./" are the relative {}.
static vector<string> PathParts(const string &path) {
    vector<string> parts;
    string cur;
    for (size_t i = 0; i <= path.size(); i++) {
        auto c = i < path.size() ? path[i] : '/';
        if (c != '/' && c != '\\') { cur += c; continue; }
        if (i == 0 && i < path.size()) parts.push_back(cur);
        else if (cur == "." || cur.empty()) {}
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

// The language server's streams carry bytes with Content-Length counts,
// which the text mode Windows gives them, translating line breaks, would
// put off.
void SetBinaryStdio() {
    #ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
    #endif
}

}  // namespace goose
