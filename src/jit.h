// The in-process C backend: instead of writing the generated C out for
// another compiler to build, hand it to TinyCC, which compiles it into this
// process's memory and calls its main. That is what happens when no `-o` is
// given ("JIT mode"). TinyCC is vendored under third_party/tinycc and built
// into every compiler.

#pragma once

#include "libtcc.h"
#include <filesystem>

namespace goose {

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

// TinyCC reports through a callback rather than a return value, so its
// diagnostics are collected and raised as one CompileError.
inline void JitDiag(void *opaque, const char *msg) {
    Append(*(string *)opaque, msg, "\n");
}

// Compiles `csrc` in memory and calls its main, returning what the program
// returned or exited with. `progargs` become the program's argv after argv[0].
// `symbols` holds the C functions of the packages' native parts the program
// reaches, by name (native.h, builtin.h): from the tables compiled in with
// builtin packages, or loaded from the libraries built for the others.
inline int RunJit(const string &csrc, const string &libpath, const string &progname,
                  const vector<string> &progargs,
                  const vector<pair<string, const void *>> &symbols) {
    string diags;
    auto s = tcc_new();
    if (!s) throw CompileError { "libtcc: out of memory" };
    tcc_set_error_func(s, &diags, JitDiag);
    // Both the header search path and the library search path are derived from
    // this, so it has to be set before the output type that reads them.
    tcc_set_lib_path(s, libpath.c_str());
    #ifdef __APPLE__
        // The C library's headers are only in an SDK. The output type adds
        // the include paths TinyCC was configured with after these: its own
        // headers, then the SDK of the machine the compiler was built on.
        // This machine's active SDK goes first, behind TinyCC's headers.
        if (auto sdk = MacSdkPath(); !sdk.empty()) {
            tcc_add_sysinclude_path(s, cat(libpath, "/include").c_str());
            tcc_add_sysinclude_path(s, cat(sdk, "/usr/include").c_str());
        }
    #endif
    auto fail = [&](const char *what) {
        auto msg = cat("libtcc: ", what, "\n", diags);
        tcc_delete(s);
        throw CompileError { msg };
    };
    if (tcc_set_output_type(s, TCC_OUTPUT_MEMORY) < 0) fail("cannot target memory");
    #ifdef __linux__
        // dlopen, for the runtime's dynamic externs: in libdl before glibc
        // 2.34 and in libc since, so a system without the archive is one
        // whose libc has the function, and a failure here is nothing.
        tcc_add_library(s, "dl");
        diags.clear();
    #endif
    // The in-process program's assets belong to its entry source, not to
    // the Goose executable. This define is JIT-only: emitted AOT C keeps no
    // build-machine resource path. Resolve now, without changing cwd.
    std::error_code patherror;
    auto root = filesystem::absolute(filesystem::path(progname), patherror).parent_path();
    if (patherror) fail("cannot locate the entry source directory");
    auto bytes = root.generic_u8string();
    string source = "#define GS_JIT_RESOURCE_DIR \"";
    // Fixed-width octal escapes are unambiguous even for quotes, control
    // characters, UTF-8 and a hex digit immediately after an escaped byte.
    for (auto ch : bytes) {
        char escaped[5];
        snprintf(escaped, sizeof escaped, "\\%03o", (unsigned)(uint8_t)ch);
        source += escaped;
    }
    if (bytes.empty() || bytes.back() != u8'/') source += '/';
    source += "\"\n";
    source += csrc;
    if (tcc_compile_string(s, source.c_str()) < 0) fail("compiling the generated C failed");
    for (auto &[name, addr] : symbols) tcc_add_symbol(s, name.c_str(), addr);
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

}  // namespace goose
