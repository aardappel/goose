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
    // %.17g roundtrips, but prefer the shortest form that still does.
    char buf[32];
    snprintf(buf, sizeof(buf), "%.15g", v);
    if (strtod(buf, nullptr) != v) snprintf(buf, sizeof(buf), "%.17g", v);
    s += buf;
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

}  // namespace goose
