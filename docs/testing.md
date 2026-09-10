# Focused compiler/runtime coverage

Run `python test/run_tests.py --profile baseline --cc native --require-clang`
after a normal compiler build. CI runs this on Windows, macOS and Linux.

| Configuration | Purpose |
|---|---|
| Debug Goose compiler; Goose `-O0` / native C `-O0` versus Goose `-O2` / native C `-O2` | Compare actual unoptimized and optimized executables, including native optimizer effects; check output against the existing fixtures. |
| Goose `-O2`, native C `-O2`, `GS_DEBUG=1`, selected fixtures | Exercise checked arithmetic/conversions and critical runtime paths with optimization enabled. Selection is `codegen_exec.goose` plus files with `runtime-debug` on their first line. |
| Additional Clang C build of `codegen_exec.goose`, native `-O1`, release/debug runtime | Keep a second C-front-end check on the central codegen coverage file without repeating the whole suite for every backend. |
| Goose `-O0` and `-O2` through the in-process TinyCC backend, plus `GS_DEBUG=1` on the same selected fixtures, over every test and every sample | Check the generated C against a third, very different C implementation, and that a program means the same whichever backend builds it. |
| One Linux Clang ASan/UBSan job | Instrument the C++ compiler, generated C, selected debug-runtime cases, all samples and the direct runtime lifecycle test. |

The sanitizer job builds Goose with
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all`,
then runs `python test/run_tests.py --exe <instrumented-goose> --profile sanitize --cc clang`.
Generated C uses native `-O1` and disables only UBSan's alignment check because
the current C backend deliberately performs unaligned packed accesses. The C++
compiler retains alignment checking. ASan observes C allocations, but does not
know logical object boundaries inside Goose's custom virtual-memory arenas.

The direct `test/runtime_threads_lifecycle.c` regression checks allocations,
mappings and Windows handles across worker churn, including unjoined workers,
concurrent/repeated waits and children outliving their parents. It runs in both
profiles. The normal suite also checks user-visible worker error diagnostics.

The TinyCC runs need the `third_party/tinycc` submodule at configure time; a
compiler built without it skips them, and the runners find that out by running
a one-line program rather than by asking the build. They are also skipped under
the sanitizers, where the program runs uninstrumented inside an instrumented
compiler and still holds its runtime allocations when that compiler exits.
A test the backend cannot run yet is reported as a skip, not a failure: either
because the compiler refuses the program outright (`JIT mode does not support`)
or because the test's first line says `no-jit`, which is for the few cases
where TinyCC's own C library answers differently from the ones the expected
output was blessed against. What the backend cannot do yet, and what could be
done about it, is `docs/design/jit_backend.md`.

An explicitly requested compiler must exist; CI does not silently skip it.
Expected aborts use `.aborts` alongside normal `.out` expectations. Optional
`.stderr` files contain required diagnostic substrings, also supported for
parser/typechecker error fixtures. Sanitizer reports fail even when a program
was expected to abort, so an unrelated crash cannot satisfy those regressions.

This is four CI jobs, rather than a product of platforms, sanitizers, compiler
optimization levels and runtime modes. Benchmarks remain separate from CI.
