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
or because the test's first line says `no-jit`. Prefer portable assertions to
skipping a backend; the math tests allow small rounding differences in
transcendental results. What the backend cannot do yet, and what could be
done about it, is `docs/design/jit_backend.md`.

An explicitly requested compiler must exist; CI does not silently skip it.
Every runnable fixture requires an `expected/<name>.out`, including an empty
file when the program should be silent. Expected aborts additionally require
`.aborts` and a nonempty `.stderr` containing the expected runtime diagnostic
substrings. Sanitizer reports fail even when a program was expected to abort.

Parser/resolver errors live in `test/errors/`; semantic errors live in
`test/errors_tc/` and must first pass `--parse`. Each source declares one or
more `// error: <diagnostic substring>` lines. The compiler must exit with 1,
and all markers must occur in diagnostic headers, excluding echoed source.
Use the specific rejection reason and relevant types/roots; omit source paths,
line numbers and specification section numbers. These inline assertions replace
the old compiler-error `.stderr` files.

The runner checks more than exit status and runtime output:

| Check | Contract |
|---|---|
| `lexer_tokens.goose` | Exact token stream, including keyword classification, decoded literals and longest-match punctuation. |
| Every positive Goose fixture | Successful parse, successful initial dump, identical dump/reparse/dump, and typechecking unless its first line contains `parse-only`. Parsing also resolves type names; dumping alone does not. |
| First-line `dump-runtime` | Compile and execute the dumped source against the original output. `control_expression_dump.goose` uses this to check grouping semantics, which a stable dump alone cannot establish. |
| Every fixture with `// bce:elide` or `// bce:keep` | Run `-O1 --check --bce-test`, including expected-abort regressions. Native/JIT O0 and O2 runs independently check behavior. |
| `optimize.goose` at O0/O1/O2 | Inspect named tail-recursion bodies in `--specs`: supported integer accumulator/plain recursion becomes loops; modulo, floating-point reassociation, nonlocal-return frames and returns inside nested loops retain self calls. Mixed operators retain the ineligible call. Leading locals prevent base-case inlining from consuming these cases first. |

The fixture audit retained the small lifetime, optional-narrowing, alias-cycle,
dispatch and frame-layout regressions. Similar diagnostics do not make them
duplicates: they exercise different expression visitors, specialization/cache
states, root propagation, or generated layouts. Whole-program globals and call
graphs are also part of many regressions, so combining them can change the
property under test.

The audit removed three redundant fixtures: `all_tests.goose` only re-ran seven
standalone suites (imported-main behavior remains covered by the namespace
tests); `spec_examples.goose` executed a word scanner already covered by
`typecheck.goose` and `bce.goose`, while its other declarations were unused
sketches; `errors_tc/shrink_live_slice.goose` duplicated the live-slice clear
rejection in `clear_live_slice.goose`. Dedicated tests retain recursive relative
structures, dispatch, nonlocal returns and dictionary execution previously
suggested by those unused sketches.

This is four CI jobs, rather than a product of platforms, sanitizers, compiler
optimization levels and runtime modes. Benchmarks remain separate from CI.
