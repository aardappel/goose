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
| `runtime/` | Runtime diagnostics, reusable slot and slice pool operations and array size checks, and `runtime_trim_memory.c`, the native test of releasing unused data-stack memory. |
| `storage/` | Relative references, pools and serialization. |
| `threads/` | Workers, queues, shared globals, contained workers' aborts and `runtime_threads_lifecycle.c`, the native runtime lifecycle test. Imported helpers live in `threads/spawn/`. |
| `stdlib/` | Standard-library modules. |
| `goose_in_goose/` | A Goose-written compiler used as one multi-file bootstrap regression: build and run three generations, check a C fixed point and repeated self-checks of the stage-2 compiler, and compare JIT/native self-compilation. |
| `audio/` | The `audio` module: the offline PCM mixer advanced by exact frame counts; the rejection of bad rates, PCM, parameters and output; voice and sound capacity and stale handles; and the threading rejection. |
| `gfx/` | The `gfx` graphics module: headless rendering, textures, compute, frames and input, a runtime misuse, shaders from files and from the program; shader and threading rejections (fixtures with `// error:` markers). The programs hold their shaders; the shader files beside them are for the file form of `embed_shader` and `--compile-shader`. |
| `physics/` | The `physics` module: worlds, bodies, every kind of shape and geometry, all joint kinds, queries, events, recording and replay, a runtime misuse, and the threading rejection. |
| `ui/` | The `ui` module: fonts, windows and layout, widgets, popups and menus, text editing, style, drawing and the input queries, charts, rendering and input through headless gfx, whether the keys and the mouse are the ui's, a runtime misuse, the layer's misuse messages, and the threading rejection. |
| `packages/` | Programs declaring packages from path sources under `pkgs/`, built by `goose build`: pure Goose packages (`pkg_basic`), one with plain C under its sidecar (`pkg_c_lib`), a CMake one (`pkg_cmake_lib`, which `native_toolchain.py` also uses), and programs with C of their own from a sidecar (`pkg_own_c`; `pkg_float_abi`, floating-point arguments on the stack between native C and TinyCC). `pkgs/col` is the package `errors/pkg_collision.goose` imports. |
| `errors/`, `errors_tc/` | Expected parser/resolver and semantic rejections. |
| `errors_tc/recovery/` | Programs with several errors, which the parser, resolution and the checker go on past (`docs/design/language_server.md` §5 D): each `// error:` marker is part of the message of the error in its position, in the order the compiler reports them, with no error besides, in text and under `--diagnostics json`. `imported/` holds a module `imports.goose` imports, which is no fixture of its own. |
| `lsp/` | The language server's fixtures, driven over the protocol by `lsp_protocol.py` rather than compiled as programs: `basics.goose`, an entry file whose `// lsp:` lines state the queries and their answers, and `helper.goose`, which it imports; `complete.goose`, whose last statement is the `p.` an editor has when it asks for completions; `recovery.goose`, an error in each of two functions, whose `// lsp: error` lines say where they are published and whose queries answer from what was checked; `broken_decls.goose`, a struct, an enum and functions with errors inside their declarations, and an import that finds nothing; `exemplar.goose`, generic functions nothing calls, whose queries answer from their exemplar checks (`docs/design/language_server.md` §11); `roots_lib.goose`, a module with an error that `roots_app.goose` and `roots_app2.goose` import, opened and closed beside them as roots come and go; `source_index.goose`, globals whose initializers leave defaults out and expand a `const for`, a generic function matching over a `const for` of arms, and calls with commas in literals and comments; and `two_a.goose` and `two_b.goose`, two entry files importing `two_shared.goose`, the second alone instantiating its generic function. |
| `expected/` | Shared output and runtime-diagnostic expectations. |
| `run_tests.py` | The Python test runner. |
| `toolchain_selection.py` | The compiler the runners take (`find_goose` in `scripts/toolchain.py`): an `--exe` path as given, made absolute, even with others in `build/`; without one, the first of `build/Release/`, `build/RelWithDebInfo/`, `build/` itself (a single-configuration generator's) and `build/Debug/`, so that a routine run uses an optimized compiler and an assertion build is chosen by `--exe`; with none there, or an `--exe` that does not exist, the run stops rather than taking another. A unittest, run by the main runner. |
| `compiler_roots.cpp` | Root-set join laws, feedback on weakening, order-independent equality and the deliberate comparison projections; compiled and run in both native profiles. |
| `compiler_containers.cpp` | The compiler's `small_vector` and `flat_map` against `std::vector` and `std::unordered_map` under random operations, across in-place/heap storage, growth and erasure, and `flat_map`'s reuse of an erased key's value cell; compiled and run in both native profiles. |
| `resource_paths.py` | Unicode source and C output paths, JIT entry-source resource roots, native executable roots with separate and standalone runtimes, relocation, and unchanged working-directory file access. Called by the main runner. |
| `cli_paths.py` | Paths on the command line and in imports: `-o` into a directory that does not exist yet, for the C and for `goose build`'s executable; a package source of `"."` or `".."`; an `--include` path rooted by a leading separator; an import that resolves nowhere, which names where it was looked for, or says that no standard library was found; a directory given as the source or to `embed_file`; and `--gen-runtime-header` from a compiler with no source tree around it. Called by the main runner. |
| `extern_types.py` | One imported extern interface and C adapter with unused and partially used functions: slice, reference-pointee and return types are available to the header at O0/O2 through native C and TinyCC, with no implementation needed for an unused extern. Called by the main runner. |
| `extern_dylib.py` | An extern block bound from a shared library the test builds: every parameter shape through the runtime's binding, by a spelling relative to the resource directory and by a bare name on the loader's path, under the JIT (with no C toolchain in sight) and built, with the generated C the same across compiles and binding only the called members; a missing library or symbol is a runtime error naming the spelling tried, and unused members need neither; a package declaring both kinds of extern builds its native part only for a program calling its static one; the sidecar's `dylibs` lists map a name to the spelling each platform opens, the program's over a package's, and a malformed entry or a name no extern block declares is refused. Called by the main runner. |
| `runtime_cache.py`, `runtime_cache_cc.c` | Concurrent first builds share one completed runtime object; a failed compile cannot poison the cache, retry and reuse work, and dry runs leave the cache untouched. A controlled C compiler exercises both driver flag styles in debug and release configurations. Called by the main runner. |
| `native_toolchain.py` | A CMake interface package, runtime and program use one toolchain. Cache paths with and without spaces also exercise response-file arguments, using MinGW GCC on Windows when available. Called by the main runner. |
| `native_cache.py` | Concurrent first builds of programs using one package with C compile that C once: the others wait on the lock of the package's build directory and link the objects it made, which the controlled C compiler of `runtime_cache_cc.c` checks are complete; a warm build compiles nothing. Both driver flag styles. Called by the main runner. |
| `native_rebuild.py` | When a native part builds again: a program's own C edited after a build, for `goose build` and `goose run`, but not when the program is named by another path; an extern fn added without touching the C, which rebuilds the JIT library; two C files of one name in different directories; `--debug` builds of a program's own C beside its release build; the extern fns a library's symbol table leaves out; and a CMake package configured again when its `cmake_options` change, a `cmake_target` its project does not define reported at configure, a C++ target, the files generated for CMake keeping their times when nothing changed, and a program's own CMake target however the program is named. Called by the main runner. |
| `native_builder.py` | C compiled on its own appends to a Goose builder: a `u8[>..]&` parameter arrives as `goose.h`'s `gs_rref`, and `gs_bld_append` adds bytes to it, from a program's sidecar C and from a CMake package's target, built by `goose build` and run in-process by `goose run`. Called by the main runner. |
| `runtime_io.py` | A program built by `goose build`, and run by `goose run` where the JIT runs, reads piped stdin: `read_stdin` gets every byte, a `\r\n` and a Ctrl-Z among them, which Windows' text mode would translate and stop at, and `read_line` into a builder already ending in a `\r` keeps that `\r`. On POSIX, `write_file_atomic` over a file keeps its permissions. Called by the main runner. |
| `commit_limit.py` | Windows only: a program run in a job object that lets its processes commit 100 MB grows a data stack past that, and is told it is out of memory committing a data stack, not that the stack overran its reservation. Called by the main runner. |
| `huge_pages.py` | Linux only: the first 2 MB of each data stack region is marked for no transparent huge pages (`nh` in `/proc/<pid>/smaps`), so that a stack holding a few bytes commits a page rather than a huge one. Called by the main runner. |
| `exit_race.py` | The main program and eight workers write to stderr and call `exit(7)` at once, with the output redirected: each of 20 runs of the program `goose build` built ends with that code, though two exits running the C library's teardown at once can crash it on Windows. Called by the main runner. |

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
| Additional Clang C build of `codegen_exec.goose`, native `-O1`, release/debug runtime; Clang builds of `simd_versions.goose` and `stdlib_base64.goose`, native `-O2`, with `GS_SIMD=2`, `1` and `0` | Keep a second C-front-end check on the central codegen coverage file without repeating the whole suite for every backend. The `simd` functions' versions (spec §7.12) exist only where clang builds for x86-64: their C must hold every version, and each level the C may be built for must print what the baseline does. |
| Goose `-O0` and `-O2` through the in-process TinyCC backend, plus `GS_DEBUG=1` on the same selected fixtures, over every test and every sample | Check the generated C against a third, very different C implementation, and that a program means the same whichever backend builds it. |
| One Linux Clang ASan/UBSan job | Instrument the C++ compiler, generated C, selected debug-runtime cases, all samples and the direct runtime tests (`runtime_threads_lifecycle.c`, `runtime_trim_memory.c`). |
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
workers that abort. The direct `test/runtime/runtime_trim_memory.c` checks
that releasing unused data-stack memory decommits the pages on Windows, drops
them from Linux's resident set and on macOS marks them reusable, keeps live
partial pages and other stacks, and that growth commits them again, through
the fault handler on Windows and macOS, behind a commit mark each trim
moves. Both run in both profiles. The normal suite also checks
user-visible worker error diagnostics.

The `gfx/` tests exercise the gfx package (https://github.com/aardappel/goose_gfx,
its `docs/gfx.md`). They
always parse, typecheck and generate C. When the compiler includes the gfx layer
(configured with the package, `GOOSE_BUILTIN_LIBS`), `goose build` links the
tests against it and they run at -O0 and -O2 and through TinyCC. They render headless into textures and read back only pixel-aligned
results, so their output is the same on every backend and GPU; a machine with no
GPU device (the program prints `gfx: no GPU device`) reports them skipped, as
does a compiler without the layer. Linux CI runs them on Mesa's lavapipe. The
runners set `GOOSE_GFX_HEADLESS=1`, so no test or sample opens a window. The
hidden `--compile-shader` flag is probed on `gfx/probe.frag` and
`gfx/probe_badset.frag` in every compiler, with the layer or without it
([Additional compiler checks](#additional-compiler-checks)).

The `physics/` tests exercise the physics package
(https://github.com/aardappel/goose_physics) in the same way. They always
generate C. When the compiler was configured with the package
(`GOOSE_BUILTIN_LIBS`), `goose build` links them and they run at -O0 and
-O2 and through TinyCC. Box3D
is deterministic across platforms and worker counts, so they print physics
results, rounded, and compare them exactly. Between them they call every
function of the layer.

The `ui/` tests exercise the ui module of the gfx package (its `docs/ui.md`) in the
same way, `goose build` linking the ui layer and, for the one that renders
through gfx, the gfx layer too: a test needs the layers of its own category
and of every module it imports, and is skipped where one is not built into
the compiler. They drive a
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

The gfx package's own `test/window/run_window_test.py` (in
https://github.com/aardappel/goose_gfx) is run by hand, on a machine with a
display and a GPU, against a Goose checkout whose `scripts/toolchain.py` it
uses (the sibling `goose` directory, or whatever `GOOSE_REPO` names): it runs
the showcase `gfx_showcase.goose` with a window in-process and built by every
C toolchain found, on each GPU driver of the platform, checks what the program
reports about its own readbacks, and decodes the screenshots it saves (under
the package's `build/gfx-window/`) to check them for detail and compare them
across runs.

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
the rest of its block take, the rest emitted without braces of its own, and
`continue_restores.goose` and `large_fixed_exits.goose` for the watermarks
that a `continue`, a `break` and a `return ... from` restore.
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
and all markers must occur in diagnostic headers, `in const for` expansion
context lines, the instantiation chain's `in fn` lines (less their
` instantiated from <path>:<line>`) or the reasons on overload candidates'
lines, excluding echoed source. The compiler goes on past the
errors it can recover from, so a fixture may print more errors than it
declares; `test/errors_tc/recovery/` holds the fixtures that declare every
one of theirs, in order (the table above).
Use the specific rejection reason and relevant types/roots; omit source paths,
line numbers and specification section numbers.
`errors_tc/function_value_chain.goose` names a block's file, line and column
on purpose: how the chain names a block is what it checks. These inline
assertions replace
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
| `gfx/probe.frag`, `gfx/probe_badset.frag` | `goose --compile-shader`, the shader compiler every compiler has, with the gfx layer or without it: `probe.frag` compiles, its `#include` of `probe_common.glsl` included, and reports its one sampler and its 32-byte uniform block; `probe_badset.frag`, a sampler outside the set SDL_GPU gives a fragment shader's textures, is rejected with the rule it broke. |
| Generated `call_chain.goose`, `call_chain_too_deep.goose` (`build/gen/<profile>/`) | A compile-time call path through 2000 distinct functions checks at `-O0`, and runs through TinyCC; at `-O2`, where the inliner may fold its single-use functions into one body only 64 C blocks deep at a time, it builds with the native C compiler and runs (`docs/implementation.md` §4). One through 6000, each calling the next from 32 blocks deep, is rejected as too deep for the compiler's stack instead of overflowing it (§3.1). |
| Generated `guard_runs.goose` (`build/gen/<profile>/`) | 300 guards in a row, in a function's body and in a loop's, build with the native C compiler and run at `-O0` and `-O2`: the rest of the block after a guard whose else leaves opens no C block (`docs/implementation.md` §6.2), where MSVC stops at 128 levels and clang at 256. A chain of 200 single-use functions, each calling the next behind four guards, folds at `-O2` into bodies 64 blocks deep, which would nest past both limits if the inliner's count left out blocks codegen opens (§4). |
| `loop_shapes.goose` at O1 | Read the generated C: every `shape_` function has the restated loop it is named for, marked by codegen's `/* loop shape: ... */` comment, and no `keep_` function has one (`docs/implementation.md` §6.12). Its runs check that each restated loop computes what a reference loop does. |
| `member_parameters.goose` at O1 | Read the generated C: `walk`, which takes two slices and a `T[>..]&`, has each as two scalar C parameters (data pointer and count, header pointer and stack; spec C.3, `ParamFields`). Its runs pass slices, builders and pool references through calls, a recursion, a case-function dispatch, nested functions' free variables, a `simd` function, a `format` overload and an exported wrapper. |
| `member_stores.goose` at O1 | Read the generated C: `store_links` stores to fields reached through references that cannot be rebound, with no index on the way, as C member stores rather than through a pointer taken first (`StableDest`); its runs check the stores, and that a right side rebinding the reference on the way still stores where the destination named before it ran (`evaluation_order.goose` has a right side writing the destination itself). |
| `optimize.goose` at O0/O1/O2 | Inspect named tail-recursion bodies in `--specs`: supported integer accumulator/plain recursion becomes loops, a list walk handing on a node read through its parameter included; modulo, floating-point reassociation, nonlocal-return frames, returns inside nested loops and a reference to a local passed down retain self calls. Mixed operators retain the ineligible call. Leading locals prevent base-case inlining from consuming these cases first. The `gb_` functions count the copies of their first statement: guarded base-case inlining copies the body once per self-call site where the test compares unassigned parameters, UFCS-spelled self-calls included, and not where it reads a global or a node, does arithmetic, or reads an assigned parameter. A u64 above i64.max that the optimizer makes, a fold's and `tre_and`'s accumulator start, dumps as that unsigned value. |
| `optimize.goose` at O1, its C | `il_pair`, an `inline fn` returning two values, which stays a call in Goose, is declared `GS_INLINE` in the C for the C compiler to inline (spec §7.13), and `il_plain`, `il_big`'s twin without the keyword, is not. |
| `docs/reference.md` | Every ```` ```goose ```` block of the language reference compiles (as written, or wrapped in a `main`), a block marked `// error` fails, none warns, and where the JIT runs, each complete program prints the block shown after it. `scripts/check_doc_examples.py` does the work in a process of its own, as the samples' runner does, writing the blocks under `build/gen/<profile>/doc_examples/`; its docstring has the markers a document uses for a block that is only a fragment or is compiled without being run. Run it by hand on any document: `python scripts/check_doc_examples.py docs/reference.md --run -v`. |
| The command line (`driver_verbs` in `run_tests.py`) | The verbs against the flags they stand for: `goose run` (where the JIT runs) prints what the program does, `goose check` only checks it, and `goose run -o` is refused; `goose version` prints `goose runtime` and 12 hex digits; a `--stack-reserve` under 64 KB is refused. The help: `goose --help` names the commands but not the hidden flags; `goose <command> --help` for run, build, check, fetch, format, toolchain, version and lsp, and `goose help c` and `goose help internals`, between them name every option `src/main.cpp` reads but the hidden shader ones; `goose help build` names `--dry-run`, an unknown topic is refused, and no file, or `goose build` with none, prints the usage. `goose toolchain --json` finds a C compiler wherever the runner found one, escapes a tab in `GOOSE_HOME`, and takes no directory named `git` on PATH for git; with no C compiler anywhere, `goose toolchain` names every one it looked for; and `goose build --cc` naming a file that is no program says why it cannot start it. Where there is a C compiler, `goose build` builds an executable that runs, and `--dry-run` prints what it would run without building one. |
| `doc_comments.py` | `goose --dump-docs` prints the documentation comments of a program's declarations, fields and variants: the run of `//` lines directly above each, less its indentation; a blank line, a block comment or any token ends a run, a comment after code starts none, and a banner followed by a blank line belongs to nothing. The program still checks and roundtrips. |
| `format.py` | `goose format`: each golden case, formatted through standard input, prints the layout it expects and is a fixed point. The cases cover the spaces the parser's token roles decide (unary against binary operators, generic lists against comparisons, type suffixes, `as!`, `[>..<]`, ranges), trailing commas added where a list's closing bracket starts a line and not after an initializer list's `..`, an array's fill form, parentheses or a last match arm ending in `return` or a bare `break`, indentation (tabs, continuations keeping their offset from the line they continue, bracket-aligned lines moving with their bracket, the body of a wrapped header or condition, `})`), blank lines, `\r\n` and a byte order mark, trailing comments moved with their line and aligned runs kept aligned, block comments and a multi-line string moved with their line, and a table of statements after semicolons kept. The command line: a file rewritten in place, a formatted one left with its modification time, `--check` listing what is not formatted (exit 1) and writing nothing, the directory walk (`.goose` files, sorted, no dot-directories or build trees unless named), a file that does not parse left as it is while the others are formatted (exit 2, which wins over `--check`'s 1), a read-only file refused, standard input with no files or `-`, wrong arguments, `--verify` and the help. A file's documentation comments come through as `--dump-docs` printed them. A second task, `format_sweep`, runs `goose format --verify` over `samples/`, `stdlib/`, `bench/` and `test/`: every file must format, pass the formatter's own checks (the same tokens and comments, a comma added only before a closing bracket, a parse to the same dump and documentation) and come out the same when formatted again; the only files skipped as not parsing must be under `errors/` or `errors_tc/`, or be the four fixtures broken on purpose. |
| `lsp_protocol.py` | `goose lsp` driven over its own protocol: `--probe`, then a client of the runner's initializes with `test/lsp/basics.goose` as an entry file and the VS Code extension's commands, opens it, waits for its diagnostics (none) and asks what its `// lsp:` lines say to ask: hovers that must contain a text, definitions that must land at a place or in a file, or nowhere, document symbols, signature help with its active parameter, semantic token types at a place, or none there, the number of references of a name, renames that must make a number of edits (each to the name's leaf alone) or be refused, inlay hints with their text at a place and how many a line has (none where a default the check filled in is written), folding ranges by their lines, selection range chains that must hold a range and nest, code lenses by line and title (an instantiation count with as many lines as its data and the command that lists them, "Run" with the file's URI, "not instantiated" a title alone), completions that must offer or lack a label (`complete.goose`, whose last statement is the `p.` an editor has when it asks), incoming and outgoing calls by the function at their other end with the lines of the calls (none for a function nothing calls, and no item for a name that is no function), and a workspace symbol from the imported `helper.goose`, and checks the analysis asked the client to refresh its lenses, semantic tokens and inlay hints. Then a burst of edits into an error and back, whose diagnostics must follow the last text, with the analysis a change interrupts cancelled; then a line put above everything and a hover asked at once at the moved position, which must answer from the text after the change, not the analysis before it; then `recovery.goose` (its document symbols asked at once on opening, which must be its analysis's rather than none) and `broken_decls.goose` opened as roots of their own, with every error published and the queries answered; then `source_index.goose`, whose globals' initializers, which the check rewrites in place, must be indexed as written, whose `const for` arms' bindings must resolve, and whose calls' literals and comments hold commas signature help must not count; then `errors_tc/recovery/inside_braces.goose`, whose errors inside the braces of a struct literal and of a match are published where they are, and no others; then `exemplar.goose` and `errors_tc/recovery/exemplar_unchecked.goose`, whose generic functions nothing calls answer from their exemplars, the second's with no diagnostic though no binding could make their bodies right (its one error is main's, as `goose check` finds); then roots coming and going, judged by the diagnostics last published for a file once a request has run the analyses due: `roots_lib.goose`, a module with an error, opened alone, then with `roots_app.goose` importing it open, then with that closed (when its hover and document symbols must answer again), then with `roots_app.goose` and `roots_app2.goose` both importing it, the one error published once, and with the first edited to stop importing it, its error published at every step; and `helper.goose` left open as `basics.goose` closes and stops being an entry file, whose hover must answer from a root of its own; and a clean shutdown. Then a second client with `two_a.goose` and `two_b.goose` as its entry files renames a function of the `two_shared.goose` both import, and asks its references and incoming calls, which must be found in both programs, each place once, and the callers of a function only the second program's instantiation of a generic function calls; and that server's shutdown. Before the first client's shutdown, `textDocument/formatting` of an unsaved document with `\r\n` line breaks returns edits that turn its text into what `goose format -` prints and touch only the lines that change; a formatted document gets no edits, and one that does not parse, or one not open, null. |
| `packages_git.py` | Besides the fetches, `goose check --offline` rejects a package not in the cache at its import, naming `goose fetch`, without fetching, and accepts it once fetched. |
| `diagnostics_json.py` | `goose check --diagnostics json` prints an array with one object per error or warning, whose `text` is what the text mode printed on stderr, with the progress lines on stderr instead of stdout. A lexer error is at its caret's column, one byte wide, and the errors after a `"""` string the lexer rejected are at their own lines; a typecheck error ranges over the node it is at, with its instantiation chain's entries at the calls they name; a warning has a range too; an import error covers the whole import; a `checkfree` function's kept checks are several errors at once; an error with no place in the program is its text alone. |

`test/lsp_sweep.py` is not a section of the runner but a script beside it, run by hand: a robustness and latency sweep of `goose lsp` over the repository's Goose programs (every sample, the goose_in_goose bootstrap, every stdlib module, every positive fixture, every `errors/` and `errors_tc/` fixture), each opened as a root of its own: its diagnostics awaited, then hover, definition, completion and signature help asked at every identifier start and after every `.`, with the incoming and outgoing calls of a function named there, semantic tokens, symbols, folding, inlay hints and code lenses once, a burst of ten random one-character edits with their diagnostics awaited, and the text put back, whose diagnostics must be the first ones again. It fails on a server exit, an error reply, a request over 2 s or a server that stops answering, and prints the slowest files and requests; `--quick` sweeps a handful of files in a second or two. The baseline is in `docs/design/language_server.md` §12.

## Fixture audit

A fixture is load-bearing when some regression would fail it and nothing
else. Similar diagnostics do not make two fixtures duplicates: they exercise
different expression visitors, specialization/cache states, root propagation,
or generated layouts. Whole-program globals and call graphs are also part of
many regressions, so combining them can change the property under test.

The October 2026 audit measured this. It ran the suite with a compiler built
with clang's `-fsanitize-coverage=trace-pc-guard`, and an instrumented runtime
object, and recorded for every fixture the compiler edges its runs hit and the
runtime lines its programs ran. Coverage counted only toward fixtures that
would notice the same regression: a positive fixture's toward other positive
fixtures (a rejection does not notice the checker refusing a good program), a
bce-annotated one's in `bce.h` toward other annotated ones, and a rejection's
in the checker's files toward other rejections of the same rule (a program
accepted by mistake goes unnoticed by positive fixtures and by other rules'
rejections). No fixture's coverage was contained in any one other fixture's,
and shared coverage is necessary, not sufficient: code that runs is not code a
fixture fails on. So a fixture covering nothing its class did not was kept
unless named fixtures pin the same behavior through the same construct, it was
not added after them as the regression test of a fix they missed, and nothing
it pins is invisible to coverage: an expected runtime diagnostic, an integer
width or extreme value, a `GS_DEBUG` check, the text of an output. A second
review tried to refute every removal, and the removals together had to keep
every class's coverage.

That removed 153 `errors_tc/` fixtures, most of them a shrink, store or
provenance rule reached through the same construct as a sibling, and two
positive ones: `threads/thread_spawn_bound.goose`, whose generic thread_fn
bindings `thread_spawn_modules.goose` spawns through the same generics with
checked output, and `typing/fill_counts.goose`, whose computed fill counts
`named_const_readonly_refs.goose`, `appended_literals.goose` and
`const_name_scopes.goose` build and print. Nearly every positive candidate
stayed: most abort with a diagnostic no other fixture's `.stderr` pins, or pin
values, types or output that run the same compiler edges. Small fixtures of
one subject were merged, each member a function of its own called in the
members' order, the expected output theirs concatenated:

| Fixture | Merged from |
|---|---|
| `codegen/adt_field_tags.goose` | `variable_adt_field_tag.goose`, `resizable_adt_tail_tag.goose` |
| `codegen/relative_optional_null.goose` | `relative_null_tests.goose`, `relative_optional_targets.goose` |
| `codegen/named_result_destinations.goose` | `named_result_bytes.goose`, `named_result_exclusions.goose`, `inlined_named_result_tail.goose` |
| `lifetimes/shrink_operand_values.goose` | `shrink_owned_copies.goose`, `shrink_fresh_receivers.goose`, `shrink_argument_values.goose` |
| `lifetimes/break_value_roots.goose` | `break_value_provenance.goose`, `break_value_holder_contents.goose` |
| `storage/relative_default_values.goose` | `relative_defaults.goose`, `relative_default_fields.goose` |
| `storage/serialize_image_checks.goose` | `serialize_canonical.goose`, `serialize_offset_overflow.goose` |
| `syntax/extern_block_dump.goose` | `extern_block.goose`, `extern_block_escapes.goose` |
| `optimizer/basecase_aliased_guards.goose` | `basecase_reference_alias.goose`, `basecase_slice_alias.goose` |
| `optimizer/tail_reference_params.goose` | `tail_reference_rebind.goose`, `tail_reference_read.goose` |
| `runtime/float_text_digits.goose` | `float_text_f64.goose`, `float_text_shortest.goose` |
| `runtime/float_conversions_unchecked.goose` | `f32_casts_unchecked.goose`, `f64_conversions_unchecked.goose` |
| `typing/variable_size_literals.goose` | `variable_element_literals.goose`, `variable_literal_equality.goose` |
| `typing/nested_fn_assignments.goose` | `nested_fn_exits_assigned.goose`, `nested_fn_reused_assigns.goose` |
| `typing/type_aliases.goose` | `alias_nominal_recursion.goose`, `variant_aliases.goose` |
| `typing/dispatch_result_no_reference.goose` | `dispatch_result_diverging_case.goose`, `dispatch_result_null_case.goose` |
| `typing/constant_widths.goose` | `constant_extents.goose`, `constant_shift_wrap.goose` |

The audit also read each rejection's comment against the error it reports.
Where a later rule reached a fixture first, the fixture changed to reach the
rule it names, and, where its old program was the only test of the path it
did reach, that program stayed as a fixture named for it: the
`growshrink_reach_*` fixtures hold a second root, so that the caller's
storage only bounds what they store, beside the `growshrink_exact_*` ones,
whose holders' contents are known exactly; `index_type_let_array.goose` and
`index_type_fixed_array.goose` pin the two halves of the index-type array
rule; the `fnval_*` fixtures are named for the function-value rule they
reach. Markers that any rejection of their kind would match (`cannot clear`,
`read-only`, a type name) became the specific message, and markers may match
the instantiation chain's frames and an overload candidate's reason, which
some fixtures exist to pin.

The small `return_from_cycle.goose` remains beside `return_from_cached_call.goose`:
the former checks replay through a recursion back edge without an additional
cached-call path, which could otherwise mask a missing propagation step.

CI uses four jobs to cover these configurations without testing every
combination of platform, sanitizer, optimization level, and runtime mode.
Benchmarks run separately from CI.
