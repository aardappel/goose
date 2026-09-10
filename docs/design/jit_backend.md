# The in-process C backend (JIT mode)

How the TinyCC backend is put together, and what it cannot do yet.

## What it is

The compiler's normal output is C, written to a file for another compiler to
build. Where the `third_party/tinycc` submodule was checked out at configure
time, the compiler also embeds TinyCC, and giving no `-o` compiles that same C
into the compiler's own process and calls its `main` there. No file is written
and no C toolchain has to be installed.

    goose 01_tour.goose            # compiled and run in one process
    goose -o tour.c 01_tour.goose  # the C file, as before
    goose --jit prog.goose         # ask for it explicitly; fails if not built in

Nothing about the generated C differs between the two. `-D` writes its define
into that C rather than passing it to a backend, so a JIT run and a compiled one
see the same text down to the byte. `--` passes the arguments after it to the
program. The program shares the process: its exit status becomes the compiler's,
its output goes to the same streams, and the compiler's own progress lines move
to stderr so stdout belongs to the program alone.

## How it is built

TinyCC has a configure script and a makefile, neither reachable from CMake on
all three platforms, so `cmake/libtcc.cmake` produces what they would have:

* a `config.h` with the version, the target, and the machine triplet Debian
  family systems need to find the C library;
* the library itself, as the single translation unit TinyCC's `ONE_SOURCE`
  build uses;
* the `tcc` driver, whose only job here is the next item: parts of the support
  library are TinyCC assembly, which only TinyCC assembles;
* `libtcc1.a`, the compiler support routines every TinyCC-compiled program
  links against, built by that driver from TinyCC's own per-target object list
  minus the pieces only a standalone executable or DLL needs.

Those, TinyCC's runtime headers, and on Windows the `.def` files naming what the
system DLLs export, are staged into one directory, which is what the compiler
hands `tcc_set_lib_path()`. `GOOSE_TCCLIB` overrides it; a `tcclib` directory
beside the compiler binary wins over the build tree it was configured in.

`-DGOOSE_LIBTCC=OFF` builds without the backend even with the submodule present,
which is also what a checkout without the submodule gets. That compiler writes
the `.c` file when given no `-o`, as it always did.

## Testing

`test/run_tests.py` and `samples/run_samples.py` run every program a second way
through the backend and compare it against the same blessed output; the
benchmark harness measures every Goose row a second way and reports those in a
table of their own. See `docs/testing.md`.

The point is not that TinyCC is fast. It is a third, very different C
implementation reading the generated C, and it disagrees where the C is
accidentally specific to one compiler or one C library. Two runtime bugs came
out of the first run: the wall clock used C11's `timespec_get`, which TinyCC's
Windows C library does not have and glibc hides from a compiler announcing C99;
and floats printed as `1e+016` under the older Microsoft C runtime TinyCC links,
where C99 asks for two exponent digits. Both were the text form of a Goose value
depending on the backend underneath, which it must not.

## Follow-up work

Not done, in rough order of how much they cost the project.

### Threads

A program that spawns a worker or uses a queue (§11.2) is refused under this
backend. TinyCC's in-memory runner rejects any section with `SHF_TLS`
(`tccrun.c`: *thread-local storage not supported with -run*), and the runtime
puts each thread program's globals pointer and its data-stack region registry in
thread-local storage whenever `GS_NEED_THREADS` is set. This costs seven test
files and one sample; everything else runs.

Possible directions:

* **Emulate thread-local storage in the runtime under `__TINYC__`**, over
  `TlsAlloc`/`TlsGetValue` on Windows and `pthread_key_create`/
  `pthread_getspecific` elsewhere. Self-contained, perhaps thirty lines, but it
  turns every access to the globals pointer into a call, and the region registry
  is read by the guard-page fault handler, where `pthread_getspecific` is not
  formally async-signal-safe. Measure the cost on the benchmarks before
  committing to it.
* **Teach TinyCC's runner to allocate a thread-local block.** The right fix, and
  upstream work rather than ours.
* **Leave it refused.** The diagnostic already says to compile with `-o`.

Windows needs two more things whichever way this goes: TinyCC's bundled winapi
headers do not declare `SRWLOCK` or `CONDITION_VARIABLE`, and its `kernel32.def`
does not export the functions over them, so the runtime would need those
declarations under `__TINYC__` and the staged `.def` file would need the names.

### `log2` on Windows

TinyCC's bundled `math.h` defines `log2(x)` as `log(x) * log2(e)`, a few ulp off
the exact answers `test/stdlib_math.goose` compares against, so that file carries
a `no-jit` marker. glibc's `log2` is exact, so the marker also costs the coverage
on Linux, where it would have passed. Either scope the marker to a platform, or
put a corrected `math.h` ahead of TinyCC's in the staged include directory and
accept maintaining a patched third-party header.

### The sanitizer profile

The JIT runs are skipped under ASan/UBSan. The program is not itself
instrumented, and because TinyCC's `exit()` longjmps back into `tcc_run` rather
than terminating, the program's own heap allocations are still live when the
compiler exits, which LeakSanitizer reports against the compiler. Freeing them
would need a teardown entry point in the runtime that the JIT could call after
`tcc_run` returns. The compiled path already carries the full sanitizer
coverage, so this is worth little.

### Backtraces and bounds checking

`config.h` turns both of TinyCC's own off, because the support objects behind
them need the full Windows SDK headers TinyCC does not ship. A crash inside
generated code therefore takes the compiler process down with no symbolic stack.
They could be enabled on the ELF and Mach-O targets alone, where those objects
build without `windows.h`.

### macOS

The build follows TinyCC's per-target object lists and CI now checks the
submodule out on all three platforms, but the macOS path has not been run here.
Apple silicon is the least exercised of the three: Mach-O output, and a JIT that
has to ask the kernel for writable-then-executable memory. If it does not build,
the choice is between fixing the object list and checking the submodule out on
Linux and Windows only.

### Compilation is not cached

Every JIT run recompiles the runtime, about 1200 lines of C, along with the
program. The benchmark report measures that floor. TinyCC has no serialized form
for an in-memory result, so there is nothing cheap to do here.
