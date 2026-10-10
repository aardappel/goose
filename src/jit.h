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
#include <filesystem>
#endif
#ifdef GOOSE_HAVE_GFX
#include "gfx/gfx_api.h"
#endif
#ifdef GOOSE_HAVE_AUDIO
#include "audio/audio_api.h"
#endif
#ifdef GOOSE_HAVE_PHYSICS
#include "physics/physics_api.h"
#endif
#ifdef GOOSE_HAVE_UI
#include "ui/ui_api.h"
#endif
#ifdef GOOSE_HAVE_NGFX
#include "ngfx/ngfx_api.h"
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

// Register each optional native layer's own functions before relocation.
inline void AddAudioSymbols(TCCState *s) {
    #ifdef GOOSE_HAVE_AUDIO
        #define GS_AUDIO_SYMBOL(ret, name, params) tcc_add_symbol(s, #name, (const void *)&name);
        GS_AUDIO_API(GS_AUDIO_SYMBOL)
        #undef GS_AUDIO_SYMBOL
    #else
        (void)s;
    #endif
}

inline void AddGfxSymbols(TCCState *s) {
    #ifdef GOOSE_HAVE_GFX
        #define GS_GFX_SYMBOL(ret, name, params) tcc_add_symbol(s, #name, (const void *)&name);
        GS_GFX_API(GS_GFX_SYMBOL)
        #undef GS_GFX_SYMBOL
    #else
        (void)s;
    #endif
}

// The physics layer's functions (src/physics/physics_api.h), the same way.
inline void AddPhysicsSymbols(TCCState *s) {
    #ifdef GOOSE_HAVE_PHYSICS
        #define GS_PHYS_SYMBOL(ret, name, params) tcc_add_symbol(s, #name, (const void *)&name);
        GS_PHYS_API(GS_PHYS_SYMBOL)
        #undef GS_PHYS_SYMBOL
    #else
        (void)s;
    #endif
}

// The ui layer's functions (src/ui/ui_api.h), the same way.
inline void AddUiSymbols(TCCState *s) {
    #ifdef GOOSE_HAVE_UI
        #define GS_UI_SYMBOL(ret, name, params) tcc_add_symbol(s, #name, (const void *)&name);
        GS_UI_API(GS_UI_SYMBOL)
        #undef GS_UI_SYMBOL
    #else
        (void)s;
    #endif
}

// The ngfx layer's functions (src/ngfx/ngfx_api.h), the same way.
inline void AddNgfxSymbols(TCCState *s) {
    #ifdef GOOSE_HAVE_NGFX
        #define GS_NGFX_SYMBOL(ret, name, params) tcc_add_symbol(s, #name, (const void *)&name);
        GS_NGFX_API(GS_NGFX_SYMBOL)
        #undef GS_NGFX_SYMBOL
    #else
        (void)s;
    #endif
}

// Compiles `csrc` in memory and calls its main, returning what the program
// returned or exited with. `progargs` become the program's argv after argv[0].
// `layers` says which native layers the program calls into.
inline int RunJit(const string &csrc, const string &libpath, const string &progname,
                  const vector<string> &progargs, const NativeLayers &layers) {
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
    if (layers.audio) AddAudioSymbols(s);
    if (layers.gfx) AddGfxSymbols(s);
    if (layers.physics) AddPhysicsSymbols(s);
    if (layers.ui) AddUiSymbols(s);
    if (layers.ngfx) AddNgfxSymbols(s);
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
                  const vector<string> &, const NativeLayers &) {
    throw CompileError { "this compiler was built without the TinyCC backend; "
                         "check out third_party/tinycc and reconfigure, or pass -o" };
}

#endif

}  // namespace goose
