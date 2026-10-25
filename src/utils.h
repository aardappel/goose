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

// Where a diagnostic is: a file of the program (an index in Ast::sources), the
// 1-based line and byte column it starts at, and where it ends, one past its
// last byte. A column of 0 is a line alone; an end on line 0 is unknown.
struct DiagLoc {
    int fileidx = -1;
    int line = 0, col = 0;
    int endline = 0, endcol = 0;
};

// A compile error or warning as data: what the text the compiler prints is
// made of (RenderDiagnostic), and what `--diagnostics json` prints instead.
struct Diagnostic {
    bool warning = false;
    DiagLoc where;
    string message;
    // The lines after the message, innermost first: the instantiation chain
    // and the const for expansions an error was reached through, each with
    // the place it names where it names one.
    struct Related { DiagLoc where; string message; };
    vector<Related> related;
    string text;                // As the terminal shows it.
};

// The text the terminal shows for d: `file:line: error: message` (the message
// alone where it has no file), the source line it is on where the file's
// text is given, under that a caret at its column where asked, and each
// related line indented after.
inline string RenderDiagnostic(const Diagnostic &d, string_view filename, string_view source,
                               bool caret) {
    string s;
    auto &w = d.where;
    if (!filename.empty()) Append(s, filename, ":", w.line, ": ");
    Append(s, d.warning ? "warning: " : "error: ", d.message);
    if (!filename.empty() && w.line > 0 && source.data()) {
        auto p = source.data(), e = p + source.size();
        for (auto ln = 1; p < e && ln < w.line; p++) if (*p == '\n') ln++;
        auto end = p;
        while (end < e && *end != '\n' && *end != '\r') end++;
        Append(s, "\n", string_view(p, (size_t)(end - p)));
        if (caret && w.col > 0 && w.col - 1 <= end - p) {
            s += '\n';
            for (auto q = p; q < p + w.col - 1; q++) s += *q == '\t' ? '\t' : ' ';
            s += '^';
        }
    }
    for (auto &r : d.related) Append(s, "\n  ", r.message);
    return s;
}

// Whether diagnostics printed on stderr are colored (StderrColor, --color).
inline bool stderrcolor = false;

// Diagnostic text as a terminal shows it in color: on each line that starts
// with a place and `error:` or `warning:`, the place in bold, the word in
// red or magenta and the message in bold; a caret line in green.
inline string ColorDiagnostics(string_view text) {
    const char *bold = "\x1b[1m", *red = "\x1b[1;31m", *magenta = "\x1b[1;35m",
               *green = "\x1b[1;32m", *reset = "\x1b[0m";
    string s;
    for (size_t pos = 0; pos <= text.size();) {
        auto e = text.find('\n', pos);
        if (e == string_view::npos) e = text.size();
        auto line = text.substr(pos, e - pos);
        auto kind = string_view();
        size_t at = string_view::npos;
        for (auto k : { string_view("error: "), string_view("warning: ") }) {
            auto p = line.find(k);
            // At the start, or after the place's `: `.
            if (p != string_view::npos && (p == 0 || line.substr(p - 2, 2) == ": ") && p < at) {
                at = p;
                kind = k;
            }
        }
        auto caret = line.find_first_not_of(" \t");
        if (at != string_view::npos) {
            if (at) Append(s, bold, line.substr(0, at), reset);
            Append(s, kind[0] == 'e' ? red : magenta, kind, reset, bold, line.substr(at + kind.size()), reset);
        } else if (caret != string_view::npos && line[caret] == '^' &&
                   line.find_first_not_of("^~", caret) == string_view::npos) {
            Append(s, line.substr(0, caret), green, line.substr(caret), reset);
        } else {
            s += line;
        }
        if (e < text.size()) s += '\n';
        pos = e + 1;
    }
    return s;
}

// Prints diagnostic text on stderr, with a line break, colored where it is
// to be.
inline void PrintDiagnosticText(string_view text) {
    auto s = stderrcolor ? ColorDiagnostics(text) : string(text);
    fprintf(stderr, "%s\n", s.c_str());
}

// Thrown by the parser and the checker where Ast::cancel is set: the analysis
// a language server abandons for a newer one. Not a CompileError, so no
// tentative parse or recovery takes it for an error of the program.
struct Cancelled {};

// Thrown by the checker where a check cannot go on and has nothing to report
// that was not reported already: the rest of a body the parser lost a
// statement of (Block::brokenat, Dot::incomplete), or a call that would reuse
// a specialization whose check failed (FnSpec::failed). The body or root
// being checked ends there as it does on a CompileError, with no diagnostic.
struct Abandoned {};

// Compile errors (in the program being compiled) throw their text, caught in
// main, with the same as data where the error has a place in the program
// (bounds-check-free functions report every check kept at once; a driver's
// error, such as a file that cannot be opened, has none). The parser and the
// checker go on past errors where they can (Ast::Report); what is thrown
// ended the compile.
struct CompileError {
    string msg;
    vector<Diagnostic> diags;
};

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
// It reads to the end rather than the length a seek reports: a directory
// opens on POSIX systems, with no such length, and fails to read (EISDIR).
inline bool LoadFile(const string &path, string &dest) {
    auto f = fopen(path.c_str(), "rb");
    if (!f) return false;
    dest.clear();
    for (size_t got = 0;;) {
        dest.resize(std::max<size_t>(got * 2, 1 << 16));
        got += fread(dest.data() + got, 1, dest.size() - got, f);
        if (got < dest.size()) {
            dest.resize(got);
            break;
        }
    }
    auto ok = !ferror(f);
    fclose(f);
    return ok;
}

// A path's file name without its directory and extension.
inline string PathStem(const string &path) {
    auto base = path.substr(path.find_last_of("/\\") + 1);
    auto dot = base.find_last_of('.');
    return dot == string::npos ? base : base.substr(0, dot);
}

// A Goose source file, without the UTF-8 byte-order mark many Windows
// editors write at its start (spec §2).
inline void StripBom(string &s) {
    if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
}
inline bool LoadSource(const string &path, string &dest) {
    if (!LoadFile(path, dest)) return false;
    StripBom(dest);
    return true;
}

// Where a file's text comes from before the disk: the text for the path, or
// null for the disk's. The dump `--roundtrip` parses again is served this
// way, as an editor's unsaved buffers would be.
using SourceProvider = function<const string *(const string &path)>;

}  // namespace goose
