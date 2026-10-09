// Goose compiler — project-wide utilities.
#pragma once

namespace goose {

// String building: small variadic append/cat helpers, no iostreams.
inline void CatOne(string &s, string_view v)   { s += v; }
inline void CatOne(string &s, const char *v)   { s += v; }
inline void CatOne(string &s, const string &v) { s += v; }
inline void CatOne(string &s, char v)          { s += v; }
inline void CatOne(string &s, bool v)          { s += v ? "true" : "false"; }
// Every other integer, whatever it is spelled as here: size_t is unsigned long
// on the LP64 systems and unsigned long long on Windows, so a fixed set of
// overloads leaves it ambiguous on one platform or the other.
template<typename T> requires (is_integral_v<T> && !is_same_v<T, char> && !is_same_v<T, bool>)
void CatOne(string &s, T v)                    { s += to_string(v); }
inline void CatOne(string &s, double v) {
    // The spellings Goose programs print (§3.7), whatever the C library's.
    if (v != v) { s += "nan"; return; }
    if (std::isinf(v)) { s += v > 0 ? "inf" : "-inf"; return; }
    // The runtime's text (gs_fmt_f64): the fewest significant digits that
    // read back as v and the nearest of those, which to_chars finds as well,
    // laid out as %g lays them out at a precision of max(15, digits): in
    // exponent form below 1e-4 or from that power of ten up, else
    // positionally, with .0 on a whole number.
    char buf[32];
    *to_chars(buf, buf + sizeof buf - 1, v, chars_format::scientific).ptr = 0;
    auto e = strchr(buf, 'e');
    string dig;
    for (auto p = buf; p != e; p++)
        if (*p != '-' && *p != '.') dig += *p;
    int n = (int)dig.size(), x = atoi(e + 1);
    if (x < -4 || x >= max(n, 15)) { s += buf; return; }
    if (buf[0] == '-') s += '-';
    if (x < 0) {
        s += "0.";
        s.append(-x - 1, '0');
        s += dig;
        return;
    }
    s += dig.substr(0, x + 1);
    if (n < x + 1) s.append(x + 1 - n, '0');
    s += '.';
    s += n > x + 1 ? dig.substr(x + 1) : "0";
}

template<typename... Ts> void Append(string &s, const Ts &...args) {
    (CatOne(s, args), ...);
}

template<typename... Ts> string cat(const Ts &...args) {
    string s;
    Append(s, args...);
    return s;
}

// Compile errors (in the program being compiled) throw a string, caught in main.
struct CompileError { string msg; };

// The native stack. The typechecker recurses once per function on the
// compile-time call path, which is as long as the program makes it, so the
// driver runs the passes on a thread with a stack of known size
// (RunOnCompilerStack in main.cpp) and sets this floor some headroom above its
// end: below it, a check reports the program as too deep rather than let the
// stack overflow. Zero on any other thread, which nothing checks.
inline thread_local uintptr_t stackfloor = 0;

inline uintptr_t StackPointer() {
    // The frame's address rather than a local's, which a sanitizer may move
    // to a stack of its own.
    #ifdef _MSC_VER
        return (uintptr_t)_AddressOfReturnAddress();
    #else
        return (uintptr_t)__builtin_frame_address(0);
    #endif
}

inline bool StackLow() { return StackPointer() < stackfloor; }

// Reads a whole file; the returned string's c_str() gives the lexer its 0 terminator.
inline bool LoadFile(const string &path, string &dest) {
    auto f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    auto len = ftell(f);
    fseek(f, 0, SEEK_SET);
    dest.resize((size_t)len);
    auto read = fread(dest.data(), 1, (size_t)len, f);
    fclose(f);
    return read == (size_t)len;
}

// The stdlib modules with a native layer a program calls into, which a JIT
// run hands it from the compiler's own copies of them.
struct NativeLayers {
    bool audio = false;
    bool gfx = false;
    bool physics = false;
    bool ui = false;
};

// The response file of link inputs a program built from the generated C
// needs for a stdlib module with a native layer (gfx, physics, ui), which CMake
// writes: `style` is "msvc" for cl and clang-cl, "cc" for gcc and clang.
// Looked for in the directory `env` names, for a moved build tree, then in
// `<module>/` next to the compiler binary, for an installed one, then where
// CMake wrote it (`built`).
inline string NativeLinkFile(const string &flag, bool have, const char *missing,
                             const string &module, const char *env, const char *built,
                             const string &exedir, const string &style) {
    if (style != "msvc" && style != "cc")
        throw CompileError { cat(flag, " takes msvc or cc, not ", style) };
    if (!have) throw CompileError { missing };
    auto name = cat("link-", style, ".rsp");
    vector<string> dirs;
    if (auto dir = getenv(env)) dirs.push_back(dir);
    dirs.push_back(cat(exedir, module));
    if (built) dirs.push_back(built);
    for (auto &dir : dirs) {
        auto path = cat(dir, "/", name);
        if (auto f = fopen(path.c_str(), "rb")) {
            fclose(f);
            return path;
        }
    }
    throw CompileError { cat("cannot find ", name, " (set ", env, " to its directory)") };
}

}  // namespace goose
