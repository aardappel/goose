# Builds and packages

<details>
<summary>Table of contents</summary>

- [1. Decisions](#1-decisions)
- [2. What there is today](#2-what-there-is-today)
- [3. The command line](#3-the-command-line)
- [4. Packages](#4-packages)
- [5. Native parts](#5-native-parts)
- [6. Building programs](#6-building-programs)
- [7. The compiler's own build](#7-the-compilers-own-build)
- [8. Testing](#8-testing)
- [9. Documentation](#9-documentation)
- [10. The plan](#10-the-plan)
- [11. Deferred and open](#11-deferred-and-open)

</details>

This document describes how `goose build` replaces the hand-written C
compiler invocations, and how a program pulls in libraries, written in Goose
or C, from git repositories. It is the design and the migration plan,
written against the tree at `de24a156`, following the discussion that
settled the decisions in §1. §10 gives the order in which it was
implemented, one working commit at a time. The user-facing documentation is
[docs/packages.md](../packages.md).

## 1. Decisions

* **One executable, with verbs.** `goose build foo.goose`, `goose run
  foo.goose`, `goose check foo.goose`, `goose fetch foo.goose`, `goose
  toolchain`, `goose version`. The bare `goose foo.goose` form stays and
  means `run`. Every flag that exists today stays, as the plumbing the verbs
  are built on, and `goose build --dry-run` prints the commands for anyone
  with a build system of their own.
* **Dependencies are declared in the root source file**, as imports with a
  source: `import gfx from "aardappel/goose_gfx@v1";`. Every other file,
  including the library's own files, imports by name only. Information that
  is not code (C sources to compile, system libraries, CMake options) goes
  in an optional sidecar file beside the entry file, `foo.build` beside
  `foo.goose`.
* **Dependencies are flat.** The root names every package the program
  needs, each one pinned. A library states what it needs with the same
  import form. The compiler checks this against the root, and if the root
  does not declare it, reports an error that says what line to add. Exactly
  one version of a package exists in a program, as whole-program
  compilation and global namespaces require anyway. Lockfiles, version
  ranges and a registry are not used: a source is a git repository (GitHub
  by default), pinned to a tag or commit.
* **AOT output is statically linked.** Shared libraries exist only so the
  JIT can run a program against a native library that was not compiled into
  the compiler.
* **The compiler's own build can compile packages in.** `cmake
  -DGOOSE_BUILTIN_LIBS=all` fetches the blessed packages with CMake's own
  fetcher and links them into `goose`, which then runs their programs with no
  git, CMake or C compiler on the machine. The default is none: the core
  compiler builds quickly on its own, and the first program that imports
  `gfx` fetches and builds it into the user's cache. This happens once, and
  the result is shared by every checkout. The README's build instructions
  use `all`.
* **TinyCC is vendored and mandatory.** The JIT is core to Goose; the
  submodule becomes a copied subtree like `cute_spirv`, and every `have_jit`
  fallback is removed.
* **The blessed native modules leave this repository one by one**, physics
  first (no compiler-side code), then ui, then gfx and audio together, since
  they share one SDL build. Audio, and probably ui, live in the gfx
  repository as further modules of that single package.
* **Tools are called, not replaced.** git, cmake and a C compiler are found
  on the machine, and a missing tool gives a readable error that names the
  tool, the package that needs it and what to do. There are no prebuilt
  binaries in v1.
* **The cache is global**: `GOOSE_HOME`, by default `~/.goose`. A program's
  executable is written beside its root source; everything intermediate
  goes under the cache.
* **Imports resolve against the importing file's package**, not the program's
  directory. For a single-package program nothing changes.
* **`embed_file(path)`** joins `embed_shader` as a compile-time read into
  static data, so a library can include a font or a table without having to
  deploy a separate file.
* **Deferred**: a compiler version number (a commit count baked in when
  needed, never a hand-assigned number), lockfiles, automatic transitive
  dependencies, prebuilt binaries, fetching without git, and the runners
  calling `goose build` (last, once it is solid).

## 2. What there is today

A JIT run is `goose foo.goose`. An AOT build of the same program is

    goose --emit-runtime goose_runtime.c && cc -O2 -c goose_runtime.c
    goose -o foo.c foo.goose && cc -O2 foo.c goose_runtime.o -o foo -lm -pthread

and with graphics, physics and ui on top

    cc cube.c goose_runtime.o -o cube @$(goose --ui-link cc) @$(goose --gfx-link cc) @$(goose --physics-link cc)

in that order, because GNU ld resolves static archives left to right. The
logic for doing this is in `scripts/toolchain.py`, which the test, sample
and benchmark runners share. It finds MSVC through vswhere and imports the
vcvars64 environment, finds gcc and clang on PATH, writes flags in the form
each driver style expects, emits the runtime once and compiles it per
configuration, and appends the response files CMake wrote. The compiler
itself never starts a process.

A native stdlib module (gfx, audio, physics, ui) touches eight places:

* `stdlib/<m>.goose`;
* the C layer in `src/<m>/`, with an X-macro list of its functions in
  `<m>_api.h`;
* `cmake/<m>.cmake`;
* a submodule under `third_party/`;
* `src/<m>.h`, with `have_<m>` and the response-file lookup;
* symbol registration in `jit.h`;
* prefix sniffing of `gs_<m>_` in `codegen_calls.h` and `typecheck.h`
  (which native layer a program calls, and the main-thread-only rule for
  `thread_fn`);
* the runners' skip logic.

The JIT gets a layer's functions by address, by calling `tcc_add_symbol` on
functions linked into `goose` itself. This is why a layer the compiler was
not built with cannot be run. The layers never call back into the program
(`gs_gfx_error` fills a caller's slice), so they can be shared libraries.

`import a.b;` resolves `a/b.goose` against the root program's directory, then
the stdlib; `import .a.b;` resolves against the importing file. A library
using the first form would resolve against the program. All names are
public, and namespaces are global, so two libraries declaring
`namespace util;` collide. `embed_shader` is the only compile-time file
read. There is no compiler version number; the runtime's text hash gates
linking.

## 3. The command line

The command line is `goose <verb> [options] file.goose [-- program args]`.
If the first argument is not a verb, the command line is read exactly as
today, so `goose foo.goose`, `goose -o foo.c foo.goose`,
`goose --emit-runtime r.c` and the rest keep working; `goose foo.goose` is
`goose run foo.goose`.

| Verb | Does |
|---|---|
| `run` | fetches what the root declares, builds the native parts the program reaches, compiles the program in-process (TinyCC) and runs it; `-O2` by default |
| `build` | the same, then writes the C, compiles it and links an executable beside the root source (`foo`, `foo.exe`); `-O2` by default, Goose's inlining, while the C compiler optimizes at every level |
| `check` | fetches the Goose sources it needs and typechecks; never builds C |
| `fetch` | fetches every package the root declares, and builds nothing; `--update` re-fetches refs that are not commit hashes |
| `toolchain` | prints the C compiler, cmake and git it would use, and why, or what it could not find; `--json` for scripts |
| `version` | the compiler's version (§11: a commit count, when there is one) and the runtime hash |

Options of `build`:

* `-O0`/`-O1`/`-O2`: Goose inlining; the C compiler optimizes at every
  level.
* `--debug`: `-DGS_DEBUG=1` for the generated C, and debug information and
  no optimization for the C compiler; packages stay release builds.
* `--standalone`: no runtime object.
* `-o <exe>`.
* `--cc <name|path>` (§6.1).
* `--dry-run`: print every command and write nothing.
* `-v`: print every command as it runs.
* The options shared with the plumbing: `-D`, `--include`,
  `--stack-reserve`, `--stdlib`, and `--offline`. `--offline` fetches
  nothing: a package not in the cache is an error at its import, which
  `fetch` resolves. It is what an editor's checks pass.

`run` takes the same options except `-o`, `--standalone` and `--debug`.

## 4. Packages

### 4.1 Terms

A **package** is a directory with an entry file `<name>.goose` at its root,
optionally a sidecar `<name>.build` beside it (§4.7), optionally C sources or
a CMake project (§5), and whatever else it wants. A program is a package
whose entry is its root file, whose package root is that file's directory,
and whose sidecar is `foo.build`. A **module** is one `.goose` file;
`import pkg.sub;` is the module `<pkg root>/sub.goose`. The standard
library is a package, found as today (`--stdlib`, `GOOSE_STDLIB`, then
beside the compiler). Its modules that have native parts move out of it
(§7), and the pure ones stay.

A package's name is the name the root imports it by. It names the cache
directory and the entry file, and by convention the namespace its files
declare, which nothing enforces.

### 4.2 Declaring a dependency

```goose
import gfx from "aardappel/goose_gfx@v1";                 // GitHub, tag v1
import mylib from "github.com/joe/mylib@3f2c1a9";         // a host, a commit
import other from "https://git.example.com/x/other.git@main";  // any git URL, a branch
import local from "../local";                             // a path, relative to this file
```

`import name from "source";` declares package `name`. `from` is read as the
contextual identifier it already is in `return … from f`. The source is one
string:

* `host/owner/repo@ref` is cloned from `https://host/owner/repo`. A source
  with two components, `owner/repo@ref`, means `github.com/owner/repo`.
  Anything with `://` or a `user@host:` prefix is passed to git unchanged.
* `@ref` is required for a git source. A tag or a commit hash is a pin; a
  branch name is accepted and behaves like a pin until
  `goose fetch --update`. The docs give this as the reason to prefer tags.
* A path (`.`, `..`, `./`, `../`, `/`, or a drive letter) is a directory
  used in place, relative to the importing file. No git is involved. This
  form is for developing a library beside the program that uses it.

The grammar line becomes

    import := "import" "."? ident ("." ident)* ("from" strlit)? ";"

### 4.3 Root only, and flat

A sourced import is **honored only in the root file**. Anywhere else, a
sourced import is a **requirement**: when the file is compiled as part of a
program, the root must declare a package of that name, or the compiler
reports an error that names the fix:

    joe/mylib needs the package vec_extras: add
        import vec_extras from "github.com/x/vec_extras@v2";
    to foo.goose (packages are declared by the root file only)

If the root declares the required name from a different source, that is an
error that names both sources. If it declares the same source at a different
ref, that is a warning that names both refs, since it is the root that knows
which versions work together. A
requirement's package is loaded only when the root declares it; nothing is
fetched just because a library asked for it.

So a program's dependencies are flat and visible: the root lists everything,
and a library documents its needs in a form the compiler reads. A library's
examples and tests are roots of their own, so the same line in them is
honored there. If transitive dependencies are ever wanted, the lines are
already in place: a `goose deps add` command or an `--auto-deps` flag would
do what the error message says, with no change of format.

Two packages' C parts cannot depend on each other (§5.5); a library's C
dependencies (SDL inside the gfx package) are inside the library and
invisible to the root.

### 4.4 Resolving `import`

For `import a.b;` (no source) in a file of package P:

1. the file `a/b.goose` under P's root (`import a;` is `a.goose`); for a
   program this is the root's directory, which is what happens today;
2. a package named `a` that the root declared, or a blessed name (§4.5):
   `<a's root>/b.goose`, and `import a;` is `<a's root>/a.goose`;
3. the standard library's `a/b.goose`.

A file under P's root with the name of a package the root declared
explicitly is an error (the root meant the package); a local file still
shadows a blessed name or a stdlib module, as it shadows the stdlib today.
`import .a.b;` is relative to the importing file, unchanged.

Spec §11.1 changes accordingly: "resolved relative to the *importing file's
package root* (the root file's directory for the program itself), then among
the root's declared packages, then in the standard library".

### 4.5 Blessed packages

The blessed packages are listed in a table in the compiler,
`src/blessed.txt`, which CMake generates into the binary and also reads for
§7:

    # import-name  package  source                             ref
    physics        physics  github.com/aardappel/goose_physics  v1
    gfx            gfx      github.com/aardappel/goose_gfx      v1
    audio          gfx      github.com/aardappel/goose_gfx      v1
    ui             gfx      github.com/aardappel/goose_gfx      v1

A naked `import gfx;` that step 1 of §4.4 does not satisfy, and that the
root did not declare, is read as if the root had written the table's line;
`import audio;` is module `audio` of package `gfx`. A root may still
declare one of these names itself with any source, to try a fork or
another ref: an explicit declaration always takes precedence over the table
and over a compiled-in copy. The pins are updated with the compiler: when a
blessed package's API changes, the compiler that expects the new API bumps
the ref.

### 4.6 Fetching and the cache

`GOOSE_HOME`, default `~/.goose` (`%USERPROFILE%\.goose` on Windows):

    pkg/<host>/<owner>/<repo>/<ref>/                 the checkout, submodules included
    build/pkg/<host>/<owner>/<repo>/<ref>/<toolchain>/   its native part built: objects or archives,
                                                     the JIT shared library, link-cc.rsp, link-msvc.rsp, logs
    build/pkg/local/<hash of the absolute path>/<toolchain>/   the same for a path source
    build/runtime/<runtime hash>/<toolchain>-<config>/goose_runtime.o
    build/prog/<stem>-<hash of the root's absolute path>/   a program's C, objects, logs
    config                                           optional settings (§6.1)

A tag or branch is `git clone --depth 1 --branch <ref> --recurse-submodules
--shallow-submodules <url>`; a commit hash is `git init`, `git fetch
--depth 1 origin <hash>`, `git checkout FETCH_HEAD`, then the submodules.
A fetch is written to `<ref>.tmp` and renamed when complete, so an
interrupted fetch leaves no partial checkout behind. Fetches and updates
take a cross-process lock on the persistent `<ref>.lock` file beside the
checkout; after waiting, a fetch rechecks the cache before touching
`<ref>.tmp`. `run`, `build` and `check` fetch what is missing with one line
of notice per package; `fetch` fetches everything up front. A checkout at a hash or tag is never touched
again; `fetch --update` re-fetches branch refs. Path sources are used where
they are.

Without git on PATH:

    cannot find `git` on PATH, needed to fetch the package gfx from
    github.com/aardappel/goose_gfx@v1. Install git, or clone that repository
    yourself and declare it in foo.goose as
        import gfx from "<the directory>";

Building a fetched package runs its C compiler, and a CMake package runs
CMake: code from the repository runs on the machine at build time, as with
every language's build scripts. Pinning a commit hash protects against a
tag being moved to a different commit.

### 4.7 Layout and the sidecar

A library repository, minimally:

    mylib.goose          the entry: `namespace mylib;`, its declarations, its requirements
    mylib.build          the sidecar, when the package has build settings
    sub.goose            further modules, `import mylib.sub;`
    src/*.c, include/    its C, if any
    examples/, test/     programs that are roots of their own

The sidecar is parsed by the Goose lexer, so it looks like Goose: `//`
comments, `key = value;`, strings, `true`/`false`, integers and
`[a, b, c]` lists. Unknown keys are errors naming the known ones.

| Key | In | Meaning |
|---|---|---|
| `c_sources` | both | C files, relative to the package root, compiled by the discovered C compiler (§5.1); a single-header library is one `impl.c` that defines its implementation macro and includes the header |
| `c_include` | both | include directories |
| `c_headers` | both | headers, relative to the package root, included in the consumer's generated C after its types and before extern prototypes and function bodies, as with `--include`; these alone create no native part |
| `c_defines` | both | `NAME` or `NAME=VALUE` |
| `c_flags_cc`, `c_flags_msvc` | both | extra compile flags per driver style |
| `c_libs` | both | system libraries, by name (`m`, `user32`); the compiler writes them in each driver style's form |
| `cmake_target` | both | the package root holds a CMake project, and this is the static library target to build and link (§5.2) |
| `cmake_options` | both | `NAME=VALUE` cache entries for its configure |
| `dylibs` | both | `name=spelling` entries: the file name to open for the shared library that an `extern "name" { ... }` block binds from (spec §7.10). A package's entries map its own names and serve as defaults; the program's entries can map a name for any package, and take precedence per platform. A name that no block declares is an error |
| `main_thread_only` | both | its functions may not be reached from a `thread_fn` (§5.4) |
| `exclude` | both | directories, relative to the package root, whose `.goose` files are not the package's own: the JIT library's symbol table (§5.3) does not look for extern fns in these directories, nor in any directory below the root that holds a sidecar of its own |
| `output` | program | the executable's name, relative to the root's directory |

Every `c_*` key takes a `_windows`, `_linux` or `_macos` suffix for a
platform-only part. A program's sidecar lists its own C with the same keys;
a program that includes a library's C through `--include` keeps working,
since the header is spliced into the generated C as it is today.

`c_headers` preserves list order, after command-line headers, with the
program's sidecar first and packages in load order. The headers may use
types of declared externs even when unused, but not types from unimported
modules. They must support the consumer's C backend; a header-only package
can run through TinyCC without an external toolchain. The native source
settings (`c_include`, `c_defines`, `c_flags_*`) do not apply to these headers.

## 5. Native parts

A package with nonempty `c_sources` or a `cmake_target` has a native part. It is
built once per toolchain (§6.1) under `build/pkg/…/<toolchain>/`, in the
release configuration, with the static C runtime on Windows. It is built in
two forms: what an AOT program links, and a shared library the JIT loads.
The first `run` or `build` that reaches the package builds the first form,
and the first `run` builds the library, from the same objects; `check`
never builds.

Inputs are stamped. A path package is rebuilt when any of its C sources (by
path, modification time and size), its sidecar or the set of its extern fns
changes. Every package, including a git checkout, is rebuilt when `goose.h`
changes. `goose.h` is the header the compiler gives native C for the values
that C cannot declare itself (a builder's `gs_rref`, spec §7.10).

Every program and process that reaches a package builds it in that one
directory, under a cross-process lock on its persistent `build.lock`. A
process that had to wait for the lock checks the stamp again before
building anything. A build that finds the stamp current reads the response
file and the stamp without the lock. Both are written beside their final
paths and renamed into place, the stamp last.

### 5.1 Plain C sources

Each file of `c_sources` is compiled to an object with the package's
includes, defines and flags. The object is named after the file's name and
a hash of its path, so that two files with the same name in different
directories do not share an object. The AOT form is the objects
themselves, listed in the response file ahead of `c_libs`, so no archiver
is needed. The JIT form links the objects, `goose_symbols.c` (§5.3) and
the system libraries into `goose_jit_<name>.{dll,so,dylib}` through the
same driver (`-shared`, or `/LD`). Only `.c` is compiled this way; C++ goes through CMake.

### 5.2 CMake packages

For `cmake_target`, the compiler writes a wrapper project into the package's
build directory and configures and builds that, never the package's own
CMakeLists directly:

```cmake
cmake_minimum_required(VERSION 3.20)
project(goose_pkg_physics C)
# C++, and on macOS Objective-C and Objective-C++, where there is a compiler.
include(CheckLanguage)
check_language(CXX)
if(CMAKE_CXX_COMPILER)
    enable_language(CXX)
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
add_subdirectory("<package root>" pkg EXCLUDE_FROM_ALL)
if(NOT TARGET <cmake_target>)
    message(FATAL_ERROR "... names the CMake target <cmake_target>, which ... does not define")
endif()
# The JIT library: the symbol table pulls what it names out of the target.
add_library(goose_jit_physics SHARED goose_symbols.c)
target_link_libraries(goose_jit_physics PRIVATE <cmake_target>)
# A probe whose link line is the package's whole link closure.
add_executable(goose_linkprobe goose_linkprobe.c)
target_link_libraries(goose_linkprobe PRIVATE <cmake_target>)
```

The wrapper is configured with `-DCMAKE_BUILD_TYPE=Release`, the discovered
C and C++ compilers, the `cmake_options`, and a generator (Ninja if it is on
PATH, else the platform default), and built with `--config Release`. The
configure command that last succeeded is kept beside the build. Any other
command (from an edited `cmake_options`, or after a configure that failed)
configures again from a fresh cache. The wrapper and its two C files are
written only when their contents change, so that a build with nothing new
compiles and relinks nothing. This matters because on Windows, a JIT
library that another process has loaded cannot be overwritten.

The response file for AOT programs is the link closure of `<cmake_target>`:
its archive, every static library it depends on, and the system libraries
and link options behind them (`-lm`, `-framework Cocoa`, `user32.lib`),
in order. CMake has no generator expression for a transitive closure; today
`cmake/sdl.cmake` extracts the closure from SDL's collector target by hand.
The general solution is CMake's file API. Before configuring, the compiler
writes `.cmake/api/v1/query/codemodel-v2` into the build directory. It then
reads the reply's target object for `goose_linkprobe`, whose
`link.commandFragments` are the link line (with roles `libraries`,
`libraryPath`, `frameworkPath` and `flags`) as the generator would pass
them. Paths relative to the build directory are made absolute. This needs a
small JSON reader in the compiler. In this way anything with a CMakeLists
becomes a Goose package, including an upstream library bound directly
(`cmake_target = "box3d"`), without writing any CMake specifically for
Goose.

Without cmake on PATH:

    cannot find `cmake` on PATH, needed to build the C part of the package
    physics (github.com/aardappel/goose_physics@v1, target goose_physics).
    Install CMake 3.20 or later.

### 5.3 The symbol table and the JIT

The JIT needs the address of every C function the program calls. Today
`jit.h` gets them from the layer's X-macro list, which is why only compiled-in
layers can be run. Instead, the compiler generates a C file from the
package's own declarations, and every package's JIT library is linked with
that file:

```c
/* goose_symbols.c, generated for the package physics. */
typedef struct { const char *name; const void *addr; } goose_symbol;
void gs_phys_create_world(void); void gs_phys_step(void); /* … */
GOOSE_EXPORT const goose_symbol *goose_symbols(void) {
    static const goose_symbol table[] = {
        { "gs_phys_create_world", (const void *)&gs_phys_create_world },
        { "gs_phys_step", (const void *)&gs_phys_step },
        /* … */
        { 0, 0 } };
    return table;
}
```

The names are the C symbols of every `extern fn` in every `.goose` file
under the package root, except in the sidecar's `exclude` directories and
in directories below the root that hold a sidecar of their own (§4.7). The
compiler parses these files for this purpose. The prototypes are dummies,
since taking a function's address needs no signature. Each library exports
one symbol, so Windows needs no export lists. Linking the table pulls
exactly the objects that define those functions, and their dependencies,
into the shared library.

A JIT run then handles each package whose extern functions the program's
code reaches (codegen already collects `usedexterns`, and each extern's
declaring file knows its package). It loads `goose_jit_<name>` with
`LoadLibrary` or `dlopen`, calls `goose_symbols`, and passes every entry to
`tcc_add_symbol` before relocation. Today `AddGfxSymbols` and its siblings
do this from static tables. A package compiled into the compiler (§7) has
the same table linked in under the name `goose_symbols_<name>`, listed in a
generated `builtin_packages.h`. A builtin package needs no library; a
declared package always goes through its library. `NativeLayers` and the
`gs_<m>_` prefix sniffing are removed.

A package may declare an `extern fn` only for a function that its own C
part, or the C library, defines. The runtime's `gs_os_*` and `gs_sqrt`
belong to `os` and `math`, which stay in the stdlib. A package that
declared one of them would fail to link its JIT library, and the error
names the symbol and says that it belongs to the runtime.

### 5.4 Main-thread-only packages

`main_thread_only = true;` in the sidecar replaces the hard-coded prefix
list in `CheckThreadGlobals`. A call from a `thread_fn` to an extern
declared in such a package gives the same error as today, naming the
package. gfx, audio, physics and ui all set it.

### 5.5 Rules

* A package's C part is self-contained: it may not include another package's
  headers or call its functions. Cross-package work happens in Goose, as ui
  draws through gfx today. Packages that must share C share a repository.
* Link order follows the requirements of §4.3: a package is linked before
  the packages it requires, so ui's response file precedes gfx's, which is
  what the GNU linker needs. Otherwise packages are linked in the root's
  declaration order.
* A package is built in the release configuration, once per toolchain;
  `--debug` affects only the program and the runtime object. The program's
  own `c_sources` follow `--debug`, and their debug build is kept apart
  from their release build (`native/<toolchain>-debug` in the program's
  build directory). A CMake part, including the program's own, is
  always built in the release configuration.
* The archives built this way link into a program built by the same
  toolchain. The toolchain id (§6.1) is in the build path, so switching
  `--cc` builds the package again rather than mixing toolchains.

## 6. Building programs

### 6.1 Toolchains

The C compiler is chosen in this order: `--cc`, then `GOOSE_CC`, then
`cc = "…";` in `GOOSE_HOME/config` (the sidecar format), then discovery:

* **Windows**: `cl` if it is already on PATH (a developer prompt); else
  `clang-cl` or `clang` from the Visual Studio LLVM directory or PATH (both
  find the MSVC headers and the Windows SDK by themselves, with no
  environment needed); else `cl` through vswhere and `vcvars64.bat`. That
  environment is imported the way `toolchain.py` does it, and cached in
  `GOOSE_HOME` keyed on the installation's path and version, since
  importing it takes a second or two.
* **Linux**: `cc`, then `gcc`, then `clang`, on PATH.
* **macOS**: `cc` (Apple clang).

A single command finds its toolchain once and builds everything with it:
the native parts, the runtime object and the link. "On PATH" means the
PATH the compiler was started with. Building a CMake package on Windows
imports the vcvars64 environment, which puts `cl` on PATH, but discovery
finds the same toolchain whether or not that happened earlier in the
process.

`--cc` takes a name (`msvc`, `clang-cl`, `clang`, `gcc`) or a path. Each
toolchain has a *style*, `msvc` or `cc`, which decides the syntax of every
flag and which response file a package's build writes. This is the same
split that `toolchain.py`'s `CC` class and `--gfx-link msvc|cc` make today.
Each toolchain also has an *id*,
`<name>-<version>-<8 hex of the driver's path and style>`, which names the
cache directories. `cmake` and `git` are found on PATH, and on Windows
cmake is also looked for where Visual Studio installs it. `goose toolchain`
prints all of this.

Processes are started by a small cross-platform runner (`src/process.h`:
`CreateProcessW`, `posix_spawn`). Their output is captured to a log file in
the build directory and, on failure, shown together with the command that
failed. `-v` shows every command, and `--dry-run` only prints them. While
processes run, `src/progress.h` keeps the user informed without flooding a
log. Each step that does real work (a fetch, a package's native part, the
runtime object) prints one line. Only when stderr is a terminal is there
also a live line, redrawn in place, that shows the running command, its
running time and the last line it printed. `GOOSE_PROGRESS=plain|live`
overrides the terminal check.

Without any C compiler, on Windows:

    cannot find a C compiler (looked for cl, clang-cl and clang on PATH and in
    Visual Studio, and for gcc and cc on PATH); install Visual Studio with C++
    or LLVM, or set GOOSE_CC

### 6.2 The runtime object

The runtime object is
`build/runtime/<runtime hash>/<toolchain>-<config>/goose_runtime.o`. It is
written from `--emit-runtime` and compiled on first use, as
`toolchain.py`'s `GooseRuntime` does. The hash is `RuntimeVersion()`, so a
new compiler never links a stale object. `--standalone` skips it.

### 6.3 What `goose build` does

1. Parse the root; fetch declared packages that are missing (§4.6); parse
   the rest; typecheck; generate C into `build/prog/<stem>-<hash>/foo.c`.
2. For each package with a native part that the program reaches, build it if
   it is not built for this toolchain (§5).
3. Compile the runtime object if missing (§6.2) and the program's own
   `c_sources`, if any.
4. Compile `foo.c` and link: the program's objects, the runtime object, the
   packages' response files in link order, `-lm -pthread` where the style
   needs them; `/Fe:` or `-o` to the executable beside the root source, or
   `output` from the sidecar, or `-o`.

`--dry-run` prints steps 2 to 4 as commands, with the response files'
paths, for a Makefile or CMake project to copy. The flags that exist
today, `-o foo.c`, `--emit-runtime`, `--gfx-link` and the others, remain as
the plumbing for the same purpose; `--<m>-link` becomes a thin alias for
"the response file of package m" and is kept until the runners move (§10).

### 6.4 `goose run` with packages

`goose run` does the same up to step 2; then the program is compiled
in-process and the packages' JIT libraries are loaded (§5.3). A `thread_fn`
program still needs `build` (TinyCC cannot place thread-local storage in
memory; this is unchanged).

### 6.5 `embed_file`

`embed_file("path") -> const u8[:]` returns the file's bytes. The file is
read when the program is compiled, resolved relative to the calling file
like `embed_shader`'s file form, and emitted into static data the same way,
each distinct file once. The argument is a string literal or a
`let`/`const` global holding one, under `embed_shader`'s rules. It lets a
package ship a font or a table without a runtime file, which
`resource_dir()` cannot locate for a library.

## 7. The compiler's own build

### 7.1 Builtin packages

`GOOSE_BUILTIN_LIBS` is a CMake cache variable: empty by default, `all`, or
a list of blessed package names. For each, CMake reads `src/blessed.txt` and

```cmake
FetchContent_Declare(goose_pkg_gfx
    GIT_REPOSITORY https://github.com/aardappel/goose_gfx
    GIT_TAG v1 GIT_SHALLOW TRUE GIT_SUBMODULES_RECURSE TRUE
    SOURCE_DIR "${CMAKE_BINARY_DIR}/pkg/github.com/aardappel/goose_gfx/v1")
FetchContent_MakeAvailable(goose_pkg_gfx)
```

fetches it at configure time with CMake's own fetcher (no Python, and no
goose yet) into the cache layout of §4.6 under the build directory. CMake
then adds the package's project, links its `cmake_target` into `goose`, and
generates its symbol table. This is the same `goose_symbols.c`, produced by
a CMake regex over `extern fn name` lines and their trailing `= "…"`, since
no compiler exists yet to parse them. Finally CMake writes `link-cc.rsp`
and `link-msvc.rsp` under `pkg/…/<config>/`, through the same wrapper and
file-API probe as §5.2 (`cmake/package.cmake`, shared by the two). The
compiler is built knowing that directory, as it knows `GOOSE_GFX_LINK_PATH`
today. It looks for a builtin package's response file there, then beside
its own binary, as it does for the package's sources. A builtin package
with a plain-C sidecar is built the same way, minus `add_subdirectory`:
CMake compiles the `c_sources` itself.

`FETCHCONTENT_SOURCE_DIR_GOOSE_PKG_GFX` points a build at a local checkout,
which is also how a package is developed against the compiler.

The release binaries and CI configure with `all`. A developer's first
`cmake -B build` builds the compiler alone in a fraction of the time, and
the graphics samples still run after a single fetch and build into
`~/.goose`, which every worktree shares.

A release (`.github/workflows/release.yml`, laid out by
`scripts/package_release.py`) has no build tree to look in. Instead, each
builtin package is in `pkg/<name>/` beside the compiler: its sources
without their vendored dependencies, and the static libraries of its link
closure in `lib/`, which its response files there name by paths relative
to themselves. A link uses a copy of those response files under
`GOOSE_HOME/build/builtin/` with the paths made absolute, one copy per
place the compiler is unpacked.

### 7.2 What stays in the compiler

`embed_shader`, `src/shaderc.c` and `third_party/cute_spirv` stay: the
shader compiler is a compiler feature any graphics package may use. The
blob it produces is a contract between compiler and gfx package:
`src/gfx/gfx_blob.h` moves to `src/gfx_blob.h`, the package carries a copy,
and the header gains a version constant the layer checks when it reads a
blob. SDL_GPU's binding
rules, checked at compile time, stay where they are.

### 7.3 TinyCC

The submodule becomes a copied subtree at the same path, `third_party/tinycc`:
the sources `cmake/libtcc.cmake` builds (`libtcc.c`, `tcc.c`, the `.c` and
`.h` of the x86-64, i386 and arm64 targets, `tccpe.c`, `tccmacho.c`,
`include/`, `lib/`, `win32/include` and `win32/lib`), `VERSION`, `COPYING`,
`RELICENSING`, and a README recording the upstream commit and how to update,
like `third_party/cute_spirv/README.md`. `GOOSE_LIBTCC` and
`GOOSE_HAVE_LIBTCC` are removed, and `have_jit` is always true. The
fallback where `goose` without `-o` writes the C, and the runners'
`have_jit` probe, are removed too. The compiler builds on x86-64 and arm64,
the only platforms it has been tested on.

## 8. Testing

* `test/packages/` contains root fixtures that declare packages from path
  sources in the same directory. They test a pure Goose package with `import
  pkg.sub;` and requirements that are satisfied, missing, from a different
  source, or at a different ref. A missing requirement reports its line; a
  different source is an error, and a different ref is a warning. Other fixtures
  cover package/local-file collisions, a plain-C package run under JIT and built
  AOT, and a CMake package (skipped with a notice if cmake is not on PATH). They
  also cover a `main_thread_only` package reached from a `thread_fn`, and
  `embed_file`.
* Git sources: the runner creates a bare repository with `git init` and a
  tag, and a fixture declares it as `file://…@tag` and as `@<hash>`, so the
  fetch path is tested offline. These tests are skipped without git.
* `goose build` of every sample that `run_samples.py` builds, compared
  against the same expected output, once the runners move (§10, last step).
* The blessed packages' own tests (`test/gfx/`, `test/physics/`, `test/ui/`,
  `test/audio/`) stay in this repository's suite while the packages are
  blessed. CI configures with `GOOSE_BUILTIN_LIBS=all`, since its gfx tests
  need the Linux packages it installs anyway; the sanitizer job configures
  with none, which exercises the no-package paths as it does today.
  `api_check.py` moves with each package to its repository.
* In the runners' skip detection, the strings "built without SDL3" and
  similar ones are replaced by the tool-missing errors of §4.6, §5.2 and
  §6.1, matched the same way.

## 9. Documentation

The following documentation is updated:

* Spec §11.1 (package roots, the sourced import, the resolution order, flat
  requirements), Appendix D (the grammar line), §12 (`embed_file`).
* `implementation.md` §1 (the verbs, `process.h`, `toolchain.h`,
  `packages.h`), §7 (the symbol table replacing the X-macro registration).
* `stdlib.md` (gfx, audio, physics and ui documented in their repositories,
  linked).
* `testing.md`.
* `README.md` (build with `-DGOOSE_BUILTIN_LIBS=all`, and
  `goose build foo.goose` in place of the cc lines).
* `samples/README.md`.
* `tutorial.md`, where it builds programs.
* The design docs of the moved packages, which move with them.
* `vscode/`: a Build command calling `goose build`.

## 10. The plan

Each step is one commit, or a few, and leaves every test passing. Steps
are ordered so that nothing is built twice and the old path keeps working
until the new one covers it.

1. **Vendor TinyCC and make the JIT mandatory** (§7.3). Delete the submodule
   entry and the `GOOSE_LIBTCC` option; remove every `have_jit` branch, the
   `.c`-fallback and the runners' probe. CI drops nothing else yet.
2. **Verbs and the process runner.** `src/process.h`; `Main` dispatches on
   a verb and otherwise reads the command line as today; `goose run`,
   `goose check`, `goose version` (runtime hash only). No new behaviour.
3. **Toolchains and `goose toolchain`** (§6.1): `src/toolchain.h`, the
   discovery order, the vcvars import with its cache, `GOOSE_HOME` and
   `GOOSE_HOME/config`.
4. **`goose build` for what exists**: the runtime object cache (§6.2), the
   program's C, the link, `-o`, `--standalone`, `--debug`, `-v`,
   `--dry-run`, with the native modules linked through today's response
   files found as `--gfx-link` finds them. From here `goose build
   samples/27_gfx_cube.goose` works. The README shows it; the runners are
   untouched.
5. **Package roots** (§4.4 step 1): the driver gets `Package` records,
   every source carries its package, imports resolve against the importing
   file's package root. No visible change for today's programs.
6. **Declared packages from path sources** (§4.2, §4.3, §4.4): the parser's
   `from` form, root-only honoring, requirements and their diagnostics,
   `import pkg.sub;`, the collision rule. `test/packages/` begins. Spec
   §11.1 and Appendix D updated.
7. **Git sources and the cache** (§4.6): `fetch`, auto-fetch, `--update`,
   the git error. The offline `file://` test.
8. **`embed_file`** (§6.5), independent of everything else; it can land
   anywhere from here on.
9. **The sidecar and a program's own C** (§4.7, §5.1, §5.3): the sidecar
   parser, `c_*` keys on the program, the generated symbol table, the JIT
   shared library and its loading. The program is the first package with a
   native part, before any library is.
10. **Plain-C packages** (§5.1): the same per package, cached per toolchain,
    link order. A fixture with a `.c` file, JIT and AOT.
11. **CMake packages** (§5.2): the wrapper, the file-API probe and the JSON
    reader, the response files. A fixture with a tiny CMakeLists, skipped
    without cmake.
12. **Builtin packages and the blessed table** (§4.5, §5.4, §7.1):
    `src/blessed.txt`, `GOOSE_BUILTIN_LIBS`, `cmake/package.cmake` with the
    CMake-side symbol table, `builtin_packages.h`, `main_thread_only`. The
    four in-tree modules are **re-expressed as packages in place** first:
    `packages/physics/` holding `physics.goose`, `physics.build`, a
    `CMakeLists.txt` and `src/`, the Box3D submodule still under
    `third_party/`, with `blessed.txt` pointing at the in-tree path. The
    layer code moves but its behaviour does not change, and
    `cmake/physics.cmake`, `src/physics.h`, the prefix sniffing and
    `jit.h`'s registration are removed, one module per commit: physics, ui,
    audio, gfx.
13. **Move physics out** to `aardappel/goose_physics`: the repository holds
    what `packages/physics/` held plus the Box3D submodule, `api_check.py`
    and its design doc; `blessed.txt` points at it; the in-tree copy and
    the submodule go; CI configures with `GOOSE_BUILTIN_LIBS=all`.
14. **Move ui, then gfx and audio** the same way, into `goose_gfx` (ui's
    place there, or a repository of its own, decided when it moves). The
    SDL and Nuklear submodules go, the blob header gets its version
    constant, the Linux build dependencies in `ci.yml` stay because the
    builtin build needs them.
15. **The runners call the compiler**: `toolchain.py` asks `goose toolchain
    --json` instead of discovering, and the samples runner builds with
    `goose build`; the test runner keeps driving the C compiler itself
    where it needs per-level flags, a second front end or multi-program
    batches. `--<m>-link` is retired. The VS Code extension gets Build.

## 11. Deferred and open

* **A version number.** When a library needs to say "compiler N or
  later", CMake bakes `git rev-list --count HEAD` and the commit date into
  `goose version`; a build from a branch reports that it is a branch build.
  Nothing is hand-assigned and no tag is needed.
* **Transitive dependencies** on request (§4.3), a lockfile if branch refs
  turn out to be used in anger, `goose vendor`.
* **Fetching without git** (GitHub tarballs over HTTPS), and prebuilt
  package binaries, when building for three platforms times the
  configurations is worth the effort. `goose.h` repeats layouts codegen
  emits (`gs_stack`, `gs_rhdr`, `gs_rref`), and a part built from source is
  rebuilt when it changes; a prebuilt part would need those layouts to carry
  a version that it is checked against.
* **Package data at run time**: a library's files reachable through a
  `package_dir("gfx")` in JIT and copied beside the executable by `build`.
  `embed_file` covers fonts and tables until then.
* **Build steps**: a package that runs a tool while the program compiles (a
  shader compiler of its own). The natural form is a Goose program the
  compiler runs under the JIT, so a tool needs no C compiler; the sidecar
  would name it. This is planned for, but not built.
* **Repository subdirectories** as package roots, C++ in `c_sources`, a
  `windows_subsystem` key for programs without a console, `goose test`,
  `goose clean`.
* **Namespace privacy** stays out of scope (§12); a library cannot hide its
  internals, and the convention that a package's files declare its name as
  their namespace is only a convention.
