// The in-process C backend: instead of writing the generated C out for
// another compiler to build, hand it to TinyCC, which compiles it into this
// process's memory and calls its main. That is what happens when no `-o` is
// given ("JIT mode"). TinyCC is vendored under third_party/tinycc and built
// into every compiler.

#pragma once

#include "libtcc.h"
#include "jit_runtime.h"
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
    if (auto f = fopen(cat(beside, "/include/tccdefs.h").c_str(), "rb")) {
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
    tcc_set_options(s, "-nostdinc -nostdlib -Wl,-nostdlib");
    tcc_add_sysinclude_path(s, cat(libpath, "/goose/include").c_str());
    tcc_add_sysinclude_path(s, cat(libpath, "/include").c_str());
    auto fail = [&](const char *what) {
        auto msg = cat("libtcc: ", what, "\n", diags);
        tcc_delete(s);
        throw CompileError { msg };
    };
    if (tcc_set_output_type(s, TCC_OUTPUT_MEMORY) < 0) fail("cannot target memory");
    // TinyCC's compiler helpers come only from this build's bundled archive.
    #ifdef _WIN32
        auto support = cat(libpath, "/lib/libtcc1.a");
    #else
        auto support = cat(libpath, "/libtcc1.a");
    #endif
    for (auto p = goose_jit_symbols(); p->name; ++p)
        tcc_add_symbol(s, p->name, p->address);
    for (auto &[name, addr] : symbols) tcc_add_symbol(s, name.c_str(), addr);
    if (tcc_compile_string(s, csrc.c_str()) < 0) fail("compiling the generated C failed");
    if (tcc_add_file(s, support.c_str()) < 0) fail("loading compiler support failed");
    if (tcc_relocate(s) < 0) fail("relocating the generated C failed");
    auto entry = (int (*)(int, char **))tcc_get_symbol(s, "main");
    auto cleanup = (void (*)(void))tcc_get_symbol(s, "gs_jit_cleanup");
    if (!entry || !cleanup) fail("cannot find the program entry points");
    std::error_code patherror;
    auto root = filesystem::absolute(filesystem::path(progname), patherror).parent_path();
    if (patherror) fail("cannot locate the entry source directory");
    auto bytes = root.generic_u8string();
    string resource_dir((const char *)bytes.data(), bytes.size());
    if (resource_dir.empty() || resource_dir.back() != '/') resource_dir += '/';
    // The runtime hands these to the program's main, which takes them as C main
    // does: an array of writable pointers. Hence the mutable copies.
    auto name = progname;
    auto args = progargs;
    vector<char *> argv;
    argv.push_back(name.data());
    for (auto &a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    auto code = goose_jit_run(entry, cleanup, (int)argv.size() - 1, argv.data(),
                              resource_dir.c_str());
    tcc_delete(s);
    return code;
}

}  // namespace goose
