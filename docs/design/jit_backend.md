# The in-process C backend (JIT mode)

<details>
<summary>Table of contents</summary>

- [What it is](#what-it-is)
- [How it is built](#how-it-is-built)
- [Testing](#testing)
- [Follow-up work](#follow-up-work)

</details>

This note describes how the TinyCC backend works and its remaining
limitations.

## What it is

The compiler's normal output is C, written to a file for another compiler to
build. The compiler also embeds TinyCC. Without `-o`, it compiles that same C
into its own process and calls the program's `main` there. No file is written
and no C toolchain has to be installed.

    goose 01_tour.goose            # compiled and run in one process
    goose -o tour.c 01_tour.goose  # the C file, as before
    goose --jit prog.goose         # the same, asked for explicitly

A program whose only C is a shared library bound by name (an
`extern "SDL2" { ... }` block, spec §7.10) needs no toolchain either: the
runtime opens the library when the program starts, under this backend as in
a built program. Only a package or program with C of its own to compile
needs one.

Both modes generate identical C for the program. JIT and default `-o` output
declare most of the runtime extern, keeping the inline helpers in the
generated unit. AOT links the runtime object produced by `--emit-runtime`
(`docs/implementation.md` §7); JIT binds those functions to the runtime built
into Goose. `--standalone` still writes the whole runtime into an AOT unit.
`-D` writes a define into the generated C source. `--` passes the remaining
arguments to the program.

The program shares the process: its exit status becomes the compiler's, its
output goes to the same streams, and the compiler's own progress lines move
to stderr so that stdout carries only the program's output. It does not
share the compiler's stack: the compile runs on a thread of its own, and the
program on the main thread. On Windows and macOS the compiler is linked to
reserve 64 MB for the main thread's stack, as much as the compile gets,
whereas an executable built from the same C starts with 1 MB on Windows and
8 MB on macOS. On Linux both have the stack limit the process started with
(`ulimit -s`).

## How it is built

TinyCC's sources are vendored under `third_party/tinycc`, whose `README.md`
records the upstream commit; every build of the compiler includes them.
TinyCC has a configure script and a makefile, and CMake can use neither of
them on all three platforms, so `cmake/libtcc.cmake` produces what they would
have produced:

* a `config.h` with the version and paths within the bundled directory;
* the library itself, as the single translation unit TinyCC's `ONE_SOURCE`
  build uses;
* the `tcc` driver, which is used here only to build the next item, because
  parts of the support library are written in TinyCC assembly, which only
  TinyCC can assemble;
* `libtcc1.a`, the compiler support routines every TinyCC-compiled program
  links against, built by that driver from TinyCC's own per-target object
  list, leaving out the pieces that only a standalone executable or DLL
  needs.

These, TinyCC's runtime headers and, on Windows, its import definitions are
staged into one directory. Packagers can set `GOOSE_TCC_LIB_PATH` in CMake
to their installation directory. `GOOSE_TCCLIB` overrides it at run time,
and a `tcclib` directory beside the compiler binary takes precedence over
the configured path. Ordinary JIT programs use only the bundled `tccdefs.h` and
`libtcc1.a`; their small C declaration surface is embedded. Explicitly
included C can use the additional staged headers.

`RunJit` disables default include and library paths, registers the host
runtime's symbols, explicitly loads the bundled support archive, relocates
the generated unit, and calls its entry point through the hosted runtime.
System headers, SDKs, and the platform's dynamic-loading interfaces are
resolved when Goose itself is built. Runtime startup supplies the program's
arguments and resource root; teardown releases its regions and restores the
host's fault handlers.

## Testing

`test/run_tests.py` and `samples/run_samples.py` run every program a second
time, through this backend, and compare its output against the same expected
output. The benchmark harness also measures every Goose row through this
backend and reports those results in a table of their own. See
`docs/testing.md`.

TinyCC provides a third C implementation for testing the generated code. It can
expose accidental dependencies on another compiler or C library. The first run
found two runtime bugs. The wall clock used C11's `timespec_get`, which
TinyCC's Windows C library does not have and which glibc hides from a
compiler that announces C99. And floats printed as `1e+016` under the older
Microsoft C runtime that TinyCC links against, whereas C99 asks for two
exponent digits. Both bugs made a Goose value's text representation depend on
the backend, contrary to the language's requirements.

## Follow-up work

Remaining limitations and possible improvements, roughly ordered by their
impact on the project.

### Included C

The hosted runtime calls the relocated entry point directly. Included C
constructor/destructor and `atexit` hooks still need integration with this
entry/exit path. Its staged standard-header aliases expose only the C
declarations used by Goose, so arbitrary C depending on additional libc
functions or SDK headers needs an explicit interface.

### Threads

A program that spawns a worker or uses a queue (§11.2) is refused under this
backend. TinyCC's in-memory runner rejects any section with `SHF_TLS`
(`tccrun.c`: *thread-local storage not supported with -run*), and the runtime
puts each thread program's globals pointer in thread-local storage whenever
`GS_NEED_THREADS` is set. The native runtime's TLS is supported by its host
compiler, but the generated unit still needs TinyCC TLS support. The test
runner reports the skipped fixtures and sample.

Possible directions:

* **Emulate thread-local storage in the runtime under `__TINYC__`**, over
  `TlsAlloc`/`TlsGetValue` on Windows and `pthread_key_create`/
  `pthread_getspecific` elsewhere. This is self-contained, perhaps thirty
  lines, but it turns every access to the globals pointer into a call. Also,
  the fault handler reads the region registry and the thread's stack bounds,
  and `pthread_getspecific` is not formally async-signal-safe there. Measure
  the cost on the benchmarks before committing to it.
* **Teach TinyCC's runner to allocate a thread-local block.** This would address the limitation in
  TinyCC itself and would require upstream work.
* **Leave it refused.** The diagnostic already says to compile with `-o`.

### Native math

The embedded JIT binds math functions from the host C runtime, including
`log2` on Windows. `test/stdlib/stdlib_math.goose` allows small rounding
differences in transcendental results across platforms and runs on both
native and JIT backends without a `no-jit` marker.

### The sanitizer profile

The JIT runs are skipped under ASan/UBSan. The program itself is not
instrumented. The hosted runtime releases its regions and generated
stack-pointer array after normal return or Goose exit/abort, but this path
has not been validated under sanitizers. The compiled path has sanitizer
coverage.

### Backtraces and bounds checking

`config.h` turns off TinyCC's own backtraces and bounds checking, because the
support objects behind them need the full Windows SDK headers, which TinyCC
does not ship. A crash inside generated code therefore takes the compiler
process down with no symbolic stack. Both could be enabled on the ELF and
Mach-O targets only, where those objects build without `windows.h`.

### macOS

The build follows TinyCC's per-target object lists and CI builds it on all
three platforms, but the macOS path has not been run here. Apple silicon is
the least exercised of the three: it needs Mach-O output, and a JIT that has
to ask the kernel for writable-then-executable memory.

### Compilation is not cached

Every JIT run recompiles the generated program and inline runtime helpers.
The rest of the runtime is already compiled into Goose. TinyCC has no
serialized form for an in-memory result, so caching would require
substantial work.
