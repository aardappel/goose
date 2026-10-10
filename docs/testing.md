# Compiler and runtime tests

Run `python test/run_tests.py --profile baseline --cc native --require-clang`
after a normal compiler build. CI runs this on Windows, macOS and Linux.

The runners work on `-j`/`--jobs` things at once (by default one per logical
processor, up to 32), on threads that mostly wait for the compiler, the C
compiler and the programs they start. The log comes out in the same order
whatever the number of jobs: each piece of work prints into a buffer of its
own, which shows once everything before it has, so two logs diff cleanly.
`-j1` runs everything in order on one thread. Everything one fixture runs
happens one run after another, since a program may write a file
(`stdlib_os.goose` does) that another run of it would see. The Goose-in-Goose
chains and the samples' runner (`samples/run_samples.py`, a process of its own
with as many jobs) start first: the -O0 bootstrap chain is the longest piece
of work, and the suite takes about as long as it does. `--gpu-jobs N` limits
how many gfx programs run at once within each runner, should a GPU driver not
take many headless devices at a time; by default nothing limits them.

The compiler runs per fixture are as few as the checks allow. One run,
`goose --roundtrip --check`, parses, checks that the dump parses again to
the same dump, and typechecks (with `--bce-test` where the fixture has
annotations, `--parse` instead of `--check` for a `parse-only` one, and
`--dump-file` writing the dump for a `dump-runtime` one). Every run that gets
past resolution prints `parsed ok`, which tells a parse failure from a
typecheck failure in one `--check` of an `errors_tc/` fixture. Where a level
both builds the C and runs the program through TinyCC, one `goose -O<n> --jit
-o <file>` does both: it writes the C, then runs the program in-process.

Most of those runs, and those of the `errors/` and `errors_tc/` fixtures,
share a compiler process with others: `goose --multi-test <flags> a.goose
b.goose ...` compiles each file in turn as a run on it alone would, and ends
each file's part of stdout and of stderr with a line `==== goose
--multi-test: exit <code> <file>`, from which the runner hands every fixture
exactly the exit code and output a run of its own would have given. A
compiler process starting costs about as much as checking a small fixture,
so a batch of 20 checks in a tenth of the time. A batch that crashes or
aborts gets through the files before the one it was on; that file and the
rest run one to a process, so a crash is reported as that fixture's. Runs
writing a dump (`dump-runtime`), with `runtime-define` defines, or running a
program (TinyCC) stay one to a process. The C of the fixtures not linking a
native layer builds a batch at a time as well, at each level and for the
dump and `GS_DEBUG` programs: one C compiler run compiles each program's
unit with its `main` renamed, beside a `main` that runs the program its
first argument names, and links them once against the runtime object
(`CC.compile_programs` in `scripts/toolchain.py`). Every run of a program is
still a process of its own. Where a batch does not build, each program in it
builds on its own and shows why. A C compiler run's start and link cost
most of its time on these programs: a batch of 20 builds in a fifth of the
time. `--batch N` sets how many files go to one compiler process and how
many programs to one executable (by default a number that leaves every job
some batches); `--batch 1` gives each a process and an executable of its own.

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
| `goose_in_goose/` | A Goose-written compiler used as one multi-file bootstrap regression: build and run three generations, check a C fixed point and repeated self-checks of the stage-2 compiler, and compare JIT/native self-compilation. |
| `gfx/` | The `gfx` graphics module: headless rendering, textures, compute, frames and input, a runtime misuse, shaders from files and from the program; shader and threading rejections (fixtures with `// error:` markers). The programs hold their shaders; the shader files beside them are for the file form of `embed_shader` and `--compile-shader`. `gfx/window/` is the windowed showcase, not part of the suite. |
| `ngfx/` | The experimental `ngfx` module: headless rendering, textures and mips, compute, frames and input, a runtime misuse, Slang from files and from the program; `embed_slang` and threading rejections. Each test also runs under `GOOSE_NGFX_SYNC=full`, which must not change its output. |
| `physics/` | The `physics` module: worlds, bodies, every kind of shape and geometry, all joint kinds, queries, events, recording and replay, a runtime misuse, and the threading rejection. |
| `ui/` | The `ui` module: fonts, windows and layout, widgets, popups and menus, text editing, style, drawing and the input queries, charts, rendering and input through headless gfx, whether the keys and the mouse are the ui's, a runtime misuse, the layer's misuse messages, and the threading rejection. |
| `errors/`, `errors_tc/` | Expected parser/resolver and semantic rejections. |
| `expected/` | Shared output and runtime-diagnostic expectations. |
| `run_tests.py` | The Python test runner. |
| `compiler_roots.cpp` | Root-set join laws, feedback on weakening, order-independent equality and the deliberate comparison projections; compiled and run in both native profiles. |
| `resource_paths.py` | JIT entry-source resource roots, native executable roots with separate and standalone runtimes, relocation, and unchanged working-directory file access. Called by the main runner. |
| `api_check.py` | Checks `stdlib/gfx.goose`, `stdlib/physics.goose` and `stdlib/ui.goose` against their C layers' headers; run by `run_tests.py`. |

