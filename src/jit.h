// The optional in-process C backend: instead of writing the generated C out
// for another compiler to build, hand it to TinyCC, which compiles it into
// this process's memory and calls its main. That is what happens when no `-o`
// is given ("JIT mode").
//
// Present only when the third_party/tinycc submodule was checked out at
// configure time; everything below compiles to a "not built in" error
// otherwise, and the driver falls back to writing the .c file.

#pragma once

#ifdef GOOSE_HAVE_LIBTCC
#include "libtcc.h"
#endif

namespace goose {

#ifdef GOOSE_HAVE_LIBTCC
inline constexpr bool have_jit = true;
#else
inline constexpr bool have_jit = false;
#endif

// Where TinyCC's own headers and its libtcc1 support archive were staged: an
// explicit override for a moved build tree, next to the compiler binary for an
// installed one, and otherwise the directory CMake wrote them to.
inline string JitLibPath(const string &exedir) {
    if (auto env = getenv("GOOSE_TCCLIB")) return env;
    #ifdef GOOSE_TCC_LIB_PATH
        auto staged = string(GOOSE_TCC_LIB_PATH);
    #else
        auto staged = string();
    #endif
    auto beside = cat(exedir, "tcclib");
    // Installed next to the binary wins, so a copied build tree keeps working
    // even though the configure-time path still exists on this machine.
    if (auto f = fopen(cat(beside, "/include/stddef.h").c_str(), "rb")) {
        fclose(f);
        return beside;
    }
    return staged;
}

#ifdef GOOSE_HAVE_LIBTCC

// TinyCC reports through a callback rather than a return value, so its
// diagnostics are collected and raised as one CompileError.
inline void JitDiag(void *opaque, const char *msg) {
    Append(*(string *)opaque, msg, "\n");
}

// Compiles `csrc` in memory and calls its main, returning what the program
// returned or exited with. `progargs` become the program's argv after argv[0].
inline int RunJit(const string &csrc, const string &libpath, const string &progname,
                  const vector<string> &progargs) {
    string diags;
    auto s = tcc_new();
    if (!s) throw CompileError { "libtcc: out of memory" };
    tcc_set_error_func(s, &diags, JitDiag);
    // Both the header search path and the library search path are derived from
    // this, so it has to be set before the output type that reads them.
    tcc_set_lib_path(s, libpath.c_str());
    auto fail = [&](const char *what) {
        auto msg = cat("libtcc: ", what, "\n", diags);
        tcc_delete(s);
        throw CompileError { msg };
    };
    if (tcc_set_output_type(s, TCC_OUTPUT_MEMORY) < 0) fail("cannot target memory");
    if (tcc_compile_string(s, csrc.c_str()) < 0) fail("compiling the generated C failed");
    // tcc_run hands these to the program's main, which takes them as C main
    // does: an array of writable pointers. Hence the mutable copies.
    auto name = progname;
    auto args = progargs;
    vector<char *> argv;
    argv.push_back(name.data());
    for (auto &a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    diags.clear();
    auto code = tcc_run(s, (int)argv.size() - 1, argv.data());
    // A run that never started reports through the callback; a program that
    // genuinely returns -1 does not.
    if (code == -1 && !diags.empty()) fail("running the generated C failed");
    tcc_delete(s);
    return code;
}

#else

inline int RunJit(const string &, const string &, const string &,
                  const vector<string> &) {
    throw CompileError { "this compiler was built without the TinyCC backend; "
                         "check out third_party/tinycc and reconfigure, or pass -o" };
}

#endif

}  // namespace goose
