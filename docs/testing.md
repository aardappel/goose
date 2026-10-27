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

The runners execute `-j`/`--jobs` jobs at once, defaulting to one per logical
processor up to 32. Their threads mostly wait for Goose, the C compiler, and the
programs they start. Each job buffers its output, and those buffers are printed
in job order. Logs therefore stay comparable regardless of the number of jobs.
`-j1` runs everything in order on one thread.

Runs of the same fixture are sequential because one run may write a file that
another would see (`stdlib_os.goose`, for example). The Goose-in-Goose chains
and sample runner start first. The sample runner (`samples/run_samples.py`) is a
separate process using the same number of jobs. The -O0 bootstrap chain is the
longest job, and the whole suite takes about as long as that chain.

`--gpu-jobs N` limits concurrent gfx programs within each runner for GPU drivers
that cannot handle many headless devices. There is no limit by default.

The log is divided into sections (parse, roundtrip and typecheck; generated
C; JIT runs; `errors_tc/`; samples; ...). Each section ends with a line
counting its checks, such as `== typecheck errors (...): 1668 ok`. Anything
a section prints other than its ok lines is shown in place, under the
section's title: a failure together with what led to it, and the skips. `-v`
also prints a line for every check that passed. Under GitHub Actions, those
ok lines are kept in a collapsed group per section, and the group is closed
before anything else prints. After the log, the FAIL lines are repeated with
the titles of their sections; under GitHub Actions they are printed as
annotations.

Each fixture is compiled as few times as its checks allow. One run of
`goose --roundtrip --check` parses the fixture, checks that its dump parses
again to the same dump, and typechecks it. That run adds `--bce-test` where
the fixture has annotations, uses `--parse` instead of `--check` for a
`parse-only` fixture, and adds `--dump-file` to write the dump for a
`dump-runtime` fixture. Each run has `--stats`, with which every run that
gets past resolution prints `parsed ok`, so a single `--check` of an `errors_tc/` fixture can tell a
parse failure from a typecheck failure. Where an optimization level both
builds the C and runs the program through TinyCC, one
`goose -O<n> --jit -o <file>` does both: it writes the C, then runs the
program in-process.

Most of those runs, and the runs of the `errors/` and `errors_tc/`
fixtures, share a compiler process with other fixtures.
`goose --multi-test <flags> a.goose b.goose ...` compiles each file in turn,
exactly as a separate run on that file would. It ends each file's part of
stdout and of stderr with a line `==== goose --multi-test: exit <code> <file>`.
From these lines the runner gives every fixture exactly the exit code and
output that a run of its own would have produced. Starting a compiler
process takes about as long as checking a small fixture, so a batch of 20 is
checked in a tenth of the time. If a batch crashes or aborts, the results for
the files before the one it was compiling are kept; that file and the rest
are then run one per process, so the crash is reported for the right
fixture. Runs that write a dump (`dump-runtime`), use `runtime-define`
defines, or run a program (TinyCC) still use one process each.

The C of the fixtures that do not link a native layer is also built a batch
at a time, at each optimization level and for the dump and `GS_DEBUG`
programs. One C compiler run compiles each program's unit with its `main`
renamed, together with a `main` that runs the program named by its first
argument, and links them once against the runtime object
(`CC.compile_programs` in `scripts/toolchain.py`). Every run of a program is
still a separate process. Where a batch does not build, each program in it
is built on its own, which shows why it failed. Starting and linking take
most of a C compiler run's time for these programs, so a batch of 20 builds
in a fifth of the time. `--batch N` sets how many files go to one compiler
process and how many programs go into one executable (by default, a number
that leaves every job some batches); `--batch 1` gives each file its own
process and each program its own executable.

## Fixture layout

Test fixtures are grouped by category; `run_tests.py` stays at the root of `test/`:

