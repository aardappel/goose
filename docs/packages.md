# Building programs and using packages

<details>
<summary>Table of contents</summary>

- [1. Running and building](#1-running-and-building)
- [2. Using a package](#2-using-a-package)
- [3. A program with C of its own](#3-a-program-with-c-of-its-own)
- [4. Making a package](#4-making-a-package)
- [5. The cache](#5-the-cache)

</details>

This page explains how to turn a Goose program into an executable, how to
use libraries that are not part of the compiler, and how to make such a
library. The design behind this, and how the compiler implements it, are
described in [design/packages.md](design/packages.md).

## 1. Running and building

`goose foo.goose` (or `goose run foo.goose`) compiles the program into the
compiler's own process and runs it there, with no C compiler and no build
step. `goose build foo.goose` makes an executable, `foo` or `foo.exe`,
beside the source, with a C compiler found on the machine:

```bash
goose build foo.goose && ./foo
goose build --dry-run foo.goose         # print the commands instead of running them
```

`goose toolchain` shows which C compiler a build uses, and `--cc clang`,
`GOOSE_CC` or `cc = "clang";` in `~/.goose/config` selects another. The
runtime and the C of each package are compiled once per C compiler, and an
executable is linked with the native parts of the packages it uses. A run
builds the same native parts into a library that it loads, unless the
package is compiled into the compiler. Everything the compiler produces
along the way goes under `~/.goose` (`GOOSE_HOME` moves it,
[§5](#5-the-cache)).

[The command line](command_line.md) describes the rest: every option of
run, build and check, what the compiler prints, and how to write the C
for a build system of your own.

## 2. Using a package

A *package* is a library in a directory of its own: a module named after it,
`name.goose`, further modules beside it, and possibly C. The packages the
compiler knows by name are the *blessed* ones (`src/blessed.txt`). A program
imports them like any other module:

```goose
import gfx;
import physics;
```

The first time, the compiler fetches the package from its repository at the
pinned tag into `~/.goose/pkg`, builds its C with cmake and the C compiler,
and keeps both. A compiler configured with
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
module `name.goose`; its other modules are imported with
`import name.other;`. A git source needs a ref after `@`. A tag or a commit
pins the package. A branch does too, until
`goose fetch --update program.goose` fetches it again. A path is relative
to the file that contains the import, and nothing is copied: you can edit
the library and run the program.

**Only the root file declares packages.** Every other file, including the
library's own files, imports by name alone. A library that needs another package
writes the same `import other from "…";` line in its own files, and the
compiler reads it as a *requirement*: if the root does not declare `other`,
the error says exactly what line to add to the root. If the root declares
the same package from a different ref, the compiler warns; if it declares
it from a different source, that is an error. So a program's dependencies
are flat, pinned and in one place, and a program contains only one version
of each package.

`goose fetch program.goose` fetches everything the root declares, for a
later offline session, and `--offline` makes any command fetch nothing.
When a tool that is needed is missing, the error
names it: `git` to fetch, `cmake` for a package built with CMake, and a C
compiler for any C code. A program with no C and no such packages needs
none of them.

## 3. A program with C of its own

C that belongs to the program goes in a sidecar beside the root file,
`foo.build` beside `foo.goose`:

```goose
// foo.build
c_sources = ["impl.c", "vendor/stb_impl.c"];   // compiled with the program's build
c_include = ["vendor"];
c_defines = ["IMPL_FAST=1"];
c_flags_cc = ["-mavx2"];                       // compile flags for gcc and clang; c_flags_msvc for MSVC
c_libs = ["m"];                                // system libraries, by name
c_libs_windows = ["user32"];                   // one platform's part
dylibs_linux = ["SDL2=libSDL2-2.0.so.0"];      // what an extern "SDL2" block opens here
output = "foo";                                // the executable's name, if not the file's
```

The file looks like Goose (`//` comments, `key = value;`, strings, `true`
and `false`, `[lists]`), and every `c_*` key can take a `_windows`,
`_linux` or `_macos` suffix for a part that applies to one platform only.
`goose build` compiles and links that C; `goose run` builds it into a
library that the in-process run loads. Either way, an `extern fn` in the
program can call it:

```goose
extern fn impl_sum(a: i32, b: i32) -> i32;
extern fn impl_count(s: const u8[:], c: u8) -> i64;   // a slice arrives as { data, len }
```

A single-header library needs only one `impl.c` that defines its
implementation macro and includes the header. Alternatively,
`--include shim.h` on the command line splices a header into the generated
C; this still works for small shims written against the generated types
(spec §7.10, [command_line.md](command_line.md#8-writing-the-c-yourself)).

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

A library that needs another package writes the requirement in its entry
file, `import vec_extras from "github.com/x/vec_extras@v2";`, and the
compiler checks it against the program's root as described above. The
library's own examples and tests are root files, so in them the line
declares the package, and the examples run on their own.

### The native part

A package can call a library installed on the machine or shipped beside the
program without compiling C of its own. An `extern "name" { ... }` block
declares functions to load from that shared library by name at program startup
(spec §7.10).

If the name produces the wrong library filename, map it in the sidecar's
`dylibs` list using a platform suffix, for example `dylibs_linux =
["SDL2=libSDL2-2.0.so.0"];`. A package supplies defaults for its own names. The
program's sidecar can override any of those names, with precedence determined
separately for each platform. Mapping a name that no block declares is an error.

A program that calls only these functions compiles and links no native code and
can run with `goose run` without a C toolchain. The rest of this section covers
packages that compile their own C.

A package can include headers in the program's generated C, so the C compiler
can inline small adapters into Goose functions:

```goose
// mylib.build
c_headers = ["include/adapters.h"];
```

Paths are relative to the package root, or the program's directory for its
own sidecar. As with `--include`, headers follow the generated types and
precede extern prototypes and function bodies. They can define `static inline`
implementations of `extern fn` declarations without a separate object file
or a command-line `--include`. Types from unimported modules are not available.
Headers must compile with the selected backend, including TinyCC for
`goose run`; a header-only package needs no external C toolchain for that mode.

`c_headers` takes the same platform suffixes as the other `c_*` lists. Headers
are included in list order: command-line `--include` entries, the program's
sidecar, then packages in load order. Headers include their own dependencies;
`c_include`, `c_defines` and `c_flags_*` still apply only to separately compiled
native sources, not the program's generated C.

The sidecar names the C in one of two ways. The first is plain sources,
which the compiler compiles with the given includes, defines, flags and
libraries:

```goose
// mylib.build
c_sources = ["src/mylib.c"];
c_include = ["include"];
c_libs_linux = ["m"];
```

The second is a CMake project at the package root, named by its static
library target:

```goose
// mylib.build
cmake_target = "mylib";
cmake_options = ["MYLIB_SIMD=ON"];
```

The compiler then configures and builds a small wrapper project around the
package's own CMakeLists and uses the target's complete link line, so a
package can be a thin Goose layer over an existing C library without any
CMake code written specifically for Goose. Submodules the package needs are
cloned with it. Either way, the native part is built once per C compiler
under `~/.goose/build`, and a program links it statically.

The functions that the package's `extern fn`s name must be defined by that
C (or by the C library); the runtime's own `gs_os_*` functions and similar
ones belong to the standard library. Every `.goose` file under the package
root belongs to the package, except files in a directory below the root
that has a sidecar of its own (such as an example program with its own C),
and files in the directories that the sidecar excludes:

```goose
// mylib.build
exclude = ["tools", "third_party/demo"];
```

Most values an `extern fn` passes have layouts that C can declare for
itself, such as a slice as `{ data, len }` (spec §7.10). A builder
(`T[>..]&`, such as `u8[>..]&`) is the exception: C appends to it through
the runtime's `gs_rref`, and `goose.h` gives separately compiled C access to
the runtime for this, along with `gs_bld_append` for bytes. The
compiler puts that header on the include path of every native part,
including the program's own and CMake targets, and rebuilds them all when
the header changes:

```c
#include "goose.h"
void mylib_name(gs_rref out, int64_t id) {   /* extern fn mylib_name(out: u8[>..]&, id: i64); */
    gs_bld_append(out, "item", 4);
}
```

A package's C is self-contained: two packages that must share C share a
repository, as `gfx`, `audio` and `ui` do. A package whose functions keep
one state for the process, or that may only be called from the main thread,
says so in its sidecar with `main_thread_only = true;`, and calling it from
a `thread_fn` is then a compile error.

### Using Goose types in a C header

A `c_headers` adapter uses the types Goose has already declared; it does not
redeclare them or import a C struct into Goose. Here is a complete local
package and a program that calls it:

```goose
// mylib/mylib.goose
namespace mylib;
struct Pair { lo: u32, hi: u32 }
extern fn swap(p: Pair) -> Pair = "mylib_swap";
```

```goose
// mylib/mylib.build
c_headers = ["include/adapters.h"];
```

```c
// mylib/include/adapters.h
#ifndef MYLIB_ADAPTERS_H
#define MYLIB_ADAPTERS_H

static inline mylib_Pair_g5 mylib_swap(mylib_Pair_g5 p) {
    mylib_Pair_g5 result;
    result.lo_g = p.hi_g;
    result.hi_g = p.lo_g;
    return result;
}

#endif
```

```goose
// app.goose, beside the mylib directory
import mylib from "./mylib";
fn main() {
    let p = mylib::swap(mylib::Pair { 20, 22 });
    assert(p.lo == 22 && p.hi == 20);
    print(p.lo, " ", p.hi);
}
```

`goose run app.goose` prints `22 20`; `goose build app.goose` builds the
same program with the native C compiler. Neither needs `--include` or a
separately compiled `.c` file.

The names in the header follow spec §7.10:

| Goose declaration | C spelling | Rule |
| --- | --- | --- |
| `mylib::Pair` | `mylib_Pair_g5` | namespace, `_`, type name, `_g`, namespace length |
| `Pair` without a namespace | `Pair_g` | type name followed by `_g` |
| Fields `lo`, `hi` | `lo_g`, `hi_g` | field name followed by `_g`, without the namespace |
| `swap(...) = "mylib_swap"` | `mylib_swap` | the explicit extern symbol, unchanged |

The `5` is the length of `mylib`, not an import-order number. For example,
`goosimd::Bytes32` becomes `goosimd_Bytes32_g7`. A package's import path does
not set these names; its `namespace` declaration does.

Inspect the actual declarations with `goose -o app.c app.goose`: the type
definitions appear before the adapter's `#include`, and extern prototypes
follow it. This is especially useful for generic or composite types, whose
names include their type arguments and may need collision suffixes. User
field names get `_g`; compiler-defined slice members remain `data` and `len`.

Use the generated type rather than a second C definition with similar
fields. These naming rules are not a stable binary ABI contract: matching
names or fields does not establish compatible layout, alignment, or calling
conventions with an independently compiled C library. Goose structs are
packed, not naturally aligned C structs. An adapter for an existing C API
must account for that API's representation rather than assume the types are
interchangeable. Separately compiled `c_sources` do not see the generated
declarations; this example relies on compiling the header and the Goose code
together in the same C translation unit.

### Files a package needs at run time

`embed_file("path")` reads a file when the program is compiled (the path is
relative to the file containing the call) and gives its bytes as a
`const u8[:]` into static data, such as a font, a table or a shader source.
This is how a package ships data without having to deploy separate files.
Files read at run time belong to the program (`resource_dir()`,
`docs/stdlib.md`).

### Publishing

Push the repository, tag a version, and tell users the line to write:

```goose
import mylib from "github.com/you/mylib@v1";
```

There is no registry to register with. A tag can move and a branch always
does, so users who need a build to be reproducible pin a commit. Code from
a fetched package runs on the user's machine when its C is built, as with
any build script; pinning a version expresses trust in that code.

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

Deleting it loses nothing that a build cannot recreate.
