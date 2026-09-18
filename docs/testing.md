# Focused compiler/runtime coverage

Run `python test/run_tests.py --profile baseline --cc native --require-clang`
after a normal compiler build. CI runs this on Windows, macOS and Linux.

Test fixtures are grouped by category; `run_tests.py` stays at the root of `test/`:

| Path under `test/` | Coverage |
|---|---|
| `syntax/` | Lexer, parser, dump grouping, imports and namespace resolution. Imported helpers live in `syntax/ns/` and `syntax/sub/`. `raw_strings_crlf.goose` is the one file checked out with CRLF line breaks (`.gitattributes`). |
| `typing/` | Types, generics, constants, optional narrowing, dispatch results and recursive return contracts. |
| `lifetimes/` | Reference roots, byte views, borrowed contents and shrinking while references are live. |
| `codegen/` | C names, evaluation order, representations, frame/stack layout and aliasing. |
| `optimizer/` | Inlining, tail recursion and bounds-check elimination. |
| `runtime/` | Runtime diagnostics, reusable slot and slice pool operations and array size checks. |
| `storage/` | Relative references, pools and serialization. |
| `threads/` | Workers, queues, shared globals and the native runtime lifecycle test. |
| `stdlib/` | Standard-library modules. |
| `gfx/` | The `gfx` graphics module: headless rendering, textures, compute, frames and input, a runtime misuse, shaders from files and from the program; shader and threading rejections (fixtures with `// error:` markers). The programs hold their shaders; the shader files beside them are for the file form of `embed_shader` and `--compile-shader`. `gfx/window/` is the windowed showcase, not part of the suite. |
| `errors/`, `errors_tc/` | Expected parser/resolver and semantic rejections. |
| `expected/` | Shared output and runtime-diagnostic expectations. |
| `run_tests.py` | The Python test runner. |
| `gfx_api_check.py` | Checks `stdlib/gfx.goose` against `src/gfx/gfx_api.h`; run by `run_tests.py`. |

Positive fixtures are discovered one level below `test/`; nested import helpers
run through their entry programs. Keep fixture stems unique across categories,
since expectations and generated build artifacts use those names. The runner
rejects duplicate names.

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

The direct `test/threads/runtime_threads_lifecycle.c` regression checks allocations,
mappings and Windows handles across worker churn, including unjoined workers,
concurrent/repeated waits and children outliving their parents. It runs in both
profiles. The normal suite also checks user-visible worker error diagnostics.

The `gfx/` tests exercise the SDL3 graphics module (`docs/design/gfx.md`). They
always parse, typecheck and generate C. Where the compiler has the gfx layer
built in (the `third_party/SDL` submodule, `goose --gfx-link` answering), they
also build, linking what that names, and run at -O0 and -O2 and through
TinyCC. They render headless into textures and read back only pixel-aligned
results, so their output is the same on every backend and GPU; a machine with
no GPU device (the program prints `gfx: no GPU device`) reports them skipped,
as does a compiler without the layer. Linux CI runs them on Mesa's lavapipe.
The runners set `GOOSE_GFX_HEADLESS=1`, so no test or sample opens a window.
`gfx_api_check.py` checks that `stdlib/gfx.goose` and the C layer's list of
its functions, structs and constants describe the same boundary, which
compiles on both sides when they do not; the hidden `--compile-shader` flag is
probed on `gfx/probe.frag`.

`test/gfx/window/run_window_test.py` is run by hand, on a machine with a
display and a GPU: it runs the showcase `gfx_showcase.goose` with a window
in-process and built by every C toolchain found, on each GPU driver of the
platform, checks what the program reports about its own readbacks, and decodes
the screenshots it saves (under `build/gfx-window/`) to check them for detail
and compare them across runs.

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
| `gfx_err_shader_syntax.goose`, `gfx_err_shader_part.goose` | Besides their markers, the error is reported at the program's line holding the offending GLSL. |
| Generated `call_chain.goose`, `call_chain_too_deep.goose` (`build/gen/<profile>/`) | A compile-time call path through 2000 distinct functions checks at `-O0`, and runs through TinyCC; one through 6000, each calling the next from 32 blocks deep, is rejected as too deep for the compiler's stack instead of overflowing it (`docs/implementation.md` §3.1). |
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
