# Dynamic externs: calling a shared library by name

<details>
<summary>Table of contents</summary>

- [1. Decisions](#1-decisions)
- [2. The boundary today](#2-the-boundary-today)
- [3. The declaration](#3-the-declaration)
- [4. What happens at run time](#4-what-happens-at-run-time)
- [5. The generated C](#5-the-generated-c)
- [6. Calling a real library without an adapter](#6-calling-a-real-library-without-an-adapter)
- [7. The compiler's pieces](#7-the-compilers-pieces)
- [8. Alternatives considered](#8-alternatives-considered)
- [9. Testing](#9-testing)
- [10. Documentation](#10-documentation)
- [11. The plan](#11-the-plan)
- [12. Deferred and open](#12-deferred-and-open)

</details>

Today an `extern fn` is a C function that the linker resolves, from the
runtime, from a package's native part, or from an `--include` header. This
document proposes a second kind: an `extern fn` that names the shared
library (`.so`, `.dll`, `.dylib`) it is in. The program opens the library
when it starts, the runtime looks the symbol up by name and stores its
address in a pointer, and calls go through that pointer. A program can then
use a library the machine already has, such as SDL2, sqlite3, or a vendor
SDK that ships as a DLL, without any C source, import library or `.build`
sidecar, and in JIT mode without a C toolchain. Written against the tree at
`1e31b6d`. Not implemented.

## 1. Decisions

* **The source names the library.** `extern "SDL2" { fn SDL_Init(flags:
  u32) -> i32; ... }` declares a group of extern fns bound from one library.
  Inside the block each line is an extern declaration as defined in §7.10,
  with the same optional string for a symbol that differs from the Goose
  name. Nothing else in the language changes. To the checker, overload
  resolution, the optimizer and the effect analysis, the members are
  ordinary `extern fn`s.
* **Bound by the runtime, through a pointer.** The generated C holds one
  function pointer per dynamic extern the program calls, and a table of
  (library, symbol, slot) entries. At program start, before the globals are
  initialized, the runtime fills the slots using `dlopen`/`dlsym`
  (`LoadLibrary`/`GetProcAddress`). Each call is one indirect call, the same
  as a call to a dynamically linked symbol through the PLT. The link line
  never mentions the library, so no import library or `-dev` package is
  needed. The same generated C serves the JIT run and the `goose build`
  executable, as the backend design requires (`jit_backend.md`).
* **Eager, and only what is called.** The libraries and symbols of the
  dynamic externs that codegen reached (`usedexterns`) are resolved before
  `main`. A missing library or symbol is a runtime error that names both,
  raised before the program has done anything. An unused declaration is not
  bound, and its library need not exist, which keeps §7.10's rule that an
  unused extern needs no implementation. Lazy binding and optional
  libraries are deferred (§12).
* **One name, platform spellings derived, overridable in the sidecar.** A
  bare name `"SDL2"` means `SDL2.dll`, `libSDL2.dylib` and `libSDL2.so`,
  which each platform's loader finds the way it finds any library. A
  versioned soname, or a file shipped with the program, is given per
  platform in the program's sidecar
  (`dylibs_linux = ["SDL2=libSDL2-2.0.so.0"]`); the program's entry takes
  precedence over a package's. The compiler never opens a library at
  compile time.
* **The symbol is a string, not a C identifier.** It is passed to `dlsym` as
  written, so a decorated or mangled name binds as well as a plain one.
* **Nothing new crosses the boundary.** Dynamic externs use the same shapes
  as §7.10. What changes is that there is no adapter between the Goose
  declaration and the library's real prototype, so §6 describes how to
  declare real C signatures with those shapes: handles as `u64`, strings as
  a reference to a NUL-terminated buffer, structs laid out with `pad`.

## 2. The boundary today

An `extern fn` has one of three implementations (spec §7.10, implementation §6):

* The runtime (`gs_os_*`, `gs_sqrt`), with prototypes declared in
  `runtime_ext.h`.

* A header supplied with `--include`, written against the generated typedefs.

* A package's native part (`docs/packages.md`): C sources or a CMake target,
  built once per toolchain and linked statically.

For a JIT run, the native part is built again as a shared library exporting one
table, `goose_symbols`. The compiler loads that table (`process.cpp`,
`LoadSharedSymbols`) and passes its symbols to TinyCC one by one
(`tcc_add_symbol`, `jit.h`). Codegen emits a prototype for every used extern
that the runtime does not declare, and calls the symbol directly
(`EmitExternCall`, `ExternProto`, `codegen_calls.h`). `NativePackagesUsed`
(`native.h`) maps the used externs to the packages whose native parts are
linked.

To bind an installed C library this way, a package needs a native part
whose only content is the link line: a sidecar with `c_libs = ["SDL2"]`,
and a C file, possibly empty, so that the compiler has something to build.
The JIT run then needs a C toolchain to produce the `goose_symbols` library,
even though the program has no C code of its own to compile. `goose build`
needs the import library (`SDL2.lib`) or the unversioned `libSDL2.so`
symlink, which on Linux comes from the `-dev` package. A library that ships
as a DLL with no `.lib` cannot be used without writing a loader in C.

## 3. The declaration

```goose
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32;
    fn SDL_Quit();
    fn SDL_GetTicks64() -> u64;
    fn delay(ms: u32) = "SDL_Delay";
}
```

The grammar gains one top-level form:

```
externblock  := "extern" strlit "{" externmember* "}"
externmember := "fn" declname "(" params? ")" ("->" rettypes)? ("=" strlit)? ";"
```

Both at the top level (§7.10) and inside a block, a symbol is given as the
string after `=`, where a body would otherwise go. A string after `extern`
names a library when `{` follows it. When `fn` follows it, the string is
the deprecated top-level syntax for a symbol, which is still accepted with a
warning; block members cannot use that syntax. A member without a symbol
string binds the symbol with the same name as its Goose leaf name. This
keeps the Goose names in one column: `fn delay(` is what a reader or a grep
looks for. A member takes a `declname`, so a namespaced `fn gfx::init(...)`
is written as it would be outside the block. The qualifiers an extern
cannot carry today (`checkfree`, `simd`, `inline`, `recursive`, a body) are
errors inside the block, with the same messages; `thread_fn` and `export`
are not valid members. A block can only appear at top level and cannot be
nested. Trailing defaults work as in §7.10: every call passes every
argument.

Two declarations may bind the same symbol with different Goose signatures
(the `sqrt`/`sqrtf` overload pair of `math.goose`, or two views of one
function); each gets its own pointer, so unlike the prototype form there
is no C-level conflict to avoid.

**Spelling the library.** The string is a bare name or a file name:

| string | Windows | macOS | Linux |
|---|---|---|---|
| `"SDL2"` (no separator, no dot) | `SDL2.dll` | `libSDL2.dylib` | `libSDL2.so` |
| `"libfoo.so.3"`, `"foo.dll"` (a dot) | as written | as written | as written |
| `"bin/steam_api64"` or `"./x.so"` (a separator) | relative to the program's resources, see below | | |

A bare spelling is passed to the platform's loader as a bare name, so it is
found through the DLL search order on Windows, through `DYLD_LIBRARY_PATH`
and the usual places on macOS, and through `LD_LIBRARY_PATH`, the rpath and
`ld.so.cache` on Linux. Unless it is absolute, a spelling with a separator
is joined to the program's resource directory (`resource_dir()`,
`docs/stdlib.md`: the entry source's directory in a JIT run, the
executable's directory in a built program). A program can therefore ship a
DLL beside itself and name it the same way on every platform. The bare-name
transformation applies to the last path component (`"bin/steam_api64"` is
`bin/steam_api64.dll` and `bin/libsteam_api64.so`). A file name with a dot
is taken literally and is therefore platform-specific; it belongs in the
sidecar rather than the source, unless the program targets only one
platform.

**The sidecar.** A `dylibs` list maps a name used in the source to the
spelling this platform should open. Like every list in a sidecar, it can
take the `_windows`, `_linux` and `_macos` suffixes (`packages.h`):

```goose
// myprog.build
dylibs_linux = ["SDL2=libSDL2-2.0.so.0"];     // the soname every distro installs
dylibs_windows = ["SDL2=bin/SDL2.dll"];       // shipped with the program
```

The left side is the string in the source, and the right side is a
spelling as described in the table above. The program's sidecar maps names
for every package the program uses, and a package's sidecar may map its own
names as defaults. A name that no block in the program declares is an
error, which guards against typos. Nothing in the sidecar is required: a
program without one opens the derived spellings.

Both mechanisms are needed. A package that binds SDL2 wants to write
`"SDL2"` once and have it work for every user, and the derived spellings
match what a user who installed the library for development has. How the
program is deployed (to machines with only the runtime package, or with the
DLL beside the executable) is something only the program knows, and its
sidecar is where the program already says what it links and where it
builds. A list of candidate spellings tried in order was considered and is
deferred (§12).

## 4. What happens at run time

The runtime gains the following in `runtime_os.h`, the half of the runtime
that includes the platform's headers and is therefore part of the runtime
object and the `--standalone` unit:

```c
typedef struct gs_dllib  { const char *name, *win, *mac, *linux; } gs_dllib;
typedef struct gs_dlbind { int32_t lib; const char *sym; void **slot; } gs_dlbind;
GS_API void gs_dl_bind(const gs_dllib *libs, int64_t nlibs,
                       const gs_dlbind *binds, int64_t nbinds);
```

`gs_dl_bind` opens each library once (with `dlopen(RTLD_NOW | RTLD_LOCAL)`,
or with `LoadLibraryW` on the UTF-8 spelling, widened the same way
`gs_os_*` widens paths), never closes it, and stores each symbol's address
in its slot. A library entry carries the bare name and the sidecar's three
spellings, with `NULL` where the default applies. The runtime derives the
default for the platform it is compiled on, so the generated C stays as
portable across platforms as the rest of it. A failure is a runtime error
through `gs_panic`, with the text the loader gave:

```
cannot load SDL2 (libSDL2.so): libSDL2.so: cannot open shared object file: No such file or directory
SDL2 (libSDL2.so) has no symbol SDL_Foo
```

The program's `gs_program_init` calls `gs_dl_bind` right after
`gs_rt_init` and before `gs_init_globals` (`codegen_emit.h`). A global's
initializer may therefore call a dynamic extern, and so may `fn main()` of
a `--header` library, since `goose_init` runs the same initializer. Binding
happens on the main thread before any worker exists. The slots are written
once and only read after that, so `thread_fn`s read them without
synchronization, as they read the globals' initialized state. A library
that may only be called from the main thread is marked the same way as
today: with `main_thread_only = true` in the sidecar of the package that
declares it. The checker already applies that setting to every extern fn in
the package (`typecheck.h`, the `thread_fn` walk).

The program never closes a library. Calling `dlclose` on a library while a
worker may still be running one of its functions is the kind of problem the
language exists to avoid, and the process ends soon enough.

**Toolchain consequences.** `dlopen` is in libc on glibc 2.34 and later, and
in `libdl` before that. On Linux, `-ldl` is added to `SystemLibs()`
(`build.h`); on new systems libdl is a harmless stub. On macOS `dlopen` is
in libSystem, and on Windows `LoadLibraryW` is in kernel32; both are linked
already (the runtime calls `GetProcAddress` today). A JIT run compiles the
runtime with TinyCC, which links libc but not libdl. `RunJit` therefore
adds `tcc_add_library(s, "dl")` on Linux and ignores its failure, since a
system without the archive has the function in its libc. TinyCC does the
same for its own bounds checker (`tccelf.c`). The runtime's source text
changes, so `GS_RUNTIME_VERSION` changes and the runtime object is rebuilt,
as for any runtime change.

## 5. The generated C

For the block of §3, with every member called:

```c
/* ---- extern prototypes ---- */
static int32_t (*gs_dl0_SDL_Init)(uint32_t);
static void (*gs_dl1_SDL_Quit)(void);
static uint64_t (*gs_dl2_SDL_GetTicks64)(void);
static void (*gs_dl3_SDL_Delay)(uint32_t);
static const gs_dllib gs_dllibs[] = {
    { "SDL2", 0, 0, "libSDL2-2.0.so.0" },
};
static const gs_dlbind gs_dlbinds[] = {
    { 0, "SDL_Init", (void **)&gs_dl0_SDL_Init },
    { 0, "SDL_Quit", (void **)&gs_dl1_SDL_Quit },
    { 0, "SDL_GetTicks64", (void **)&gs_dl2_SDL_GetTicks64 },
    { 0, "SDL_Delay", (void **)&gs_dl3_SDL_Delay },
};
```

and in `gs_program_init`, `gs_dl_bind(gs_dllibs, 1, gs_dlbinds, 4);`. A
call site is `gs_dl0_SDL_Init(flags)` where the prototype form writes
`SDL_Init(flags)`. The argument snapshots and the builder write-back around
the call (`EmitExternCall`) are unchanged. The pointer's name is made from
the spec's position in `usedexterns` and the symbol, which makes it
deterministic and distinct per declaration. The `gs_` prefix keeps it from
clashing with anything an `--include` header declares, so a header may
`#include "SDL.h"` for its own purposes without the prototype conflict the
direct form would have. Storing `dlsym`'s `void *` into a function pointer
through `void **` is what every POSIX program does and what the platform
ABIs define.

The libraries are listed in the order the program's blocks declare them
(file order, then source order), and the binds in `usedexterns` order, so
the output is stable across runs. The table and the slots are omitted when
no dynamic extern is used, and the `/* ---- extern
prototypes ---- */` section keeps its prototypes for the static externs
beside them.

On the targets the compiler supports (x86-64, AArch64), the calling
convention is the platform's single C convention. 32-bit Windows
`__stdcall` is not a target. A variadic function cannot be bound: the
declaration has no way to say that a function is variadic, and calling one
through a fixed-arity pointer is undefined behavior (on Apple AArch64 it
actually goes wrong). The compiler cannot detect this. §6 lists it as a
rule for the programmer, and such a function can be called through a
wrapper in a native part.

## 6. Calling a real library without an adapter

The shapes of §7.10 were chosen with an adapter in mind: a header written
against the generated typedefs converts between what Goose passes and what
the library wants. With a dynamic extern there is no adapter, so the
declaration has to express the library's real prototype in Goose's shapes.
Most of a library's API fits these shapes; the parts that do not are listed
last.

* **Scalars** cross as their C types: `i32` is `int`, `u32` `unsigned`,
  `i64` `int64_t`, `f32` `float`, `f64` `double`, `bool` one byte. C's
  `long` is `i64` on Linux and macOS and `i32` on Windows, so a function
  with a `long` in its signature needs a per-platform declaration or an
  adapter; `size_t` is `u64` on every target.
* **Opaque handles**, such as the `SDL_Window *` a library returns and later
  takes back, are `u64`. On both ABIs a pointer and a 64-bit integer are
  returned and passed the same way. Goose cannot look through a `u64`,
  which is intended: the handle is only a token. This is a convention this
  document establishes, not a language change, since `u64` can already
  cross the boundary.
* **A pointer to something Goose owns** is `T&` for a flat fixed `T`, which
  crosses as that pointer: `fn SDL_GetWindowSize(w: u64, x: i32&, y: i32&)`.
  A slice `T[:]` crosses as the `{ data, len }` pair by value, which no C
  library takes. Where C wants `(ptr, count)`, the declaration takes
  `xs[0]` and `xs.len` as two parameters. Where C wants a NUL-terminated
  string, the Goose side appends a `\0` and passes the first byte by
  reference:

  ```goose
  extern "SDL2" {
      fn SDL_CreateWindow(title: const u8&, x: i32, y: i32, w: i32, h: i32, flags: u32) -> u64;
  }
  let title = "Goose\0";
  let win = SDL_CreateWindow(title[0], 100, 100, 640, 480, 0);
  ```

These calls show exactly what crosses the boundary, but are awkward to write. A
`stdlib` `ffi` module could provide the two helpers below.
* **Structs by value and by reference** cross in the packed layout of
  C.2, which is C's layout only when Goose's fields are already aligned as
  C would place them. `SDL_Rect { int x, y, w, h }` matches `struct Rect {
  x: i32, y: i32, w: i32, h: i32 }`; a struct with a `u8` before an `i32`
  matches only if `pad` is written where C inserts padding (§3.2 has `pad`
  and `pad n` for this purpose). The same applies to tail padding: a C
  struct's size is a multiple of its alignment, so `{ a: i32, b: u8 }`
  needs `pad 3` at the end to be stored in an array that C indexes. The
  documentation says this plainly, with the two examples. However, matching
  the bytes does not mean matching the calling convention. A `pad` is a
  `uint8_t` array in the generated C, so when passed by value,
  `{ x: f32, pad 4, y: f64 }` goes in an integer register where the
  library's `{ float; double; }` goes in two float registers (on x86-64
  System V; an ARM64 float aggregate behaves likewise). A struct holding a
  `pad` is therefore refused by value in a block and must cross by
  reference. A later change could lift this restriction by emitting a
  natural-alignment mirror of such a struct for the call.
* **What needs an adapter still.** Three things do. The first is reading
  memory the library owns, such as a `const char *` it returns
  (`SDL_GetError`) or an array it keeps (`SDL_GetKeyboardState`): Goose has
  no way to read through a `u64`, and should not gain one. The second is
  callbacks: a library that wants a function pointer (`qsort`, GLFW's event
  callbacks) has nothing to receive, since an `export fn` has a C symbol
  but no Goose value that is its address. The third is variadic functions
  (`SDL_Log`, `printf`). Each of these takes a few lines of C in a native
  part today, and the two read helpers below would remove the need for the
  first:

  ```goose
  // stdlib/ffi.goose, proposed, after the feature lands
  extern fn cstr(p: u64, out: u8[>..]&) = "gs_ffi_cstr";             // bytes at p up to a NUL
  extern fn bytes(p: u64, n: i64, out: u8[>..]&) = "gs_ffi_bytes";   // n bytes at p
  ```

  They copy into Goose storage, so foreign memory never becomes a Goose
  value without being copied, which is the boundary rule of §9 (external
  bytes are validated as they enter). They trust the pointer, as every
  extern call trusts C, and the documentation says so.

## 7. The compiler's pieces

* **AST.** `SFunction` gains `string dylib;`, empty for the extern of
  today. `isextern` stays true for both kinds, so everything that asks
  "is this a C function" is unchanged.
* **Parser** (`parser.h`, the `T_EXTERN` branch of the function parser).
  After `extern strlit`, a `{` starts a block. The parser loops reading
  members until `}`. Each member is parsed by the existing header path with
  `isextern` set and the library stored, and a `;` is expected after it, as
  for an extern declaration. The block has its own errors for a body, a
  nested block, a member qualifier an extern rejects, an empty library
  string, and a block below top level. An empty block is allowed: it is a
  comment's worth of declaration, and a package may generate one.
* **Checker.** `CheckExternSpec` is the check, unchanged. The
  `main_thread_only` walk is unchanged.
* **Codegen** (`codegen_calls.h`, `codegen.h`, `codegen_emit.h`). Codegen
  emits the pointer, the two tables and the bind call shown in §5.
  `ExternProto` writes a pointer declaration for a dynamic spec and a
  prototype for a static one, and `EmitExternCall` calls through the
  pointer's name. The sidecar's spellings reach codegen through the `Ast`'s
  package records (`dylibs` on the package, merged with the program's
  entries overriding the package's when the tables are emitted).
* **Native parts** (`native.h`). `NativePackagesUsed` skips a dynamic spec.
  A package whose externs are all dynamic therefore has no native part, and
  a package with both kinds does not have its native part pulled in by a
  dynamic call. `ExternNamesIn`, the lexer-level scan that lists the
  symbols a package's `goose_symbols` table must export, skips from
  `extern strlit {` to the matching `}`, because those symbols do not
  belong to the native part.
* **Driver** (`main.cpp`, `build.h`, `jit.h`). `-ldl` is added to the link
  on Linux, and `tcc_add_library(s, "dl")` to `RunJit`, where a missing
  library is tolerated (§4). There is no compile-time probing and there are
  no new command-line options.
* **Sidecar** (`packages.h`). `dylibs`, with the three suffixes, is added to
  the known keys. Entries have the form `name=spelling`; each is checked for
  a `=` and kept on the package record. The program's entries override a
  package's when the tables are emitted. A name that no block declares is
  an error at that point, when every block is known.
* **Runtime** (`runtime.h` for the declarations, `runtime_os.h` for the
  definitions). §4.
* **Editors.** The tree-sitter grammar (`tree-sitter/grammar.js`, the
  `extern` qualifier at the function declaration rule) and the VS Code
  grammar accept the block; the language server only sees ordinary
  functions and needs no change. The Goose-in-Goose parser of the
  self-hosting tests (`test/goose_in_goose/parser.goose`) parses the block
  too, since it parses the stdlib and samples.
* **Wasm.** Wasm is not a target of this work; `wasm.md` §3 lists externs
  as a decoded-pointer boundary. It is noted in §12 because the
  declaration amounts to a wasm import.

## 8. Alternatives considered

* **Link against the library instead of opening it.** Emit the prototype
  as today, put `-lSDL2` (`SDL2.lib`) on the link line, and for a JIT run
  have the compiler `dlopen` the library and `tcc_add_symbol` each used
  function, as it does for native parts. Rejected: the build would then need
  the import library or the unversioned `.so`, which means the `-dev` package
  or a hand-made `.lib`. The JIT and build paths would resolve symbols
  differently, so a mistake would show up as a link error in one and a load
  error in the other. A DLL shipped without a `.lib` would be unreachable.
  Calling through the pointer is no slower than calling through the PLT.
* **The compiler opens the library at compile time** to verify that the
  symbols exist, as a check. Rejected as the default: the machine that
  builds the program is often not the machine that runs it (for example, a
  CI builder without SDL installed). On the machine that matters, a
  run-time error naming the library and symbol before `main` is early
  enough. A `--probe-libs` check could be added for the developer's own
  machine (§12).
* **Per-declaration syntax**, `extern "SDL2" "SDL_Init" fn init(...)`.
  Rejected: to anyone used to a C-family language, two adjacent strings
  look like concatenation. Also, a binding covers a library's worth of
  functions, and the block names that library once. A library-handle
  declaration (`lib sdl = "SDL2"; extern sdl fn ...`) was considered and
  dropped, because it adds a new kind of declaration for a single use.
* **The sidecar alone names the libraries** (`dylibs = ["SDL2"]`, with the
  externs declared as today and resolved by searching the listed
  libraries). Rejected: a package's binding should be complete in its
  source, a symbol's library should be visible where the symbol is
  declared, and a bare `extern fn` would silently mean two things.
* **Lazy binding on first call**, with a check on every call. Rejected for
  now: it adds a branch to every call for a feature (optional libraries)
  that nobody has asked for, and it reports an error in the middle of a
  run, where an error before `main` is clearer. See §12.

## 9. Testing

* **A library built for the test**, `test/extern_dylib.py`, using the
  toolchain the other native tests use (`scripts/toolchain.py`). The test
  compiles a C file into a shared library placed beside the test program.
  The C file has the parameter shapes `extern_types.py` covers (scalars, a
  struct by value and by reference, a reference to an element, a builder),
  plus a function that returns a handle and one that takes the handle back.
  The program binds the library with a bare name and the sidecar's spelling
  (`dylibs_linux = ["dltest=./libdltest.so"]` and its siblings, which
  exercise the override and the relative path). It runs both ways, JIT and
  `goose build`, and the outputs are compared.
* **The failure paths**, in the same file. A block naming a library that
  does not exist, with one member called, exits with the runtime error
  naming the spelling tried. A member whose symbol the library lacks
  behaves the same way. With the same declaration unused, the program runs.
  A static extern beside a dynamic one, in a package with a native part,
  still links the native part and nothing more.
* **Syntax and checking errors**, `test/errors_tc` and `test/syntax`: a
  member with a body, a nested block, a generic member, `checkfree` and
  `simd` members, an empty library string, a block inside a function, a
  sidecar `dylibs` entry without `=` or naming no block.
* **Determinism**: the generated C of the test program is compared across
  two compiles, as the self-hosting check compares its stages' C.
* **No-toolchain JIT**: a test that runs a dynamic-extern program under
  `goose` when no C compiler can be found (as `native_toolchain.py`
  arranges) and checks that it runs. This is the feature's main benefit,
  and a case the native-part path cannot handle. It binds the library the
  first test built, so it runs after that test.
* The self-hosting parser's tests and the tree-sitter corpus get the block.

## 10. Documentation

* Spec §7.10: a subsection for the block, the spelling table of §3, the
  binding time and failure, and the grammar lines. §6's conventions for
  real signatures go to `reference.md` §24 with the examples, since they
  are advice rather than language.
* `docs/packages.md`, "The native part": a paragraph before it saying a
  package over an installed library may need no native part, pointing at
  §7.10; the sidecar key in its table.
* `docs/stdlib.md`: `resource_dir()` gains the sentence that library
  spellings with a separator are relative to it.
* `jit_backend.md`: the libdl note and the fact that a program whose only
  C is dynamic libraries runs without a toolchain.
* A sample in `samples/` that binds SDL2 if it is present, and otherwise
  prints why not, is tempting, but the sample suite runs everywhere. The
  test's library serves as the example instead, and the spec's SDL2 block
  as the illustration.

## 11. The plan

One working commit each, tests green at every step:

1. **Runtime**: `gs_dllib`, `gs_dlbind`, `gs_dl_bind`, the error texts,
   `-ldl` in `SystemLibs()`, `tcc_add_library("dl")` in `RunJit`. Nothing
   uses these yet; the runtime version changes.
2. **Parser and AST**: the block, `dylib` on `SFunction`, the errors, the
   tree-sitter and Goose-in-Goose grammars, `ExternNamesIn` skipping
   blocks. Codegen rejects a dynamic spec with "not supported yet", so the
   tree builds and the error tests can be added in this step.
3. **Codegen and driver**: the slots, tables and bind call,
   `NativePackagesUsed` skipping dynamic specs, `test/extern_dylib.py`
   with the derived spellings only.
4. **Sidecar**: `dylibs` and its suffixes, program over package, the
   typo guard, the override tests.
5. **Documentation**: §10, with the §6 conventions.
6. **`stdlib/ffi.goose`** (`cstr`, `bytes`) as a separate decision once
   the feature has a user that needs them.

## 12. Deferred and open

* **Optional libraries.** A program that uses a library if present
  (hardware acceleration, a telemetry SDK) wants `extern? "foo" { ... }`
  or an `available("foo")` query, and binding after `main` has started.
  That requires lazy binding with a per-call check, or an explicit `bind`
  call made by the program. Deferred until a program needs it.
* **Candidate spellings in the source**, `extern "libSDL2-2.0.so.0",
  "SDL2" { ... }` tried in order, skipping spellings that are not of this
  platform's form. This would let a package work on distros without the
  `-dev` symlink, without the user adding a sidecar entry. Deferred because
  the sidecar covers this case and the ordering rules would be one more
  thing to explain.
* **`main_thread_only` per block** rather than per package, for a program
  with no sidecar binding a windowing library. Easy to add as a word after
  the library string if wanted.
* **Callbacks**: a function-pointer value for an `export fn`, so a library
  taking one can be given it. Separate design; it touches what a function
  value is (§7.6).
* **A compile-time probe**, `--probe-libs`, opening each library and
  resolving each used symbol in the compiler process, for the developer's
  machine.
* **Wasm**: an `extern "env" { fn log(x: i32); }` block is a module
  import, `(import "env" "log")`, with no runtime binding. When
  `wasm.md` is implemented the block maps onto imports by its library
  string; nothing here prevents it.
* **Cross-compilation.** The compiler does not currently cross-compile. The
  generated C remains portable because the runtime chooses the library spelling.
  If cross-compilation is added, the sidecar's suffixed lists already
  distinguish targets.
* **`long` and other platform-sized C types.** A declaration cannot say
  "C `long`". If the language gets a `const` platform test, two blocks
  under such a test would work; until then, an adapter is needed.
