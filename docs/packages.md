# Building programs and using packages

How to turn a Goose program into an executable, how to use libraries that
are not part of the compiler, and how to make one. The design behind it,
and the compiler's side of it, is in [design/packages.md](design/packages.md).

## 1. Running and building

`goose foo.goose` (or `goose run foo.goose`) compiles the program into the
compiler's own process and runs it there: no C compiler, no build step, the
arguments after `--` go to the program. `goose build foo.goose` makes an
executable, `foo` or `foo.exe` beside the source:

```bash
goose build foo.goose && ./foo
goose build -o out/foo foo.goose        # the executable elsewhere
goose build --debug foo.goose           # GS_DEBUG checks, debug information, no C optimization
goose build --standalone foo.goose      # the runtime inside the one C file
goose build --dry-run foo.goose         # print the commands instead of running them
goose build -v foo.goose                # print them as they run
```

`build` finds a C compiler on the machine (`goose toolchain` says which:
MSVC or clang on Windows, cc, gcc or clang elsewhere; `--cc clang`,
`GOOSE_CC`, or `cc = "clang";` in `~/.goose/config` choose), compiles the
runtime once per compiler into `~/.goose`, and links the program with the
native parts of every package it uses. `goose check foo.goose` only
typechecks. A build defaults to `-O2`, a run to `-O1`.

Everything the compiler makes on the way goes under `~/.goose`
(`GOOSE_HOME` moves it): fetched packages, their builds, the runtime
objects and each program's C. `goose build --dry-run` shows the commands
with their paths, for a Makefile or CMake project that wants to drive the
C compiler itself.

## 2. Using a package

A *package* is a library in a directory of its own: a module named after it,
`name.goose`, further modules beside it, and possibly C. The packages the
compiler knows by name are the *blessed* ones (`src/blessed.txt`): a program
imports them like any module,

```goose
import gfx;
import physics;
```

and the compiler fetches the package from its repository at the pinned tag
the first time, into `~/.goose/pkg`, builds its C with cmake and the C
compiler, and keeps both. A compiler configured with
`-DGOOSE_BUILTIN_LIBS=all` has the blessed packages compiled in and runs
such a program with nothing else installed; `goose toolchain` lists what is
built in.

Any other package, or another version of a blessed one, is declared in the
program's *root file* (the one named on the command line), with its source:

```goose
import mylib from "joe/mylib@v2";                  // GitHub, at tag v2
import mylib from "github.com/joe/mylib@3f2c1a9";  // any host, at a commit
import mylib from "https://git.example.com/x/mylib.git@main";   // any git URL, at a branch
import mylib from "../mylib";                      // a directory, used in place
```

`import name from "source";` declares the package `name` and imports its
module `name.goose`; its other modules are `import name.other;`. A git
source needs a ref after `@`: a tag or a commit is a pin, and a branch is
one too until `goose fetch --update program.goose` fetches it again. A path
is relative to the file that writes it, and nothing is copied: edit the
library and run the program.

**Only the root file declares packages.** Every other file, the library's
own included, imports by name alone. A library that needs another package
writes the same `import other from "…";` line in its own files, and the
compiler reads it as a *requirement*: if the root does not declare `other`,
the error says exactly what line to add to the root. The root declaring the
same package from a different ref gets a warning; from a different source,
an error. So a program's dependencies are flat, pinned, and in one place,
and one version of each package exists in a program.

`goose fetch program.goose` fetches everything the root declares, for an
offline session later. The tools involved are reported by name when they
are missing: `git` to fetch, `cmake` for a package built with CMake, a C
compiler for any C at all. A program with no C and no such packages needs
none of them.

## 3. A program with C of its own

C that belongs to the program goes in a sidecar beside the root file,
`foo.build` beside `foo.goose`:

```goose
// foo.build
c_sources = ["impl.c", "vendor/stb_impl.c"];   // compiled with the program's build
c_include = ["vendor"];
c_defines = ["IMPL_FAST=1"];
c_libs = ["m"];                                // system libraries, by name
c_libs_windows = ["user32"];                   // one platform's part
output = "foo";                                // the executable's name, if not the file's
```

