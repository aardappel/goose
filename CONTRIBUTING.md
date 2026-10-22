# Contributing to Goose

Goose takes changes as pull requests against `master` on
[aardappel/goose](https://github.com/aardappel/goose). This page is how to
build the compiler, where a test goes, and what the C++ looks like.
Questions about the language itself belong on the
[issue tracker](https://github.com/aardappel/goose/issues) or the
[Discord](https://discord.gg/397eZdbnsR) before they become code.
Open items in the language are
[Appendix B of the specification](docs/goose_spec.md#appendix-b-todo--open-items).

## Get the source

Fork the repository, clone your fork, and name the original `upstream`:

```bash
git clone https://github.com/<you>/goose
cd goose
git remote add upstream https://github.com/aardappel/goose.git
git fetch upstream
git checkout -b your-branch upstream/master
```

`<you>` is your GitHub user. A branch starts from current `upstream/master`.
Push the branch to your fork and open the pull request against
`aardappel/goose` `master`.

## Build

You need CMake 3.27 or later, a C++20 compiler, and Python 3. A Debug build
keeps the compiler's own assertions on, which is what CI uses:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

The compiler is `build/goose` (`build/Debug/goose.exe` with the Visual Studio
generator). It finds the standard library in the source tree it was built
from. Leave `-DGOOSE_BUILTIN_LIBS` unset until the change touches `gfx`,
`audio`, `ui`, or `physics`. Those four are packages in
[goose_gfx](https://github.com/aardappel/goose_gfx) and
[goose_physics](https://github.com/aardappel/goose_physics); passing `all`
fetches them at configure time. The [README](README.md#build-and-run) has
`goose build`, the in-process run, and the flags.

Check one program without running it:

```bash
build/goose check path/to/file.goose
```

## Tests

The suite and how its fixtures are laid out are
[docs/testing.md](docs/testing.md). CI runs this on Windows, macOS, and
Linux, plus a Linux sanitizer job and the VS Code extension's tests:

```bash
python test/run_tests.py --exe build/goose --profile baseline --cc native
```

On Windows the executable is `build/Debug/goose.exe`.

Put a fixture in the `test/` directory that matches what it exercises:
`syntax/`, `typing/`, `lifetimes/`, `codegen/`, `optimizer/`, `runtime/`,
`storage/`, `threads/`, `stdlib/`. A program that should run needs
`test/expected/<name>.out`, an empty file when it prints nothing. A program
that should be rejected goes in `errors/` (the parser and the resolver) or
`errors_tc/` (the typechecker) and carries one or more
`// error: <substring>` lines. The substring is the reason, not a file path,
a line number, or a specification section. A bounds-check fixture marks the
checks that should disappear or remain with `// bce:elide` and `// bce:keep`.

A change that alters what a program means also updates
[docs/goose_spec.md](docs/goose_spec.md). Update the
[reference](docs/reference.md) or the [tutorial](docs/tutorial.md) when a
reader would look there. Every Goose code block in the reference is compiled
by the suite.

## The compiler

The pipeline and the file each pass lives in are
[docs/implementation.md](docs/implementation.md). Read the section for the
pass you are changing, and the specification section it cites, before
editing.

The source is C++20, in namespace `goose`, indented four spaces. Types and
functions are PascalCase (`TypeCheckProgram`, `FnSpec`). Fields are
snake_case (`fileidx`, `varmode`). Enumerators carry a prefix (`TY_ARRAY`,
`A_GROW`, `IS_I64`). Comments are complete sentences and cite a rule as
`§7.8` when they state one.

`Ast` owns every node in append-only tables. The trees point at each other
and nodes have no destructors. Each pass is a virtual method on `Node`, and
the per-node bodies of one pass live together in one header. A new syntax
form is wired through parse, dump, clone, typecheck, optimize, bounds-check
elimination, and code generation.

Match the file you are editing. The layout follows the Lobster compiler, and
there is no formatter configuration, so a patch keeps the surrounding
indentation and names. `third_party/tinycc/` is the imported in-process
backend. The notes in `docs/design/` record the reasoning at the time they
were written. The specification and the reference are what the language is
now, and a design note stays as it was written.

## Pull requests

One change per pull request. The description says what a program does now,
the specification section when there is one, and why the change is sound.
Look through the open issues and open pull requests first, so the work is
not already in flight. CI builds a Debug compiler and runs the suite on all
three desktop platforms, and runs an address-sanitizer and
undefined-behavior build on Linux.