Positive fixtures are discovered one level below `test/`; nested import helpers
run through their entry programs. `goose_in_goose/` is handled separately as one
bootstrap program, rather than discovering its modules as standalone fixtures.
Keep fixture stems unique across categories,
since expectations and generated build artifacts use those names. The runner
rejects duplicate names.

| Configuration | Purpose |
|---|---|
| Debug Goose compiler; Goose `-O0` / native C `-O0` versus Goose `-O2` / native C `-O2` | Compare actual unoptimized and optimized executables, including native optimizer effects; check output against the existing fixtures. |
| Goose `-O2`, native C `-O2`, `GS_DEBUG=1`, selected fixtures | Exercise checked arithmetic/conversions and critical runtime paths with optimization enabled. Selection is `codegen_exec.goose` plus files with `runtime-debug` on their first line. |
| Additional Clang C build of `codegen_exec.goose`, native `-O1`, release/debug runtime | Keep a second C-front-end check on the central codegen coverage file without repeating the whole suite for every backend. |
| Goose `-O0` and `-O2` through the in-process TinyCC backend, plus `GS_DEBUG=1` on the same selected fixtures, over every test and every sample | Check the generated C against a third, very different C implementation, and that a program means the same whichever backend builds it. |
| One Linux Clang ASan/UBSan job | Instrument the C++ compiler, generated C, selected debug-runtime cases, all samples and the direct runtime lifecycle test. |
| Goose-in-Goose bootstrap, both profiles | Build stage 1 with the real compiler, stage 2 with stage 1, and stage 3 with stage 2. Build all three executables, compare stage-2/stage-3 C, and run two self-checks in stage 2, which the fixed point makes the same compiler as stage 3, while stage 3 is emitted and built. Baseline requires equal self-compiled C across host/native O0 and O2 and JIT/native execution; sanitize instruments every native stage. |

Run just the bootstrap with
`python test/run_tests.py --goose-in-goose-only --exe <goose> --cc native`.
The usual `--profile`, `--no-jit`, and `--nocgen` options apply. With no native
C compiler, it still checks the source and, when available, self-compiles through
the JIT, but reports the native chain skipped. Sources and the exact stage
contract are in [goose_in_goose/README.md](../test/goose_in_goose/README.md);
artifacts and logs go in `build/gen/<profile>/goose_in_goose/O<level>/`.

The sanitizer job builds Goose with
`-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all`,
then runs `python test/run_tests.py --exe <instrumented-goose> --profile sanitize --cc clang`.
Generated C uses native `-O1` and disables only UBSan's alignment check because
the current C backend deliberately performs unaligned packed accesses. The C++
compiler retains alignment checking. ASan observes C allocations, but does not
know logical object boundaries inside Goose's custom virtual-memory arenas.