| Path under `test/` | Coverage |
|---|---|
| `syntax/` | Lexer, parser, dump grouping, imports and namespace resolution. Imported helpers live in `syntax/ns/` and `syntax/sub/`. `raw_strings_crlf.goose` is the only file checked out with CRLF line breaks (`.gitattributes`). |
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
| `ui/` | The `ui` module: fonts, windows and layout, widgets, popups and menus, text editing, style, drawing and the input queries, charts, rendering and input through headless gfx, whether the keys and the mouse belong to the ui, a runtime misuse, the layer's misuse messages, and the threading rejection. |
| `packages/` | Programs declaring packages from path sources under `pkgs/`, built by `goose build`: pure Goose packages (`pkg_basic`), one with plain C under its sidecar (`pkg_c_lib`), a CMake one (`pkg_cmake_lib`, which `native_toolchain.py` also uses), and programs with C of their own from a sidecar (`pkg_own_c`; `pkg_float_abi`, floating-point arguments on the stack between native C and TinyCC). `pkgs/col` is the package `errors/pkg_collision.goose` imports. |
| `errors/`, `errors_tc/` | Expected parser/resolver and semantic rejections. |
| `errors_tc/recovery/` | Programs with several errors, which the parser, resolution and the checker continue past (`docs/design/language_server.md` §5 D). Taken in order, each `// error:` marker must be part of the message of the error at the same position in the order the compiler reports them, and there must be no other errors, both in text and under `--diagnostics json`. `imported/` holds a module that `imports.goose` imports, which is not a fixture of its own. |
| `lsp/` | Language-server fixtures driven by `lsp_protocol.py` over the protocol, not compiled as programs. `basics.goose` declares queries and expected answers in `// lsp:` lines and imports `helper.goose`. `complete.goose` ends with `p.` to test completion. `recovery.goose` has errors in two functions; `// lsp: error` lines specify their locations, and queries use the code checked so far. `broken_decls.goose` has errors inside a struct, an enum, and functions, plus an unresolved import. `exemplar.goose` tests queries on uncalled generic functions using exemplar checks (`docs/design/language_server.md` §11). `roots_lib.goose` contains an error and is opened and closed alongside its importers, `roots_app.goose` and `roots_app2.goose`. `source_index.goose` tests globals whose initializers omit defaults or expand a `const for`, a generic function matching a `const for` arm list, and calls containing commas in literals and comments. `two_a.goose` and `two_b.goose` import `two_shared.goose`; only the second instantiates its generic function. `ufcs_ns.goose` makes UFCS calls on a namespaced type from the global namespace, one of which matches no overload. |
| `expected/` | Shared output and runtime-diagnostic expectations. |
| `run_tests.py` | The Python test runner. |
| `toolchain_selection.py` | Which compiler the runners use (`find_goose` in `scripts/toolchain.py`). An `--exe` path is used as given, made absolute, even when there are other compilers in `build/`. Without `--exe`, the runners use the first of `build/Release/`, `build/RelWithDebInfo/`, `build/` itself (for a single-configuration generator) and `build/Debug/`, so that a routine run uses an optimized compiler and an assertion build has to be chosen with `--exe`. If there is no compiler in any of these, or the `--exe` path does not exist, the run stops rather than using another compiler. A unittest, run by the main runner. |
| `compiler_roots.cpp` | Root-set join laws, feedback on weakening, order-independent equality and the deliberate comparison projections; compiled and run in both native profiles. |
| `compiler_containers.cpp` | The compiler's `small_vector` and `flat_map` against `std::vector` and `std::unordered_map` under random operations, across in-place/heap storage, growth and erasure, and `flat_map`'s reuse of an erased key's value cell; compiled and run in both native profiles. |
| `resource_paths.py` | Unicode source and C output paths, JIT entry-source resource roots, native executable roots with separate and standalone runtimes, relocation, and unchanged working-directory file access. Called by the main runner. |
| `cli_paths.py` | Tests command-line and import paths. `-o` may target a directory that does not yet exist, for generated C or a `goose build` executable. Package sources may be `"."` or `".."`, and an `--include` path may start with a separator. An unresolved import must report the searched locations or the absence of a standard library. Other cases pass a directory as the source or to `embed_file`, and run `--gen-runtime-header` without a source tree beside the compiler. Called by the main runner. |
| `extern_types.py` | One imported extern interface and C adapter with unused and partially used functions: slice, reference-pointee and return types are available to the header at O0/O2 through native C and TinyCC, with no implementation needed for an unused extern. Called by the main runner. |
| `extern_dylib.py` | An extern block bound from a shared library that the test builds. Every parameter shape goes through the runtime's binding, with the library opened by a spelling relative to the resource directory and by a bare name on the loader's path, both under the JIT (with no C toolchain available) and in a built program. The generated C is the same across compiles and binds only the members that are called. A missing library or symbol is a runtime error naming the spelling tried, and unused members need neither. A package declaring both kinds of extern builds its native part only for a program that calls its static one. The sidecar's `dylibs` lists map a name to the spelling each platform opens, with the program's mapping taking precedence over a package's, and a malformed entry or a name that no extern block declares is refused. Called by the main runner. |
| `runtime_cache.py`, `runtime_cache_cc.c` | Concurrent first builds share one completed runtime object; a failed compile cannot poison the cache, retry and reuse work, and dry runs leave the cache untouched. A controlled C compiler exercises both driver flag styles in debug and release configurations. Called by the main runner. |
| `native_toolchain.py` | A CMake interface package, runtime and program use one toolchain. Cache paths with and without spaces also exercise response-file arguments, using MinGW GCC on Windows when available. Called by the main runner. |
| `native_cache.py` | When several programs that use one package with C are built for the first time at once, that C is compiled only once. The other builds wait on the lock of the package's build directory and link the objects the first build made; the controlled C compiler of `runtime_cache_cc.c` checks that those objects are complete. A warm build compiles nothing. Both driver flag styles are tested. Called by the main runner. |
| `native_rebuild.py` | Tests when native code is rebuilt. Editing a program's C triggers rebuilding for `goose build` and `goose run`; naming the program by another path does not. Adding an extern fn without changing C rebuilds the JIT library. Other cases cover two C files with the same name in different directories, `--debug` and release builds side by side, and extern fns omitted from a library's symbol table. CMake tests cover reconfiguration after `cmake_options` changes, an undefined `cmake_target` reported during configuration, and a C++ target. Generated CMake files must keep their timestamps when unchanged, and a program's own CMake target must work regardless of how the program is named. Called by the main runner. |
| `native_builder.py` | C compiled on its own appends to a Goose builder: a `u8[>..]&` parameter arrives as `goose.h`'s `gs_rref`, and `gs_bld_append` adds bytes to it, from a program's sidecar C and from a CMake package's target, built by `goose build` and run in-process by `goose run`. Called by the main runner. |
| `runtime_io.py` | A program built by `goose build`, and run by `goose run` where the JIT runs, reads piped stdin: `read_stdin` gets every byte, a `\r\n` and a Ctrl-Z among them, which Windows' text mode would translate and stop at, and `read_line` into a builder already ending in a `\r` keeps that `\r`. On POSIX, `write_file_atomic` over a file keeps its permissions. Called by the main runner. |
| `commit_limit.py` | Windows only: a program running in a job object that limits its processes to committing 100 MB grows a data stack past that limit. The error must say that it is out of memory committing a data stack, not that the stack overran its reservation. Called by the main runner. |
| `huge_pages.py` | Linux only: the first 2 MB of each data stack region is marked for no transparent huge pages (`nh` in `/proc/<pid>/smaps`), so that a stack holding a few bytes commits a page rather than a huge one. Called by the main runner. |
| `exit_race.py` | The main program and eight workers write to stderr and call `exit(7)` at once, with the output redirected. Each of 20 runs of the program built by `goose build` must end with that exit code, even though on Windows two exits running the C library's teardown at the same time can crash it. Called by the main runner. |

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
| Additional Clang C build of `codegen_exec.goose`, native `-O1`, release/debug runtime; Clang builds of `simd_versions.goose`, `stdlib_base64.goose` and `simd_mul_add.goose`, native `-O2`, with `GS_SIMD=2`, `1` and `0`; Clang builds of `vec_elemwise.goose` with `GS_VEC=1` and `0` | Keep a second C-front-end check on the central codegen coverage file without repeating the whole suite for every backend. The `simd` functions' versions (spec §7.12) exist only where clang builds for x86-64: their C must contain every version, and at each level the C may be built for, the program must print what the baseline prints. `mul_add` is the exception. Run with `modes`, `simd_mul_add` reports the rounding of each kind of call: a plain function (once on arm64, twice on x86-64), and in a `simd` function a direct call, an `inline fn` helper, a plain helper and a function value. A direct call and a function value round once in the AVX2 and AVX-512 versions on a CPU with AVX2 and FMA; the helpers do too where Goose inlines them, and at Goose `-O0`, which inlines nothing, they round as the plain function does. The C is generated at both levels. The runner asks the operating system whether the CPU has them, and accepts either rounding where it can't ask. |
| Goose `-O0` and `-O2` through the in-process TinyCC backend, plus `GS_DEBUG=1` on the same selected fixtures, over every test and every sample | Check the generated C against a third, very different C implementation, and check that a program behaves the same whichever backend builds it. |
| One Linux Clang ASan/UBSan job | Instrument the C++ compiler, generated C, selected debug-runtime cases, all samples and the direct runtime tests (`runtime_threads_lifecycle.c`, `runtime_trim_memory.c`). |
| Goose-in-Goose bootstrap, both profiles | Build stage 1 with the real compiler, stage 2 with stage 1, and stage 3 with stage 2. Build all three executables, compare stage-2/stage-3 C, and run two self-checks in stage 2 while stage 3 is emitted and built (the fixed point makes stage 2 the same compiler as stage 3). The baseline profile requires the same self-compiled C across host/native O0 and O2 and across JIT/native execution; the sanitize profile instruments every native stage. |

