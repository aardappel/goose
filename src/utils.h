// Goose compiler — project-wide utilities.
#pragma once

namespace goose {

// String building: small variadic append/cat helpers, no iostreams.
inline void CatOne(string &s, string_view v)   { s += v; }
inline void CatOne(string &s, const char *v)   { s += v; }
inline void CatOne(string &s, const string &v) { s += v; }
inline void CatOne(string &s, char v)          { s += v; }
inline void CatOne(string &s, bool v)          { s += v ? "true" : "false"; }
inline void CatOne(string &s, int64_t v)       { s += to_string(v); }
inline void CatOne(string &s, uint64_t v)      { s += to_string(v); }
inline void CatOne(string &s, int v)           { s += to_string(v); }
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
