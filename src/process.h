// Goose compiler — other processes. Starting a program and collecting what it
// prints, finding one on PATH, and setting this process's environment: the
// compiler's one window onto the tools it calls (a C compiler, cmake, git).
// Implemented in process.cpp over third_party/subprocess/subprocess.h, a
// translation unit of its own, so that no platform header reaches the
// compiler's own.
#pragma once

namespace goose {

struct ProcessResult {
    bool started = false;   // False: the program could not be run, and `output` says why.
    int code = -1;          // Its exit code, once started.
    string output;          // What it wrote to stdout and stderr, interleaved as it came.
};

// Runs argv (argv[0] the program: a path, or a name looked up on PATH) in
// `cwd`, or in this process's working directory when that is empty, with
// this process's environment, and collects what it prints. Its stdin is
// closed immediately: no input is sent. With `echo` the
// output is also written to stderr as it arrives, for -v; either way the
// live line (progress.h) sees it.
ProcessResult RunProcess(const vector<string> &argv, const string &cwd = "", bool echo = false);

// The full path of the program `name` on PATH, or on `path` (a list in PATH's
// form) where that is given, or "" when there is none. On Windows a name
// without an extension is tried with the ones PATHEXT lists (.exe, .cmd,
// .bat, ...). A name holding a path separator is returned as it is when it
// exists.
string FindOnPath(const string &name, const char *path = nullptr);

// Sets a variable in this process's environment, where getenv and every
// process started from here see it.
void SetEnvVar(const string &name, const string &value);

// An exclusive cross-process lock on a persistent file. The OS releases it
// when this object (or the process) exits; children do not inherit it. The
// file must not be removed, since waiters may already have it open. While
// another process holds it, the live line (progress.h) says `waiting`.
class FileLock {
    intptr_t handle = -1;
public:
    FileLock(const string &path, const string &waiting);
    ~FileLock();
    FileLock(const FileLock &) = delete;
    FileLock &operator=(const FileLock &) = delete;
};

// Loads the shared library at `path`, a package's JIT library, and reads
// the table its goose_symbols function hands out (native.h): the names of
// the package's C functions and their addresses, appended to `out`. False,
// with `err` saying why, when it cannot be loaded or has no such table.
bool LoadSharedSymbols(const string &path, vector<pair<string, const void *>> &out, string &err);

// The macOS SDK this machine has active, where the C library's headers are;
// "" elsewhere, or where none is installed.
string MacSdkPath();

// argv as one line a shell would read back as argv, for printing a command
// (-v, --dry-run): arguments with spaces or shell characters are quoted.
string CommandLineText(const vector<string> &argv);

}  // namespace goose