Run just the bootstrap with
`python test/run_tests.py --goose-in-goose-only --exe <goose> --cc native`.
The usual `--profile`, `--no-jit`, and `--nocgen` options apply. With no native
C compiler, it still checks the source and, when available, self-compiles through
the JIT, but reports the native chain as skipped. Sources and the exact stage
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
(`docs/implementation.md` §7). `GooseRuntime` (`scripts/toolchain.py`) has
the compiler under test write the runtime's C (`--emit-runtime`) once per
run. It compiles that C once per toolchain configuration (compiler,
optimization level, defines and extra flags, so the sanitize profile's
runtime is instrumented as well) into `build/gen/runtime/`, or for the
samples into their own directory. `CC.compile(..., runtime=...)` then links
that object with each program built this way. Stages 2 and 3 of the
bootstrap contain their own runtime and link none. Every TinyCC run compiles
the single-unit form, which `--standalone` writes; the benchmarks build that
form too.

The direct `test/threads/runtime_threads_lifecycle.c` regression checks allocations,
mappings and Windows handles across worker churn, including unjoined workers,
concurrent/repeated waits, children outliving their parents and contained
workers that abort. The direct `test/runtime/runtime_trim_memory.c` test
checks that releasing unused data-stack memory decommits the pages on
Windows and drops them from the resident set on Linux and macOS, while
keeping live partial pages and other stacks. It also checks that discarded
pages are zeroed, that Windows growth recommits through the fault handler
behind a commit mark that each trim moves, and that macOS growth needs no
runtime-handled faults, before or after a trim. Both run in both
profiles. The normal suite also checks
user-visible worker error diagnostics.

