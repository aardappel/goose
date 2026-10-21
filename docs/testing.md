# Compiler and runtime tests

<details>
<summary>Table of contents</summary>

- [Running the suite](#running-the-suite)
- [Fixture layout](#fixture-layout)
- [Configurations and backends](#configurations-and-backends)
- [Expected output and diagnostics](#expected-output-and-diagnostics)
- [Additional compiler checks](#additional-compiler-checks)
- [Fixture audit](#fixture-audit)

</details>

## Running the suite

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

The log comes in sections (parse, roundtrip and typecheck; generated C;
JIT runs; `errors_tc/`; samples; ...), each ending in a line counting its
checks, `== typecheck errors (...): 1668 ok`. What a section prints besides
its ok lines shows in place, under the section's title: a failure with what
led to it, and the skips. `-v` prints a line for every check that passed as
well. Under GitHub Actions those lines are kept in a collapsed group per
section, closed before anything else prints. After the log, the FAIL lines
repeat with the titles of their sections, as GitHub annotations there.

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

## Fixture layout

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
| `threads/` | Workers, queues, shared globals, contained workers' aborts and the native runtime lifecycle test. Imported helpers live in `threads/spawn/`. |
| `stdlib/` | Standard-library modules. |
| `goose_in_goose/` | A Goose-written compiler used as one multi-file bootstrap regression: build and run three generations, check a C fixed point and repeated self-checks of the stage-2 compiler, and compare JIT/native self-compilation. |
| `gfx/` | The `gfx` graphics module: headless rendering, textures, compute, frames and input, a runtime misuse, shaders from files and from the program; shader and threading rejections (fixtures with `// error:` markers). The programs hold their shaders; the shader files beside them are for the file form of `embed_shader` and `--compile-shader`. `gfx/window/` is the windowed showcase, not part of the suite. |
| `physics/` | The `physics` module: worlds, bodies, every kind of shape and geometry, all joint kinds, queries, events, recording and replay, a runtime misuse, and the threading rejection. |
| `ui/` | The `ui` module: fonts, windows and layout, widgets, popups and menus, text editing, style, drawing and the input queries, charts, rendering and input through headless gfx, whether the keys and the mouse are the ui's, a runtime misuse, the layer's misuse messages, and the threading rejection. |
| `errors/`, `errors_tc/` | Expected parser/resolver and semantic rejections. |
| `errors_tc/recovery/` | Programs with several errors, which the parser, resolution and the checker go on past (`docs/design/language_server.md` §5 D): each `// error:` marker is part of the message of the error in its position, in the order the compiler reports them, with no error besides, in text and under `--diagnostics json`. `imported/` holds a module `imports.goose` imports, which is no fixture of its own. |
| `lsp/` | The language server's fixtures, driven over the protocol by `lsp_protocol.py` rather than compiled as programs: `basics.goose`, an entry file whose `// lsp:` lines state the queries and their answers, `helper.goose`, which it imports, `recovery.goose`, a program with an error in each of two functions, whose `// lsp: error` lines say where they are published and whose queries answer from what was checked, and `broken_decls.goose`, whose struct, enum and functions have errors inside their declarations and whose import finds nothing., and `exemplar.goose`, generic functions nothing calls, whose queries answer from their exemplar checks (`docs/design/language_server.md` §11), and `roots_lib.goose`, a module with an error that `roots_app.goose` and `roots_app2.goose` import, opened and closed beside them as roots come and go, and `source_index.goose`, globals whose initializers leave defaults out and expand a `const for`, a generic function matching over a `const for` of arms, and calls with commas in literals and comments, and `two_a.goose` and `two_b.goose`, two entry files importing `two_shared.goose`, the second alone instantiating its generic function. |
| `expected/` | Shared output and runtime-diagnostic expectations. |
| `run_tests.py` | The Python test runner. |
| `compiler_roots.cpp` | Root-set join laws, feedback on weakening, order-independent equality and the deliberate comparison projections; compiled and run in both native profiles. |
| `compiler_containers.cpp` | The compiler's `small_vector` and `flat_map` against `std::vector` and `std::unordered_map` under random operations, across in-place/heap storage, growth and erasure, and `flat_map`'s reuse of an erased key's value cell; compiled and run in both native profiles. |
| `resource_paths.py` | Unicode source and C output paths, JIT entry-source resource roots, native executable roots with separate and standalone runtimes, relocation, and unchanged working-directory file access. Called by the main runner. |
| `extern_types.py` | One imported extern interface and C adapter with unused and partially used functions: slice, reference-pointee and return types are available to the header at O0/O2 through native C and TinyCC, with no implementation needed for an unused extern. Called by the main runner. |
| `extern_dylib.py` | An extern block bound from a shared library the test builds: every parameter shape through the runtime's binding, by a spelling relative to the resource directory and by a bare name on the loader's path, under the JIT (with no C toolchain in sight) and built, with the generated C the same across compiles and binding only the called members; a missing library or symbol is a runtime error naming the spelling tried, and unused members need neither; a package declaring both kinds of extern builds its native part only for a program calling its static one. Called by the main runner. |
| `runtime_cache.py`, `runtime_cache_cc.c` | Concurrent first builds share one completed runtime object; a failed compile cannot poison the cache, retry and reuse work, and dry runs leave the cache untouched. A controlled C compiler exercises both driver flag styles in debug and release configurations. Called by the main runner. |
| `native_cache.py` | Concurrent first builds of programs using one package with C compile that C once: the others wait on the lock of the package's build directory and link the objects it made, which the controlled C compiler of `runtime_cache_cc.c` checks are complete; a warm build compiles nothing. Both driver flag styles. Called by the main runner. |
| `runtime_io.py` | A program built by `goose build`, and run by `goose run` where the JIT runs, reads piped stdin: `read_stdin` gets every byte, a `\r\n` and a Ctrl-Z among them, which Windows' text mode would translate and stop at, and `read_line` into a builder already ending in a `\r` keeps that `\r`. On POSIX, `write_file_atomic` over a file keeps its permissions. Called by the main runner. |
| `commit_limit.py` | Windows only: a program run in a job object that lets its processes commit 100 MB grows a data stack past that, and is told it is out of memory committing a data stack, not that the stack overran its reservation. Called by the main runner. |

Positive fixtures are discovered one level below `test/`; nested import helpers
run through their entry programs. `goose_in_goose/` is handled separately as one
bootstrap program, rather than discovering its modules as standalone fixtures.
Keep fixture stems unique across categories,
since expectations and generated build artifacts use those names. The runner
rejects duplicate names.

## Configurations and backends

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
concurrent/repeated waits, children outliving their parents and contained
workers that abort. It runs in both profiles. The normal suite also checks
user-visible worker error diagnostics.

The `gfx/` tests exercise the gfx package (https://github.com/aardappel/goose_gfx,
its `docs/gfx.md`). They
always parse, typecheck and generate C. When the compiler includes the gfx layer
(configured with the package, `GOOSE_BUILTIN_LIBS`), the tests also link against the
libraries listed by `goose --gfx-link` and run at -O0 and -O2 and through
TinyCC. They render headless into textures and read back only pixel-aligned
results, so their output is the same on every backend and GPU; a machine with no
GPU device (the program prints `gfx: no GPU device`) reports them skipped, as
does a compiler without the layer. Linux CI runs them on Mesa's lavapipe. The
runners set `GOOSE_GFX_HEADLESS=1`, so no test or sample opens a window. The
hidden `--compile-shader` flag is probed on `gfx/probe.frag`.

The `physics/` tests exercise the physics package
(https://github.com/aardappel/goose_physics) in the same way. They always
generate C. When the compiler was configured with the package
(`GOOSE_BUILTIN_LIBS`), `goose build` links them and they run at -O0 and
-O2 and through TinyCC. Box3D
is deterministic across platforms and worker counts, so they print physics
results, rounded, and compare them exactly. Between them they call every
function of the layer.

The `ui/` tests exercise the ui module of the gfx package (its `docs/ui.md`) in the
same way, linking `goose --ui-link` and, for the one that renders through
gfx, `goose --gfx-link` too: a test needs the layers of its own category and
of every module it imports, and is skipped where one is missing. They drive a
context with input of their own and print what comes back and what was
drawn; `ui_render` draws through headless gfx and reads back pixels only
where the ui drew solid color. Between them they call every function of the
layer.

Each package's own `test/api_check.py` compares the functions, structs, and
constants in its modules with their C headers. This catches
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

The TinyCC runs are skipped under the sanitizers, where the program runs
uninstrumented inside an instrumented compiler and still holds its runtime
allocations when that compiler exits.
A test the backend cannot run yet is reported as a skip, not a failure: either
because the compiler refuses the program outright (`JIT mode does not support`)
or because the test's first line says `no-jit`. Prefer portable assertions to
skipping a backend; the math tests allow small rounding differences in
transcendental results. See `docs/design/jit_backend.md` for the backend's remaining limitations and
possible solutions.

## Expected output and diagnostics

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
`cycle_scratch_locals.goose` for its shape, not its counts, and that of
`thread_spawn_discarded.goose`, whose worker holds no regions, for the
thread cap that leaves: none. A fixture
expecting a debug-only abort sets `GS_DEBUG=1` the same way, which makes
every run of it a debug build: `cast_abort_location.goose` and
`overflow_abort_location.goose` check the location a failing check reports,
and `thread_contained.goose` that the debug checks end a contained worker
alone.

Parser/resolver errors live in `test/errors/`; semantic errors live in
`test/errors_tc/` and must get past parsing and resolution (`parsed ok`)
before they fail. Each source declares one or
more `// error: <diagnostic substring>` lines. The compiler must exit with 1,
and all markers must occur in diagnostic headers or `in const for` expansion
context lines, excluding echoed source. The compiler goes on past the
errors it can recover from, so a fixture may print more errors than it
declares; `test/errors_tc/recovery/` holds the fixtures that declare every
one of theirs, in order (the table above).
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

## Additional compiler checks

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
| `member_parameters.goose` at O1 | Read the generated C: `walk`, which takes two slices and a `T[>..]&`, has each as two scalar C parameters (data pointer and count, header pointer and stack; spec C.3, `ParamFields`). Its runs pass slices, builders and pool references through calls, a recursion, a case-function dispatch, nested functions' free variables, a `simd` function, a `format` overload and an exported wrapper. |
| `member_stores.goose` at O1 | Read the generated C: `store_links` stores to fields reached through references that cannot be rebound, with no index on the way, as C member stores rather than through a pointer taken first (`StableDest`); its runs check the stores, and that a right side rebinding the reference on the way still stores where the destination named before it ran (`evaluation_order.goose` has a right side writing the destination itself). |
| `optimize.goose` at O0/O1/O2 | Inspect named tail-recursion bodies in `--specs`: supported integer accumulator/plain recursion becomes loops, a list walk handing on a node read through its parameter included; modulo, floating-point reassociation, nonlocal-return frames, returns inside nested loops and a reference to a local passed down retain self calls. Mixed operators retain the ineligible call. Leading locals prevent base-case inlining from consuming these cases first. The `gb_` functions count the copies of their first statement: guarded base-case inlining copies the body once per self-call site where the test compares unassigned parameters, UFCS-spelled self-calls included, and not where it reads a global or a node, does arithmetic, or reads an assigned parameter. A u64 above i64.max that the optimizer makes, a fold's and `tre_and`'s accumulator start, dumps as that unsigned value. |
| `docs/reference.md` | Every ```` ```goose ```` block of the language reference compiles (as written, or wrapped in a `main`), a block marked `// error` fails, none warns, and where the JIT runs, each complete program prints the block shown after it. `scripts/check_doc_examples.py` does the work in a process of its own, as the samples' runner does, writing the blocks under `build/gen/<profile>/doc_examples/`; its docstring has the markers a document uses for a block that is only a fragment or is compiled without being run. Run it by hand on any document: `python scripts/check_doc_examples.py docs/reference.md --run -v`. |

| `doc_comments.py` | `goose --dump-docs` prints the documentation comments of a program's declarations, fields and variants: the run of `//` lines directly above each, less its indentation; a blank line, a block comment or any token ends a run, a comment after code starts none, and a banner followed by a blank line belongs to nothing. The program still checks and roundtrips. |
| `lsp_protocol.py` | `goose lsp` driven over its own protocol: `--probe`, then a client of the runner's initializes with `test/lsp/basics.goose` as an entry file and the VS Code extension's commands, opens it, waits for its diagnostics (none) and asks what its `// lsp:` lines say to ask: hovers that must contain a text, definitions that must land at a place or in a file, or nowhere, document symbols, signature help with its active parameter, semantic token types at a place, or none there, the number of references of a name, renames that must make a number of edits (each to the name's leaf alone) or be refused, inlay hints with their text at a place and how many a line has (none where a default the check filled in is written), folding ranges by their lines, selection range chains that must hold a range and nest, code lenses by line and title (an instantiation count with as many lines as its data and the command that lists them, "Run" with the file's URI, "not instantiated" a title alone), completions that must offer or lack a label (`complete.goose`, whose last statement is the `p.` an editor has when it asks), incoming and outgoing calls by the function at their other end with the lines of the calls (none for a function nothing calls, and no item for a name that is no function), and a workspace symbol from the imported `helper.goose`, and checks the analysis asked the client to refresh its lenses, semantic tokens and inlay hints. Then a burst of edits into an error and back, whose diagnostics must follow the last text, with the analysis a change interrupts cancelled; then a line put above everything and a hover asked at once at the moved position, which must answer from the text after the change, not the analysis before it; then `recovery.goose` (its document symbols asked at once on opening, which must be its analysis's rather than none) and `broken_decls.goose` opened as roots of their own, with every error published and the queries answered; then `source_index.goose`, whose globals' initializers, which the check rewrites in place, must be indexed as written, whose `const for` arms' bindings must resolve, and whose calls' literals and comments hold commas signature help must not count; then `errors_tc/recovery/inside_braces.goose`, whose errors inside the braces of a struct literal and of a match are published where they are, and no others; then `exemplar.goose` and `errors_tc/recovery/exemplar_unchecked.goose`, whose generic functions nothing calls answer from their exemplars, the second's with no diagnostic though no binding could make their bodies right (its one error is main's, as `goose check` finds); then roots coming and going, judged by the diagnostics last published for a file once a request has run the analyses due: `roots_lib.goose`, a module with an error, opened alone, then with `roots_app.goose` importing it open, then with that closed (when its hover and document symbols must answer again), then with `roots_app.goose` and `roots_app2.goose` both importing it, the one error published once, and with the first edited to stop importing it, its error published at every step; and `helper.goose` left open as `basics.goose` closes and stops being an entry file, whose hover must answer from a root of its own; and a clean shutdown. Then a second client with `two_a.goose` and `two_b.goose` as its entry files renames a function of the `two_shared.goose` both import, and asks its references and incoming calls, which must be found in both programs, each place once, and the callers of a function only the second program's instantiation of a generic function calls; and that server's shutdown. |
| `lsp_sweep.py` | Not a section of the runner but a script beside it: a robustness and latency sweep of `goose lsp` over the repository's Goose programs (every sample, the goose_in_goose bootstrap, every stdlib module, every positive fixture, every `errors/` and `errors_tc/` fixture), each opened as a root of its own: its diagnostics awaited, then hover, definition, completion and signature help asked at every identifier start and after every `.`, with the incoming and outgoing calls of a function named there, semantic tokens, symbols, folding, inlay hints and code lenses once, a burst of ten random one-character edits with their diagnostics awaited, and the text put back, whose diagnostics must be the first ones again. It fails on a server exit, an error reply, a request over 2 s or a server that stops answering, and prints the slowest files and requests; `--quick` sweeps a handful of files in a second or two. The baseline is in `docs/design/language_server.md` §12. |
| `packages_git.py` | Besides the fetches, `goose check --offline` rejects a package not in the cache at its import, naming `goose fetch`, without fetching, and accepts it once fetched. |
| `diagnostics_json.py` | `goose check --diagnostics json` prints an array with one object per error or warning, whose `text` is what the text mode printed on stderr, with the progress lines on stderr instead of stdout. A lexer error is at its caret's column, one byte wide, and the errors after a `"""` string the lexer rejected are at their own lines; a typecheck error ranges over the node it is at, with its instantiation chain's entries at the calls they name; a warning has a range too; an import error covers the whole import; a `checkfree` function's kept checks are several errors at once; an error with no place in the program is its text alone. |

## Fixture audit

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