Generated C is built as `goose -o` writes it, against the separate runtime
(`docs/implementation.md` §7): `GooseRuntime` (`scripts/toolchain.py`) has
the compiler under test write the runtime's C (`--emit-runtime`) once per run,
compiles it once per toolchain configuration -- compiler, optimization level,
defines and extra flags, so the sanitize profile's runtime is instrumented as
well -- into `build/gen/runtime/` (the samples' into their own directory), and
`CC.compile(..., runtime=...)` links that object with each program built that
way. Stages 2 and 3 of the bootstrap hold their own runtime and link none.
The single-unit form, which `--standalone` writes, is what every TinyCC run
compiles; the benchmarks build that form too.

The direct `test/threads/runtime_threads_lifecycle.c` regression checks allocations,
mappings and Windows handles across worker churn, including unjoined workers,
concurrent/repeated waits and children outliving their parents. It runs in both
profiles. The normal suite also checks user-visible worker error diagnostics.

The `gfx/` tests exercise the SDL3 graphics module (`docs/design/gfx.md`). They
always parse, typecheck and generate C. When the compiler includes the gfx layer
(built from the `third_party/SDL` submodule), the tests also link against the
libraries listed by `goose --gfx-link` and run at -O0 and -O2 and through
TinyCC. They render headless into textures and read back only pixel-aligned
results, so their output is the same on every backend and GPU; a machine with no
GPU device (the program prints `gfx: no GPU device`) reports them skipped, as
does a compiler without the layer. Linux CI runs them on Mesa's lavapipe. The
runners set `GOOSE_GFX_HEADLESS=1`, so no test or sample opens a window. The
hidden `--compile-shader` flag is probed on `gfx/probe.frag`.

The `physics/` tests exercise the Box3D physics module
(`docs/design/physics.md`) in the same way. They always generate C. When the
compiler includes the physics layer from `third_party/box3d`, they link using
`goose --physics-link` and run at -O0 and -O2 and through TinyCC. Box3D
is deterministic across platforms and worker counts, so they print physics
results, rounded, and compare them exactly. Between them they call every
function of the layer.

The `ui/` tests exercise the Nuklear ui module (`docs/design/ui.md`) in the
same way, linking `goose --ui-link` and, for the one that renders through
gfx, `goose --gfx-link` too: a test needs the layers of its own category and
of every module it imports, and is skipped where one is missing. They drive a
context with input of their own and print what comes back and what was
drawn; `ui_render` draws through headless gfx and reads back pixels only
where the ui drew solid color. Between them they call every function of the
layer.

`api_check.py` compares the functions, structs, and constants in
`stdlib/gfx.goose`, `stdlib/physics.goose` and `stdlib/ui.goose` with their C
headers. This catches
interface mismatches that can compile successfully on both sides. It also
rejects a struct the layer passes by value that TinyCC would pass differently
from the C compilers on System V x86-64: one of at most 16 bytes with a
misaligned field, or with floats alone in one eightbyte and integers in the
other.

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
transcendental results. See `docs/design/jit_backend.md` for the backend's remaining limitations and
possible solutions.

An explicitly requested compiler must exist; CI does not silently skip it.
Every runnable fixture requires an `expected/<name>.out`, including an empty
file when the program should be silent. Expected aborts additionally require
`.aborts` and a nonempty `.stderr` containing the expected runtime diagnostic
substrings. Sanitizer reports fail even when a program was expected to abort.
Resource-bound fixtures can set a limit in their first line, such as
`// runtime-define: GS_MAX_STACKS=1`; the runner passes it as a `-D` to
every compile, where the compiler checks `GS_MAX_STACKS` against the
program's static stack count and the generated C takes the rest. `append_destination_storage.goose` uses this to detect
temporary result stacks that output-only comparisons would miss,
`cycle_scratch_locals.goose` to show that a recursion's scratch locals take
the same few stacks at every level, and `discarded_resizable_results.goose`
shrinks each stack's reservation (`GS_STACK_RESERVE`) so that its loops
overflow one unless every discarded result is released, as
`guard_flat_storage.goose` does for what a guard's condition, its else and
the rest of its block take, the rest emitted without braces of its own.
`cycle_static_stacks.goose` runs the shapes a call into a recursive cycle
allows (§7.8), the `errors_tc/cycle_temporary_*`, `cycle_construction_*` and
`cycle_result_*` fixtures being the rejected ones, and codegen fails with an
internal error should any program hold a data stack across such a call.
`thread_cap.goose` shrinks the address space budget (`GS_STACK_BUDGET`) so
that `hardware_threads()` reports its floor of one wherever it runs, and
`stack_stats.goose` has its program report its stack use
(`GS_STACK_STATS`), checked through `expected/stack_stats.stderr`. The
runner also checks the `--stacks` report of `thread_cap.goose` and
`cycle_scratch_locals.goose` for its shape, not its counts. A fixture
expecting a debug-only abort sets `GS_DEBUG=1` the same way, which makes
every run of it a debug build: `cast_abort_location.goose` and
`overflow_abort_location.goose` check the location a failing check reports.

Parser/resolver errors live in `test/errors/`; semantic errors live in
`test/errors_tc/` and must get past parsing and resolution (`parsed ok`)
before they fail. Each source declares one or
more `// error: <diagnostic substring>` lines. The compiler must exit with 1,
and all markers must occur in diagnostic headers, excluding echoed source.
Use the specific rejection reason and relevant types/roots; omit source paths,
line numbers and specification section numbers. These inline assertions replace
the old compiler-error `.stderr` files.

A fixture declares the warnings its `--check` prints the same way, with one
`// warning: <message substring>` line per warning; a positive fixture that
declares none must print none. Markers and warning lines pair up one to one,
each marker part of the message of a line of its own, so a warning printed
twice fails the fixture as a missing or an undeclared one does (a warning
prints once for its node as the source has it, `docs/implementation.md`
§3.12). Two `&`s on one line that warn alike print two identical lines and
take two markers, as `r6` in `branch_reference_copies.goose` does. The
markers' order does not matter: without line numbers it could not say where
a warning is, and warnings print in the order checking reaches their nodes
-- a callee's body at its first call, a loop's and a construct's once their
checks settle, redundant casts by line after the whole program -- which a
change to the checker may rearrange without changing what a program is told.
A rejection test answers for its warnings only if it declares some: checking
stops at its error, so which warnings print first depends on how far it got.
`cycle_local_store.goose`, for one, warns about its recursive call's
`&local` before the error that call reveals, at the store on the line above.

The runner checks more than exit status and runtime output:

| Check | Contract |
|---|---|
| `lexer_tokens.goose` | Exact token stream, including keyword classification, decoded literals and longest-match punctuation. |
| Every positive Goose fixture | Successful parse, successful initial dump, identical dump/reparse/dump, and typechecking with exactly the warnings it declares, unless its first line contains `parse-only`. Parsing also resolves type names; dumping alone does not. |
| First-line `dump-runtime` | Compile and execute the dumped source against the original output. `control_expression_dump.goose` uses this to check grouping semantics, which a stable dump alone cannot establish. |
| Every fixture with `// bce:elide` or `// bce:keep` | Run `-O1 --check --bce-test`, including expected-abort regressions. Native/JIT O0 and O2 runs independently check behavior. |
| `gfx_err_shader_syntax.goose`, `gfx_err_shader_part.goose` | Besides their markers, the error is reported at the program's line holding the offending GLSL. |
| Generated `call_chain.goose`, `call_chain_too_deep.goose` (`build/gen/<profile>/`) | A compile-time call path through 2000 distinct functions checks at `-O0`, and runs through TinyCC; at `-O2`, where the inliner may fold its single-use functions into one body only 64 C blocks deep at a time, it builds with the native C compiler and runs (`docs/implementation.md` §4). One through 6000, each calling the next from 32 blocks deep, is rejected as too deep for the compiler's stack instead of overflowing it (§3.1). |
| Generated `guard_runs.goose` (`build/gen/<profile>/`) | 300 guards in a row, in a function's body and in a loop's, build with the native C compiler and run at `-O0` and `-O2`: the rest of the block after a guard whose else leaves opens no C block (`docs/implementation.md` §6.2), where MSVC stops at 128 levels and clang at 256. A chain of 200 single-use functions, each calling the next behind four guards, folds at `-O2` into bodies 64 blocks deep, which would nest past both limits if the inliner's count left out blocks codegen opens (§4). |
| `loop_shapes.goose` at O1 | Read the generated C: every `shape_` function has the restated loop it is named for, marked by codegen's `/* loop shape: ... */` comment, and no `keep_` function has one (`docs/implementation.md` §6.12). Its runs check that each restated loop computes what a reference loop does. |
| `optimize.goose` at O0/O1/O2 | Inspect named tail-recursion bodies in `--specs`: supported integer accumulator/plain recursion becomes loops; modulo, floating-point reassociation, nonlocal-return frames and returns inside nested loops retain self calls. Mixed operators retain the ineligible call. Leading locals prevent base-case inlining from consuming these cases first. The `gb_` functions count the copies of their first statement: guarded base-case inlining copies the body once per self-call site where the test compares unassigned parameters, and not where it reads a global or a node, does arithmetic, or reads an assigned parameter. |

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

Two overlapping positive fixtures are now covered by their broader suites:

| Removed fixture (and its `.out`) | Retained coverage |
|---|---|
| `lifetimes/borrowed_context_store.goose` | `lifetimes/store_reached_types.goose` saves a slice through a borrowed context, keeps it in a local binding, stores it in the context's input table, and reads both the argument and the stored text. The same suite exercises the other destination forms. |
| `codegen/resizable_adt_tag.goose` | `codegen/adt_adaptation.goose` matches both variants of a resizable ADT through a reference parameter and directly. The resizable payload has no binder; the fixed payload is bound and read. |

The small `return_from_cycle.goose` remains beside `return_from_cached_call.goose`:
the former checks replay through a recursion back edge without an additional
cached-call path, which could otherwise mask a missing propagation step.

CI uses four jobs to cover these configurations without testing every
combination of platform, sanitizer, optimization level, and runtime mode.
Benchmarks run separately from CI.