The file looks like Goose (`//` comments, `key = value;`, strings, `true`
and `false`, `[lists]`), and every `c_*` key takes a `_windows`, `_linux`
or `_macos` suffix for a platform-only part. `goose build` compiles and
links that C; `goose run` builds it into a library the in-process run
loads. Either way an `extern fn` in the program reaches it:

```goose
extern fn impl_sum(a: i32, b: i32) -> i32;
extern fn impl_count(s: const u8[:], c: u8) -> i64;   // a slice arrives as { data, len }
```

A single-header library is one `impl.c` that defines its implementation
macro and includes the header. A header spliced into the generated C
instead, `--include shim.h` on the command line, still works for small
shims written against the generated types (spec §7.10).

## 4. Making a package

A package is a git repository (or any directory) laid out like this:

```
mylib/
  mylib.goose        the entry: `namespace mylib;`, its declarations
  mylib.build        the sidecar, when there is C to build (optional)
  extra.goose        another module: `import mylib.extra;`
  src/, include/     the C, if any
  examples/, test/   programs of its own, each a root file
  README.md
```

Inside the package, `import helper;` finds `helper.goose` under the
package's root, `import .helper;` finds it relative to the importing file,
and `import std;` reaches the standard library. By convention a package's
files declare its name as their namespace.

A library that needs another package writes the requirement in its entry,
`import vec_extras from "github.com/x/vec_extras@v2";`, which the compiler
checks against the program's root as described above. Its own examples and
tests are root files, so there the line is honored and the examples run on
their own.

### The native part

The sidecar names the C, one of two ways. Plain sources, which the compiler
compiles with the includes, defines, flags and libraries given:

```goose
// mylib.build
c_sources = ["src/mylib.c"];
c_include = ["include"];
c_libs_linux = ["m"];
```

Or a CMake project at the package root, by the name of its static library
target:

```goose
// mylib.build
cmake_target = "mylib";
cmake_options = ["MYLIB_SIMD=ON"];
```

The compiler then configures and builds a small wrapper project around the
package's own CMakeLists and takes the target's whole link line, so a
package can be a thin Goose layer over an existing C library with no CMake
written for Goose's sake. Submodules the package needs are cloned with it.
Either way the part is built once per C compiler under `~/.goose/build`,
and a program links it statically.

The functions the package's `extern fn`s name must be defined by that C (or
by the C library); the runtime's own `gs_os_*` and friends belong to the
standard library. A package's C is self-contained: two packages that must
share C share a repository, as `gfx`, `audio` and `ui` do. A package whose
functions keep one state for the process, or that may only be called from
the main thread, says so in its sidecar, `main_thread_only = true;`, and a
`thread_fn` calling it is then a compile error.

### Files a package needs at run time

`embed_file("path")` reads a file when the program is compiled, relative to
the file with the call, and gives its bytes as a `const u8[:]` into static
data: a font, a table, a shader source. That is how a package ships data
without a deployment story. Files read at run time belong to the program
(`resource_dir()`, `docs/stdlib.md`).

### Publishing

Push the repository, tag a version, and tell users the line to write:

```goose
import mylib from "github.com/you/mylib@v1";
```

There is no registry to register with. A tag can move and a branch always
does, so users who need a build to be reproducible pin a commit. Code from
a fetched package runs on the user's machine when its C is built, as with
any build script; that is the trust a pin expresses.

## 5. The cache

```
~/.goose/
  config                          cc = "clang";
  pkg/<host>/<owner>/<repo>/<ref>/   fetched packages, submodules included
  build/pkg/.../<toolchain>/      their native parts, per C compiler
  build/runtime/<hash>/...        the runtime object, per C compiler and configuration
  build/prog/<name>-<hash>/       each program's C, objects and logs
  toolchain/                      what finding the C compiler took (vcvars on Windows)
```

Deleting it loses nothing that a build does not make again.
