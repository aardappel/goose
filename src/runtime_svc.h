// Goose compiler - the lower service layer the driver (main.cpp) and the
// subsystems (build.h, packages.h, lsp_*.h, format.h) share: a path's
// directory, a relative path between two of them, whether a file exists, the
// running executable's path, the process's stdio as byte streams, and the
// runtime's text (its hash names the runtime object and the shared library
// a package's native part builds into). The path and stdio functions are
// implemented in runtime_svc.cpp, a translation unit of its own (as
// process.cpp and progress.cpp are), so that the platform headers they need
// stay out of the compiler's own; the rest is header-only, over
// runtime_inline.h's embedded runtime text. The includes are explicit: a
// translation unit that wants the service layer without the whole compiler
// (runtime_svc.cpp) includes this header first of the project's.
#pragma once

#include "includes.h"
#include "utils.h"          // Append, cat.
#include "runtime_inline.h" // runtime_files, the embedded runtime's text.

namespace goose {

// In runtime_svc.cpp.
string DirOf(const string &path);
bool FileExists(const string &path);
string RelativeTo(const string &path, const string &dir);
string ExePath(const char *argv0);
void SetBinaryStdio();

// The runtime C sources embedded into the compiler (runtime_inline.h), in the
// order a standalone program holds them (runtime.h has how they divide), and
// goose.h, the header native parts compile against (native.h).
inline constexpr const char *runtimefiles[] = { "runtime.h", "runtime_impl.h", "runtime_threads.h",
                                            "runtime_ext.h", "runtime_os.h", "goose.h", "jit_c.h" };

inline string RuntimeSections(std::initializer_list<const char *> names) {
    string s;
    for (auto name : names)
        for (auto &rf : runtime_files)
            if (string_view(rf.name) == name)
                Append(s, "/* ==== ", rf.name, " ==== */\n", rf.text, "\n");
    return s;
}

// What a separately built program and its runtime object must agree on: a
// hash of the runtime's text, which names the runtime's entry point.
inline string RuntimeVersion() {
    uint64_t h = 14695981039346656037ull;
    for (auto &rf : runtime_files)
        for (auto c : string_view(rf.text)) h = (h ^ (uint8_t)c) * 1099511628211ull;
    char buf[17];
    snprintf(buf, sizeof(buf), "%012llx", (unsigned long long)(h >> 16));
    return buf;
}

// The runtime object's C (goose --emit-runtime): the runtime that programs
// written with -o declare extern, for any program this compiler writes.
inline string RuntimeObjectSource() {
    return cat("/* The Goose runtime, written by `goose --emit-runtime`. Do not edit.\n"
               "   Compile it once, with the C compiler and flags of the programs it is\n"
               "   for, and link the object with every program `goose -o` writes (all\n"
               "   but --standalone ones). It needs no -D: a program hands the runtime\n"
               "   its configuration as it starts. A program from another version of\n"
               "   the compiler fails to link against it, missing gs_rt_start_<version>. */\n\n"
               "#define GS_SEPARATE_RUNTIME 1\n#define GS_RUNTIME_OBJECT 1\n"
               "#define GS_NEED_THREADS 1\n#define GS_RUNTIME_VERSION ", RuntimeVersion(),
               "\n\n", RuntimeSections({ "runtime.h", "runtime_impl.h", "runtime_threads.h",
                                          "runtime_ext.h", "runtime_os.h" }));
}

}  // namespace goose