The `gfx/` tests exercise the gfx package (https://github.com/aardappel/goose_gfx,
its `docs/gfx.md`). They always parse, typecheck and generate C. When the
compiler includes the gfx layer (configured with the package,
`GOOSE_BUILTIN_LIBS`), `goose build` links the tests against it and they run
at -O0 and -O2 and through TinyCC. They render headless into textures and
read back only pixel-aligned results, so their output is the same on every
backend and GPU. A machine with no GPU device (where the program prints
`gfx: no GPU device`) reports them as skipped, as does a compiler without
the layer. Linux CI runs them on Mesa's lavapipe. The runners set
`GOOSE_GFX_HEADLESS=1`, so no test or sample opens a window. The hidden
`--compile-shader` flag is tested on `gfx/probe.frag` and
`gfx/probe_badset.frag` in every compiler, with or without the layer
([Additional compiler checks](#additional-compiler-checks)).

The `physics/` tests exercise the physics package
(https://github.com/aardappel/goose_physics) in the same way. They always
generate C. When the compiler was configured with the package
(`GOOSE_BUILTIN_LIBS`), `goose build` links them and they run at -O0 and
-O2 and through TinyCC. Box3D is deterministic across platforms and worker
counts, so the tests print rounded physics results and compare them exactly.
Together they call every function of the layer.

The `ui/` tests exercise the ui module of the gfx package (its `docs/ui.md`)
in the same way. `goose build` links the ui layer, and for the test that
renders through gfx, the gfx layer too. A test needs the layers of its own
category and of every module it imports, and is skipped where one of them is
not built into the compiler. The tests drive a context with their own input
and print what comes back and what was drawn; `ui_render` draws through
headless gfx and reads back pixels only where the ui drew solid color.
Together they call every function of the layer.

Each package's own `test/api_check.py` compares the functions, structs, and
constants in its modules with their C headers. This catches
interface mismatches that can compile successfully on both sides. It also
rejects a struct the layer passes by value that TinyCC would pass differently
from the C compilers on System V x86-64: a struct of at most 16 bytes that
has a misaligned field, or that has only floats in one eightbyte and
integers in the other.

The gfx package's own `test/window/run_window_test.py` (in
https://github.com/aardappel/goose_gfx) is run by hand, on a machine with a
display and a GPU. It uses the `scripts/toolchain.py` of a Goose checkout
(the sibling `goose` directory, or whatever `GOOSE_REPO` names). It runs the
showcase `gfx_showcase.goose` with a window, both in-process and built by
every C toolchain found, on each GPU driver of the platform. It checks what
the program reports about its own readbacks, and decodes the screenshots the
program saves (under the package's `build/gfx-window/`) to check them for
detail and compare them across runs.

The TinyCC runs are skipped under the sanitizers, because there the program
runs uninstrumented inside an instrumented compiler and still holds its
runtime allocations when that compiler exits. A test the backend cannot run
yet is reported as a skip, not a failure: either because the compiler
refuses the program (`JIT mode does not support`) or because the test's
first line says `no-jit`. Prefer portable assertions to skipping a backend;
the math tests allow small rounding differences in transcendental results.
See `docs/design/jit_backend.md` for the backend's remaining limitations and
possible solutions.

## Expected output and diagnostics

An explicitly requested compiler must exist; CI does not silently skip it.
Every runnable fixture requires an `expected/<name>.out`, including an empty
file when the program should be silent. Expected aborts additionally require
`.aborts` and a nonempty `.stderr` containing the expected runtime diagnostic
substrings. Sanitizer reports fail even when a program was expected to abort.
Resource-bound fixtures can set a limit in their first line, such as
`// runtime-define: GS_MAX_STACKS=1`. The runner passes it as a `-D` to
every compile: the compiler checks `GS_MAX_STACKS` against the program's
static stack count, and the generated C takes care of the rest. Fixtures
that use this:

* `append_destination_storage.goose` detects temporary result stacks that
  output-only comparisons would miss.
* `cycle_scratch_locals.goose` shows that a recursion's scratch locals use
  the same few stacks at every level.
* `discarded_resizable_results.goose` shrinks each stack's reservation
  (`GS_STACK_RESERVE`) so that its loops overflow a stack unless every
  discarded result is released. `guard_flat_storage.goose` does the same for
  what a guard's condition, its else and the rest of its block use on the
  data stacks (the rest of the block is emitted without braces of its own).
  `continue_restores.goose` and `large_fixed_exits.goose` do the same for
  the watermarks that a `continue`, a `break` and a `return ... from`
  restore.
* `thread_cap.goose` shrinks the address space budget (`GS_STACK_BUDGET`)
  so that `hardware_threads()` reports its minimum of one wherever it runs.
* `stack_stats.goose` has its program report its stack use
  (`GS_STACK_STATS`), which is checked through
  `expected/stack_stats.stderr`.

`cycle_static_stacks.goose` runs the shapes that a call into a recursive
cycle allows (§7.8); the `errors_tc/cycle_temporary_*`,
`cycle_construction_*` and `cycle_result_*` fixtures are the rejected
shapes. Codegen fails with an internal error if any program holds a data
stack across such a call. The runner also checks the shape, but not the
counts, of the `--stacks` report of `thread_cap.goose` and
`cycle_scratch_locals.goose`. For `thread_spawn_discarded.goose`, whose
worker holds no regions, it checks that the report gives no thread cap. A
fixture expecting a debug-only abort sets `GS_DEBUG=1` the same way, which
makes every run of it a debug build: `cast_abort_location.goose` and
`overflow_abort_location.goose` check the location that a failing check
reports, and `thread_contained.goose` checks that the debug checks end only
the contained worker.

Parser/resolver errors live in `test/errors/`; semantic errors live in
`test/errors_tc/` and must get past parsing and resolution (`parsed ok`)
before they fail. Each source declares one or
more `// error: <diagnostic substring>` lines. The compiler must exit with 1,
and every marker must occur in one of these: diagnostic headers,
`in const for` expansion context lines, the instantiation chain's `in fn`
lines (without their ` instantiated from <path>:<line>` part), or the
reasons on overload candidates' lines. Echoed source does not count. The
compiler continues past the errors it can recover from, so a fixture may
print more errors than it declares; `test/errors_tc/recovery/` holds the
fixtures that declare all of their errors, in order (see the table above).
In a marker, use the specific rejection reason and relevant types/roots;
omit source paths, line numbers and specification section numbers.
`errors_tc/function_value_chain.goose` names a block's file, line and column
on purpose, because what it checks is how the chain names a block. These
inline assertions replace the old compiler-error `.stderr` files.

A fixture declares the warnings its `--check` prints in the same way, with
one `// warning: <message substring>` line per warning; a positive fixture
that declares none must print none. Markers and warning lines are paired one
to one, and each marker must be part of the message of a different warning
line. So a warning printed twice fails the fixture, just as a missing or an
undeclared warning does (a warning is printed once for its node as written
in the source, `docs/implementation.md` §3.12). Two `&`s on one line that
produce the same warning print two identical lines and need two markers, as
`r6` in `branch_reference_copies.goose` does. The order of the markers does
not matter. Without line numbers, a marker could not say where a warning is,
and warnings print in the order in which checking reaches their nodes: a
callee's body at its first call, a loop's or a construct's once its checks
settle, and redundant casts by line after the whole program. A change to the
checker may rearrange that order without changing what a program is told.
The warnings of a rejection test are checked only if it declares some:
checking stops at its error, so which warnings are printed first depends on
how far checking got. For example, `cycle_local_store.goose` warns about its
recursive call's `&local` before the error that the call reveals, at the
store on the line above.

## Additional compiler checks

The runner checks more than exit status and runtime output:

| Check | Contract |
|---|---|
| `lexer_tokens.goose` | Exact token stream, including keyword classification, decoded literals and longest-match punctuation. |
| Every positive Goose fixture | Successful parse, successful initial dump, identical dump/reparse/dump, and typechecking with exactly the warnings it declares, unless its first line contains `parse-only`. Parsing also resolves type names; dumping alone does not. |
| First-line `dump-runtime` | Compile and execute the dumped source against the original output. `control_expression_dump.goose` uses this to check grouping semantics, which a stable dump alone cannot establish. |
| Every fixture with `// bce:elide` or `// bce:keep` | Run `-O1 --check --bce-test`, including expected-abort regressions. Native/JIT O0 and O2 runs independently check behavior. |
| `gfx_err_shader_syntax.goose`, `gfx_err_shader_part.goose` | Besides their markers, the error is reported at the program's line holding the offending GLSL. |
| `gfx/probe.frag`, `gfx/probe_badset.frag` | `goose --compile-shader`, the shader compiler that every compiler has, with or without the gfx layer. `probe.frag` compiles, including its `#include` of `probe_common.glsl`, and reports its one sampler and its 32-byte uniform block. `probe_badset.frag`, which has a sampler outside the set that SDL_GPU gives a fragment shader's textures, is rejected with the rule it broke. |
| Generated `call_chain.goose`, `call_chain_too_deep.goose` (`build/gen/<profile>/`) | A compile-time call path through 2000 distinct functions passes the check at `-O0` and runs through TinyCC. At `-O2`, where the inliner may fold its single-use functions into one body only 64 C blocks deep at a time, it builds with the native C compiler and runs (`docs/implementation.md` §4). A path through 6000 functions, each calling the next from 32 blocks deep, is rejected as too deep for the compiler's stack instead of overflowing it (§3.1). |
| Generated `guard_runs.goose` (`build/gen/<profile>/`) | 300 guards in a row, in a function's body and in a loop's body, build with the native C compiler and run at `-O0` and `-O2`: the rest of the block after a guard whose else leaves the block opens no C block (`docs/implementation.md` §6.2). This matters because MSVC stops at 128 nesting levels and clang at 256. A chain of 200 single-use functions, each calling the next behind four guards, folds at `-O2` into bodies 64 blocks deep, which would nest past both limits if the inliner's count left out blocks codegen opens (§4). |
| `loop_shapes.goose` at O1 | Read the generated C: every `shape_` function has the restated loop it is named for, marked by codegen's `/* loop shape: ... */` comment, and no `keep_` function has one (`docs/implementation.md` §6.12). Its runs check that each restated loop computes what a reference loop does. |
| `member_parameters.goose` at O1 | Read the generated C: `walk`, which takes two slices and a `T[>..]&`, has each as two scalar C parameters (data pointer and count, header pointer and stack; spec C.3, `ParamFields`). Its runs pass slices, builders and pool references through calls, a recursion, a case-function dispatch, nested functions' free variables, a `simd` function, a `format` overload and an exported wrapper. |
| `member_stores.goose` at O1 | Read the generated C: `store_links` stores to fields reached through references that cannot be rebound, with no index on the way, as C member stores rather than through a pointer taken first (`StableDest`). Its runs check the stores, and check that when the right side rebinds the reference, the store still goes to the destination named before the right side ran (`evaluation_order.goose` has a right side writing the destination itself). |
| `optimize.goose` at O0/O1/O2 | Inspect named tail-recursion bodies in `--specs`: supported integer accumulator and plain recursion become loops, including a list walk that hands on a node read through its parameter. Modulo, floating-point reassociation, nonlocal-return frames, returns inside nested loops and a reference to a local passed down keep their self calls. Mixed operators retain the ineligible call. Leading locals prevent base-case inlining from consuming these cases first. The `gb_` functions count the copies of their first statement: guarded base-case inlining copies the body once per self-call site where the test compares unassigned parameters, including self-calls written with UFCS, but not where the test reads a global or a node, does arithmetic, or reads an assigned parameter. A u64 above i64.max that the optimizer creates (here, the starting accumulator of a fold and of `tre_and`) dumps as that unsigned value. |
| `optimize.goose` at O1, its C | `il_pair`, an `inline fn` returning two values, stays a call in Goose but is declared `GS_INLINE` in the C, for the C compiler to inline (spec §7.13). `il_plain`, a copy of `il_big` without the keyword, is not. |
| `docs/reference.md` | Every ```` ```goose ```` block of the language reference compiles (as written, or wrapped in a `main`), a block marked `// error` fails, no block warns, and where the JIT runs, each complete program prints what the block after it shows. `scripts/check_doc_examples.py` does the work in a process of its own, as the samples' runner does, writing the blocks under `build/gen/<profile>/doc_examples/`; its docstring has the markers a document uses for a block that is only a fragment or is compiled without being run. Run it by hand on any document: `python scripts/check_doc_examples.py docs/reference.md --run -v`. |
| The command line (`driver_verbs` in `run_tests.py`) | The verbs against the flags they stand for: `goose run` (where the JIT runs) prints what the program does, `goose check` only checks it, and `goose run -o` is refused; `goose version` and `goose --version` print the compiler's version and the runtime's 12 hex digits; a word that is not a command, an option or a file is an unknown command, named with the command it is close to; `--color always` and `CLICOLOR_FORCE` color an error and its caret, and output that is not a terminal, or `NO_COLOR`, has none; `goose check` prints nothing on success, and with `--stats` its `typechecked ok` line; every shell's completion script names the commands and their options, bash's (where there is bash) completes a command, an option and a `.goose` file, and zsh's parses (where there is zsh); `goose help --man` holds every command's help, and groff (where there is one) reads it without a warning; a `--stack-reserve` under 64 KB is refused. For the help: `goose --help` names the commands but not the hidden flags; together, `goose <command> --help` for run, build, check, fetch, format, toolchain, version, lsp and completion, `goose help c` and `goose help internals` name every option that `src/main.cpp` reads except the hidden shader ones; `goose help build` names `--dry-run`; an unknown topic is refused; and no file, or `goose build` with none, prints the usage. `goose toolchain --json` finds a C compiler wherever the runner found one, escapes a tab in `GOOSE_HOME`, and does not mistake a directory named `git` on PATH for git; with no C compiler anywhere, `goose toolchain` names every one it looked for; and `goose build --cc` naming a file that is not a program says why it cannot start it. Where there is a C compiler, `goose build` builds an executable that runs, and `--dry-run` prints what it would run without building one. |
| `doc_comments.py` | `goose --dump-docs` prints the documentation comments of a program's declarations, fields and variants: the run of `//` lines directly above each one, without its indentation. A blank line, a block comment or any token ends a run, a comment after code does not start one, and a banner followed by a blank line belongs to nothing. The program still checks and roundtrips. |
| `format.py` | `goose format`: each golden case, formatted through standard input, prints the layout it expects and is a fixed point. The cases cover the spaces the parser's token roles decide (unary against binary operators, generic lists against comparisons, type suffixes, `as!`, `[>..<]`, ranges), trailing commas added where a list's closing bracket starts a line and not after an initializer list's `..`, an array's fill form, parentheses or a last match arm ending in `return` or a bare `break`, indentation (tabs, continuations keeping their offset from the line they continue and going no further left than it, so that a chain broken after its operators stays lined up under its first operand, bracket-aligned lines moving with their bracket, the body of a wrapped header or condition, `})`), blank lines, `\r\n` and a byte order mark, trailing comments moved with their line and aligned runs kept aligned, block comments and a multi-line string moved with their line, and a table of statements after semicolons kept. The command line: a file rewritten in place, a formatted one left with its modification time, `--check` listing what is not formatted (exit 1) and writing nothing, the directory walk (`.goose` files, sorted, no dot-directories or build trees unless named), a file that does not parse left as it is while the others are formatted (exit 2, which wins over `--check`'s 1), a read-only file refused, standard input with no files or `-`, wrong arguments, `--verify` and the help. A file's documentation comments come through as `--dump-docs` printed them. A second task, `format_sweep`, runs `goose format --verify` over `samples/`, `stdlib/`, `bench/` and `test/`: every file must format, pass the formatter's own checks (the same tokens and comments, a comma added only before a closing bracket, a parse to the same dump and documentation) and come out the same when formatted again; the only files skipped as not parsing must be under `errors/` or `errors_tc/`, or be the four fixtures broken on purpose. |
| `lsp_protocol.py` | Drives `goose lsp` over its protocol to check editor queries, diagnostics, edits, error recovery, root changes, and queries spanning multiple programs. See [Language server protocol checks](#language-server-protocol-checks) below. Before the first client's shutdown, `textDocument/formatting` of an unsaved document with `\r\n` line breaks returns edits that turn its text into what `goose format -` prints and touch only the lines that change; a formatted document gets no edits, and one that does not parse, or one not open, null. |
| `packages_git.py` | Besides the fetches, `goose check --offline` rejects a package not in the cache at its import, naming `goose fetch`, without fetching, and accepts it once fetched. |
| `diagnostics_json.py` | `goose check --diagnostics json` prints an array with one object per error or warning, whose `text` is what text mode prints on stderr; the progress lines go to stderr instead of stdout. A lexer error is placed at its caret's column, one byte wide, and the errors after a `"""` string that the lexer rejected are placed at their own lines. A typecheck error's range covers the node it is reported at, and its instantiation chain's entries are placed at the calls they name. A warning has a range too. An import error covers the whole import. A `checkfree` function's kept checks are reported as several errors at once. An error with no place in the program has only its text. |

### Language server protocol checks

`lsp_protocol.py` first runs `--probe`. A client in the runner then initializes
the server with `test/lsp/basics.goose` as its entry file and the VS Code
extension's commands. It opens the file, waits for diagnostics (there should be
none), and sends the queries specified by its `// lsp:` lines. These check:

* Hover text; definitions at a position, in a file, or absent; document symbols;
  and signature help with the active parameter.
* Semantic token types at a position, including positions with no token; reference
  counts; and renames that either make the expected number of edits or are refused.
  Each rename edit must change only the name's leaf.
* Inlay hint text, position, and count per line. Defaults inserted by the checker
  must produce no hints. Folding ranges must cover the expected lines, and
  selection-range chains must contain the requested range and be nested.
* Code-lens positions and titles. An instantiation lens must have a count matching
  its data lines and the command that lists them. A "Run" lens must carry the
  file's URI; "not instantiated" must be a title alone.
* Completion labels that must be present or absent. `complete.goose` ends with
  `p.`, as source often does when an editor requests completion.
* Incoming and outgoing calls, including the function at the other end and the
  call lines. Uncalled functions must have no call results, and names that are
  not functions must have no call item.
* A workspace symbol from the imported `helper.goose`.

The test also checks that analysis asks the client to refresh code lenses,
semantic tokens, and inlay hints. It then checks the following changes and
additional roots in order:

1. A burst of edits introduces an error and removes it. Diagnostics must reflect
   the latest text, and a change must cancel an interrupted analysis. The test then
   inserts a line at the top and immediately requests a hover at the shifted
   position. The answer must use the new text.
2. `recovery.goose` and `broken_decls.goose` are opened as separate roots. Every
   error must be published, and queries must still receive answers. Document
   symbols requested immediately after opening `recovery.goose` must come from its
   analysis, rather than return an empty result.
3. `source_index.goose` checks that global initializers are indexed as written,
   even though the checker rewrites them in place. Bindings in `const for` arms
   must resolve, and signature help must ignore commas in literals and comments.
   `ufcs_ns.goose` checks that signature help, and hover and definitions on a
   resolved call and on one that matches no overload, find a UFCS call's
   functions in the namespace of its receiver's type, not the global ones.
4. `errors_tc/recovery/inside_braces.goose` checks errors inside struct-literal and
   match braces. They must appear at their source locations, with no extra errors.
5. `exemplar.goose` and `errors_tc/recovery/exemplar_unchecked.goose` check queries
   on uncalled generic functions through their exemplars. In the second file,
   those bodies must produce no diagnostics even though no binding could make them
   correct. Its only error is in main, as reported by `goose check`.
6. Roots are opened, closed, and edited. After a request runs the due analyses,
   the test checks the last diagnostics published for each file. It opens
   `roots_lib.goose`, which contains an error, alone and then alongside its
   importer `roots_app.goose`. Closing that importer must leave hover and document
   symbols available again. Next it opens both `roots_app.goose` and
   `roots_app2.goose` as importers, then edits the first to stop importing the
   library. The library's error must appear at every step, only once when both
   roots import it. Separately, `helper.goose` stays open while `basics.goose`
   closes and ceases to be an entry file. The helper's hover must then use its own
   root analysis.

The first server shuts down cleanly. A second client starts with `two_a.goose`
and `two_b.goose` as entry files. It renames a function in their shared import,
`two_shared.goose`, then requests references and incoming calls. Results must
include both programs, with each location listed once. They must also include
callers of a function reached only by the second program's instantiation of a
generic function. The second server then shuts down.

Run `test/lsp_sweep.py` by hand; it is separate from the main runner. It
measures `goose lsp` robustness and latency across every sample, the
goose_in_goose bootstrap, every stdlib module, and all positive, `errors/`, and
`errors_tc/` fixtures.

For each program, it opens a separate root and waits for diagnostics. At every
identifier start and after every `.`, it requests hover, definition, completion,
and signature help, plus incoming and outgoing calls for any function named
there. It requests semantic tokens, symbols, folding, inlay hints, and code
lenses once per program. It then makes ten random one-character edits in a burst
and waits for diagnostics. After restoring the text, it checks that diagnostics
match the initial ones.

The sweep fails if the server exits, returns an error, takes over 2 s for a
request, or stops responding. It prints the slowest files and requests.
`--quick` checks a handful of files in a second or two. The baseline is in
`docs/design/language_server.md` §12.

## Fixture audit

A fixture is load-bearing when some regression would fail it and nothing
else. Two fixtures with similar diagnostics are not necessarily duplicates:
they can exercise different expression visitors, specialization/cache states,
root propagation, or generated layouts. Whole-program globals and call graphs
are also part of many regressions, so combining two fixtures can change the
property under test.

The October 2026 audit measured this. It ran the suite with a compiler built
with clang's `-fsanitize-coverage=trace-pc-guard` and with an instrumented
runtime object, and recorded, for every fixture, the compiler edges its runs
hit and the runtime lines its programs ran. Coverage was counted only toward
fixtures that would notice the same regression:

* a positive fixture's coverage toward other positive fixtures (a rejection
  does not notice the checker refusing a good program);
* a bce-annotated fixture's coverage in `bce.h` toward other annotated
  fixtures;
* a rejection's coverage in the checker's files toward other rejections of
  the same rule (a program accepted by mistake goes unnoticed by positive
  fixtures and by other rules' rejections).

No fixture's coverage was fully contained in that of any single other fixture.
Even when coverage overlaps, a fixture may catch a failure that others miss. A
fixture covered entirely by the rest of its class was therefore removed only if
all of these conditions held:

* Named fixtures checked the same behavior through the same construct.

* It had not been added later as a regression test for a bug those fixtures
  missed.

* It checked nothing that coverage alone cannot show, such as an expected
  runtime diagnostic, an integer width or extreme value, a `GS_DEBUG` check, or
  output text.

A second review challenged every proposed removal. All removals together also
had to preserve each class's coverage.

This removed 153 `errors_tc/` fixtures and two positive ones. Most of the
removed `errors_tc/` fixtures tested a shrink, store or provenance rule
reached through the same construct as a sibling. The two positive ones were
`threads/thread_spawn_bound.goose`, whose generic thread_fn bindings
`thread_spawn_modules.goose` spawns through the same generics with checked
output, and `typing/fill_counts.goose`, whose computed fill counts
`named_const_readonly_refs.goose`, `appended_literals.goose` and
`const_name_scopes.goose` build and print. Nearly every positive candidate
stayed: most abort with a diagnostic that no other fixture's `.stderr` pins,
or pin values, types or output that run the same compiler edges. Small
fixtures on one subject were merged. Each original fixture became a function
of its own, called in the original order, and the expected output is their
outputs concatenated:

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

The audit also compared each rejection's comment with the error it reports.
Where a later rule caught a fixture first, the fixture was changed to reach
the rule it names. Where its old program was the only test of the path it
did reach, that program was kept as a separate fixture named for that path.
For example, the `growshrink_reach_*` fixtures hold a second root, so that
the caller's storage only bounds what they store, beside the
`growshrink_exact_*` ones, whose holders' contents are known exactly;
`index_type_let_array.goose` and `index_type_fixed_array.goose` pin the two
halves of the index-type array rule; and the `fnval_*` fixtures are named
for the function-value rule they reach. Markers that any rejection of their
kind would match (`cannot clear`, `read-only`, a type name) were replaced by
the specific message. Markers may also match the instantiation chain's frames
and an overload candidate's reason, which some fixtures exist to pin.

The small `return_from_cycle.goose` remains beside `return_from_cached_call.goose`:
the former checks replay through a recursion back edge without an additional
cached-call path, which could otherwise mask a missing propagation step.

CI uses four jobs to cover these configurations without testing every
combination of platform, sanitizer, optimization level, and runtime mode.
Benchmarks run separately from CI.
