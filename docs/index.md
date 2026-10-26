# Goose documentation

Goose is a memory-safe systems language with no heap allocator, garbage
collector, or lifetime annotations ([what it is and why](../README.md)). Start
with the language guides below, arranged from introduction to detailed
specification. The other documents cover the library, tools, and compiler.

## The language, in reading order

| Document | What it is | Read it when |
|---|---|---|
| [README](../README.md) | What Goose is and why, in one page, with the numbers. | You want to know whether to look further. |
| [Tutorial](tutorial.md) | The language by example, concentrating on what is different about Goose: where values live, the array family, references that survive growth, enums in two sizes, `return … from`, threads. | You are new to Goose. It takes about an afternoon to read. |
| [Language reference](reference.md) | Every feature, topic by topic, with a compiling example for each. It covers the whole language in a form meant for reading, and points into the specification for every rule. | You know the basics and want all of it, or want to look something up. |
| [Specification](goose_spec.md) | The exact rules, in the language an implementor needs: size classes, construction, the lifetime and shrink rules, the compilation model, the representation. | You want to know why something did not compile, or are writing an implementation. |

## Library and tools

* [Standard library](stdlib.md): the seventeen modules, `std`, `dictionary`,
  `vec`, `math`, `os`, `ffi`, `binary`, `base64`, `utf8`, `csv`, `json`,
  `regex`, `exception`, and `gfx`, `audio`, `physics` and `ui` for graphics, sound,
  rigid bodies and widgets. All are written in readable Goose under
  `stdlib/`.
* [Building programs and using packages](packages.md): `goose build`,
  libraries from git repositories or directories, a program's own C, and
  how to make a package.
* [Samples](../samples/README.md): thirty-one complete programs in reading
  order, from a tour of the language to a JSON parser, a threaded
  Mandelbrot, a spinning cube and two small games.
* [VS Code extension](../vscode/README.md): highlighting, checks on save,
  and one-key runs.
* [Other editors](editors.md): the language server in Neovim, Helix and
  Emacs, and `goose format`, the formatter, on save.

## Benchmarks

* [Summary](../bench/summary.md): what the sixteen benchmarks show, gains
  and losses, and why.
* [Results](../bench/results.md): every measurement, against idiomatic and
  expert C++ and against safe Rust.
* [Design](../bench/design.md): what the suite tests and how it is run.

## The compiler

* [Implementation notes](implementation.md): how the compiler works, pass
  by pass: the typechecker's roots and provenance, the shrink rules, the
  optimizer, bounds-check elimination, code generation and the runtime.
* [Testing](testing.md): the test suite, its fixtures and how to run it.

### Design notes

The reasoning behind features, kept as it was written. The specification
and the reference describe what was built; these say why.

* [Index types and `checkfree` functions](design/index_types.md): bounds
  checks moved to where data enters the program, and functions the
  compiler proves check-free.
* [`const for`](design/const_for.md): specialization-time expansion over
  a struct's fields, an enum's variants, a list of types or a list of
  constants.
* [Relative references](design/relative_references.md): a measured
  comparison of self-relative and pool-relative links.
* [Serialization](design/serialization.md): the verified loader for
  structures of relative references.
* [Namespaces](design/namespaces.md): minimal namespaces and qualified
  names.
* [Builds and packages](design/packages.md): the design behind `goose
  build` and packages from git.
* [The in-process backend](design/jit_backend.md): how TinyCC runs a
  program inside the compiler, and what it cannot do.
* [String constants, writability and threads](design/string_constants.md):
  why read-only data cannot be laundered into writable data.
* [Independently owned dynamic data](design/owning_dynamic_data.md): an
  editor's open documents as a worked example of slice pools.
* [Cleanup at scope exit](design/scope_exit_cleanup.md): `defer`,
  destructor types and pool finalizers compared against the compiler's
  exit paths and `return … from`; a proposal.
* [Standard library design](design/stdlib_design.md): the library's
  original scope and conventions.
* [Compiler analyses](design/analysis_inventory.md): an inventory of the
  checker's analyses and options for simplifying them.
* [Column arrays](design/soa.md): a struct-of-arrays storage proposal, not
  implemented.
* [WebAssembly](design/wasm.md): running without address-space reservation,
  comparing fixed partitions with relocating stacks; a proposal.
* [Prior art](design/prior_art.md): the languages, libraries and papers
  nearest Goose's memory model, ranked, and what has no precedent.
* [Dynamic externs](design/dynamic_externs.md): `extern "lib" { ... }`,
  an extern fn bound from a shared library by name at program start; a
  proposal.

## Getting the compiler

Prebuilt compilers for Windows, Linux and macOS are on the
[releases page](https://github.com/aardappel/goose/releases); the
[README](../README.md#install-a-release) says what each platform needs
beside them, and how to [build from source](../README.md#build-and-run).
For questions and discussion, use the
[Discord](https://discord.gg/397eZdbnsR) and the
[GitHub repository](https://github.com/aardappel/goose).

## Standalone pages

These pages were made as finished HTML. They are kept in `docs/html` and
published as they are.

* [Goose, Byte by Byte](html/Goose,%20Byte%20by%20Byte.html): the
  language on one long illustrated page, with its data drawn byte by byte.
  It covers the array family, nested data kept inline, references the
  compiler proves valid, and where the speed comes from.

---

These pages are built from the repository's markdown by
[`docs/site/build.py`](site/README.md), so the same files can be read on
GitHub and here.
