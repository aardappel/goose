# Goose compiler bootstrap regression

This is a compiler for a subset of Goose, written in Goose. It is a large
integration fixture for the main compiler, not a second compiler whose feature
coverage the suite promises to maintain. `main.goose` imports the other thirteen
modules. Only the compiler sources live here; the exploratory probes, reports,
and standalone build/test scripts are not part of the fixture.

The implementation was written from `docs/goose_spec.md`,
`docs/implementation.md`, Goose library interfaces/tests, and black-box host
behavior, without inspecting the C++ implementation under `src/`. Its embedded
C runtime is independent. It covers enough of the language to compile itself:
parsing, types and generics, semantic checking, specialization, and C emission.
Lifetime analysis and many language features remain incomplete. Do not use its
acceptance or rejection as the language's expected behavior.

The normal suite runs this fixture automatically. To run just this regression:

```sh
python test/run_tests.py --goose-in-goose-only --exe build/Release/goose.exe --cc native
```

Use the appropriate `--exe` path for your build. The test uses the suite's
selected native C compiler and profile:

1. The real compiler emits stage 1 from `main.goose`; native C compilation
   produces the first Goose-written compiler executable.
2. Stage 1 compiles the same sources into stage 2; native C compilation builds
   that executable.
3. Stage 2 compiles the sources into stage 3, which is also built and linked.
4. Stage-2 and stage-3 C must be byte-identical. Stage 3 checks its own sources
   twice in one process; both compilations must succeed and agree.
5. When TinyCC is available, the real compiler also executes the Goose-written
   compiler through its JIT. Its emitted C must match the native bootstrap.

Baseline runs the entire chain with host/native O0 and O2 and requires the same
self-compiled C at both settings. The sanitizer profile
uses host O2 and native Clang O1 with the suite's ASan/UBSan flags on every native
stage. `--no-jit` omits the JIT comparison; `--nocgen` retains host checking and
JIT self-compilation when available, but explicitly skips the native chain.

Generated files and per-stage logs are kept in
`build/gen/<profile>/goose_in_goose/O<level>/`. Each command must succeed and each
emission must produce nonempty C; stale outputs cannot satisfy a stage. Compiler
executables receive a 64 MiB stack budget where the platform permits it. The
runner reports stage-specific failures, including timeouts and sanitizer
diagnostics. The modules are excluded from ordinary standalone fixture discovery
because they need imports and compiler command-line arguments.
