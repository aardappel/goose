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

An `extern fn` today is a C function the link resolves: the runtime's, a
package's native part, or an `--include` header. This proposes a second
kind, an `extern fn` that names the shared library (`.so`, `.dll`, `.dylib`)
it lives in, which the program opens when it starts and calls through a
pointer the runtime filled in by looking the symbol up by name. A program
can then use a library the machine already has -- SDL2, sqlite3, a vendor
SDK that ships as a DLL -- with no C source, no import library, no `.build`
sidecar and, in JIT mode, no C toolchain at all. Written against the tree
at `1e31b6d`. Not implemented.

## 1. Decisions

* **The source names the library.** `extern "SDL2" { fn SDL_Init(flags:
  u32) -> i32; ... }` declares a group of extern fns bound from one library.
  Inside the block each line is an extern declaration as §7.10 has it, with
  the same optional string for a symbol that differs from the Goose name.
  Nothing else in the language changes: the members are ordinary `extern
  fn`s to the checker, overload resolution, the optimizer and the effect
  analysis.
* **Bound by the runtime, through a pointer.** The generated C holds one
  function pointer per dynamic extern the program calls and a table of
  (library, symbol, slot); the runtime fills it with `dlopen`/`dlsym`
  (`LoadLibrary`/`GetProcAddress`) at program start, before the globals are
  initialized. A call is one indirect call, which is what a call to a
  dynamically linked symbol costs through the PLT anyway. The link line
  never mentions the library, so no import library or `-dev` package is
  needed, and the same generated C serves the JIT run and the `goose build`
  executable, as the backend design requires (`jit_backend.md`).
* **Eager, and only what is called.** The libraries and symbols of the
  dynamic externs codegen reached (`usedexterns`) are resolved before
  `main`; a missing library or symbol is a runtime error naming both,
  before the program has done anything. An unused declaration costs nothing
  and its library need not exist, which keeps §7.10's rule that an unused
  extern needs no implementation. Lazy binding and optional libraries are
  deferred (§12).
* **One name, platform spellings derived, overridable in the sidecar.** A
  bare name `"SDL2"` is `SDL2.dll`, `libSDL2.dylib` and `libSDL2.so`, found
  by each platform's loader as it finds any library. A versioned soname or a
  file shipped with the program is spelled per platform in the program's
  sidecar (`dylibs_linux = ["SDL2=libSDL2-2.0.so.0"]`), the program's entry
  winning over a package's. The compiler never opens a library at compile
  time.
* **The symbol is a string, not a C identifier.** It goes to `dlsym` as
  written, so a decorated or mangled name binds as well as a plain one.
* **Nothing new crosses the boundary.** The shapes of §7.10 are the shapes
  here. What is new is the absence of an adapter between the Goose
  declaration and the library's real prototype, so §6 spells out how real
  C signatures are declared under those shapes: handles as `u64`, strings
  as a reference to a NUL-terminated buffer, structs laid out with `pad`.

## 2. The boundary today

Three things stand behind an `extern fn` (spec §7.10, implementation §6):
the runtime (`gs_os_*`, `gs_sqrt`), whose prototypes `runtime_ext.h`
declares; a header given with `--include`, written against the generated
typedefs; and a package's native part (`docs/packages.md`), C sources or a
CMake target the compiler builds once per toolchain and links statically.
For a JIT run the native part is built again as a shared library exporting
one table, `goose_symbols`, which the compiler loads (`process.cpp`,
`LoadSharedSymbols`) and hands to TinyCC symbol by symbol
(`tcc_add_symbol`, `jit.h`). Codegen emits a prototype for every used
extern the runtime does not declare and calls the symbol directly
(`EmitExternCall`, `ExternProto`, `codegen_calls.h`); `NativePackagesUsed`
(`native.h`) turns the used externs into the packages whose native parts
the link takes.

Binding an installed C library through this costs a native part whose only
content is the link line: a sidecar with `c_libs = ["SDL2"]` and a C file,
possibly empty, for the compiler to have something to build. The JIT run
then needs a C toolchain to produce the `goose_symbols` library even though
no C of the program's own is compiled, and `goose build` needs the import
library (`SDL2.lib`) or the unversioned `libSDL2.so` symlink, which on Linux
is the `-dev` package. A library that ships as a DLL with no `.lib` cannot
be reached at all without writing a loader in C.

## 3. The declaration

```goose
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32;
    fn SDL_Quit();
    fn SDL_GetTicks64() -> u64;
    "SDL_Delay" fn delay(ms: u32);
}
```

The grammar gains one top-level form:

```
externblock  := "extern" strlit "{" externmember* "}"
externmember := strlit? "fn" declname "(" params? ")" ("->" rettypes)? ";"
```

After `extern` and a string the parser sees either `fn` (the existing
declaration, the string being its symbol) or `{` (a block, the string being
its library). The forms are unambiguous and the block keeps the string's
meaning inside: a member's string is its symbol, as in §7.10, and a member
without one binds the symbol spelled like its Goose leaf name. A member
takes `declname`, so a namespaced `fn gfx::init(...)` is written as it would
be outside the block. The qualifiers an extern cannot carry today
(`checkfree`, `simd`, `inline`, `recursive`, a body) are errors inside the
block with the same messages; `thread_fn` and `export` are not members. A
block is top-level only and may not nest. Trailing defaults are as §7.10
has them: every call passes every argument.

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

A bare spelling goes to the platform's loader as a bare name: the DLL
search order on Windows, `DYLD_LIBRARY_PATH` and the usual places on macOS,
`LD_LIBRARY_PATH`, the rpath and `ld.so.cache` on Linux. A spelling with a
separator is joined to the program's resource directory (`resource_dir()`,
`docs/stdlib.md`: the entry source's directory in a JIT run, the
executable's in a built program) unless absolute, so a program ships a DLL
beside itself and names it the same way on every platform; the bare-name
transformation applies to its last component (`"bin/steam_api64"` is
`bin/steam_api64.dll` and `bin/libsteam_api64.so`). A file name with a dot
is taken literally and is therefore platform-specific; it belongs in the
sidecar rather than the source unless the program has one platform.

**The sidecar.** A `dylibs` list, with the `_windows`, `_linux` and `_macos`
suffixes every list in a sidecar takes (`packages.h`), maps a name the
source uses to the spelling this platform should open:

```goose
// myprog.build
dylibs_linux = ["SDL2=libSDL2-2.0.so.0"];     // the soname every distro installs
dylibs_windows = ["SDL2=bin/SDL2.dll"];       // shipped with the program
```

The left side is the string in the source, the right side a spelling by
the table above. The program's sidecar maps names for every package the
program uses, and a package's sidecar may map its own as defaults; a name
no block in the program declares is an error, as a typo guard. Nothing in
the sidecar is required: a program without one opens the derived
spellings.

Why both: a package binding SDL2 wants to say `"SDL2"` once and work for
every user, and the derived spellings are what a user with the library
installed for development has. Deployment to machines with only the
runtime package, or with the DLL beside the executable, is the program's
knowledge, and its sidecar is where the program already says what it links
and where it builds. A list of candidate spellings tried in order was
considered and is deferred (§12).

## 4. What happens at run time

The runtime gains, in `runtime_os.h` (the half that includes the platform's
headers, so it lives in the runtime object and the `--standalone` unit):

```c
typedef struct gs_dllib  { const char *name, *win, *mac, *linux; } gs_dllib;
typedef struct gs_dlbind { int32_t lib; const char *sym; void **slot; } gs_dlbind;
GS_API void gs_dl_bind(const gs_dllib *libs, int64_t nlibs,
                       const gs_dlbind *binds, int64_t nbinds);
```

`gs_dl_bind` opens each library once (`dlopen(RTLD_NOW | RTLD_LOCAL)`,
`LoadLibraryW` of the UTF-8 spelling widened as `gs_os_*` widens paths),
never closes it, and stores each symbol's address in its slot. A library
entry carries the bare name and the sidecar's three spellings, `NULL` where
the default applies, and the runtime derives the default for the platform
it is compiled on, so the generated C stays portable across platforms as
the rest of it is. A failure is a runtime error through `gs_panic`, with
the text the loader gave:

```
cannot load SDL2 (libSDL2.so): libSDL2.so: cannot open shared object file: No such file or directory
SDL2 (libSDL2.so) has no symbol SDL_Foo
```

The program's `gs_program_init` calls it right after `gs_rt_init` and
before `gs_init_globals` (`codegen_emit.h`), so a global's initializer may
call a dynamic extern, and so may `fn main()` of a `--header` library,
since `goose_init` runs the same initializer. Binding happens on the main
thread before any worker exists; the slots are written once and read
thereafter, so `thread_fn`s read them without synchronization, as they do
the globals' initialized state. A library that must be called from the main
thread only is marked as today, `main_thread_only = true` in the sidecar of
the package declaring it, which the checker already applies to every
extern fn in the package (`typecheck.h`, the `thread_fn` walk).

The program never closes a library. `dlclose` of a library whose functions
a worker may still be in is the kind of problem the language exists to
avoid, and the process ends soon enough.

**Toolchain consequences.** `dlopen` is in libc on glibc 2.34 and later, in
`libdl` before; `-ldl` joins `SystemLibs()` (`build.h`) on Linux, where it
is a harmless stub on new systems. On macOS it is in libSystem and on
Windows `LoadLibraryW` is in kernel32, both linked already (the runtime
calls `GetProcAddress` today). A JIT run compiles the runtime with TinyCC,
which links libc but not libdl: `RunJit` adds `tcc_add_library(s, "dl")` on
Linux and ignores its failure, since a system without the archive is one
whose libc has the function; TinyCC does the same for its own bounds
checker (`tccelf.c`). The runtime's text changes, so `GS_RUNTIME_VERSION`
moves and the runtime object is rebuilt, as for any runtime change.

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
`SDL_Init(flags)`; the argument snapshots and the builder write-back around
the call (`EmitExternCall`) are unchanged. The pointer is named by the
spec's position in `usedexterns` and the symbol, which is deterministic
and distinct per declaration; the `gs_` prefix keeps it out of the way of
anything an `--include` header declares, so a header may `#include
"SDL.h"` for its own purposes without the prototype conflict the direct
form would have. Storing `dlsym`'s `void *` into a function pointer
through `void **` is what every POSIX program does and what the platform
ABIs define.

The libraries are listed in the order the program's blocks declare them
(file order, then source order) and the binds in `usedexterns` order, so
the output is stable across runs. The table and the slots are omitted
entirely when no dynamic extern is used, and the `/* ---- extern
prototypes ---- */` section keeps its prototypes for the static externs
beside them.

The calling convention is the platform's one C convention on the targets
the compiler has (x86-64, AArch64). 32-bit Windows `__stdcall` is not a
target. A variadic function cannot be bound: the declaration has no way to
say so and calling one through a fixed-arity pointer is undefined, and on
Apple AArch64 actually wrong. The compiler cannot detect it; §6 notes it as
a rule for the programmer and a wrapper in a native part is the way to
such a function.

## 6. Calling a real library without an adapter

The shapes of §7.10 were chosen with an adapter in mind: a header written
against the generated typedefs converts between what Goose passes and what
the library wants. With a dynamic extern there is no adapter, and the
declaration has to be the library's real prototype under Goose's shapes.
Most of a library's API fits; what does not is listed last.

* **Scalars** cross as their C types: `i32` is `int`, `u32` `unsigned`,
  `i64` `int64_t`, `f32` `float`, `f64` `double`, `bool` one byte. C's
  `long` is `i64` on Linux and macOS and `i32` on Windows, so a function
  with a `long` in its signature needs a per-platform declaration or an
  adapter; `size_t` is `u64` on every target.
* **Opaque handles**, the `SDL_Window *` a library returns and takes back,
  are `u64`. A pointer and a 64-bit integer are returned and passed the
  same way on both ABIs, and Goose cannot look through it, which is the
  point: it is a token. This is a convention this document establishes,
  not a change; `u64` already crosses.
* **A pointer to something Goose owns**, `T&` for a flat fixed `T`, is
  that pointer: `fn SDL_GetWindowSize(w: u64, x: i32&, y: i32&)`. A
  slice `T[:]` is the `{ data, len }` pair by value, which no C library
  takes; where C wants `(ptr, count)` the declaration takes `xs[0]` and
  `xs.len` as two parameters, and where it wants a NUL-terminated string
  the Goose side appends a `\0` and passes the first byte by reference:

  ```goose
  extern "SDL2" {
      fn SDL_CreateWindow(title: const u8&, x: i32, y: i32, w: i32, h: i32, flags: u32) -> u64;
  }
  let title = "Goose\0";
  let win = SDL_CreateWindow(title[0], 100, 100, 640, 480, 0);
  ```

  This is honest about what crosses but clumsy; a `stdlib` `ffi` module
  with the two helpers below is the natural follow-up.
* **Structs by value and by reference** cross in the packed layout of
  C.2, which is C's layout only when Goose's fields are already aligned as
  C would place them. `SDL_Rect { int x, y, w, h }` matches `struct Rect {
  x: i32, y: i32, w: i32, h: i32 }`; a struct with a `u8` before an `i32`
  matches only with `pad` written where C pads (§3.2 has `pad` and `pad n`
  for exactly this). Tail padding too: a C struct's size is a multiple of
  its alignment, so `{ a: i32, b: u8 }` needs `pad 3` at the end to be
  passed by value or stored in an array C indexes. The documentation says
  this plainly, with the two examples.
* **What needs an adapter still.** Reading memory the library owns, a
  `const char *` it returns (`SDL_GetError`) or an array it keeps
  (`SDL_GetKeyboardState`): Goose has no way to read through a `u64`, and
  should not grow one. Callbacks: a library that wants a function pointer
  (`qsort`, GLFW's event callbacks) has nothing to receive, since an
  `export fn` has a C symbol but no Goose value that is its address.
  Variadic functions (`SDL_Log`, `printf`). Each of these is a few lines of
  C in a native part today, and the two read helpers below would remove
  the first:

  ```goose
  // stdlib/ffi.goose, proposed, after the feature lands
  extern "gs_ffi_cstr"  fn cstr(p: u64, out: u8[>..]&);              // bytes at p up to a NUL
  extern "gs_ffi_bytes" fn bytes(p: u64, n: i64, out: u8[>..]&);     // n bytes at p
  ```

  They copy into Goose storage, so nothing foreign is ever a Goose value
  without a copy, which is the boundary rule of §9 (external bytes are
  validated as they enter). They trust the pointer as every extern call
  trusts C, and the documentation says that.

## 7. The compiler's pieces

* **AST.** `SFunction` gains `string dylib;`, empty for the extern of
  today. `isextern` stays true for both kinds, so everything that asks
  "is this a C function" is unchanged.
* **Parser** (`parser.h`, the `T_EXTERN` branch of the function parser).
  After `extern strlit`, `{` starts a block: a loop reading members until
  `}`, each parsed by the existing header path with `isextern` set, the
  library stored, and a `;` expected as for an extern declaration. The
  block's own errors: a body, a nested block, a member qualifier an extern
  rejects, an empty library string, a block below top level, a block that
  is empty (allowed; it is a comment's worth of declaration, and a package
  may generate one).
* **Checker.** `CheckExternSpec` is the check, unchanged. The
  `main_thread_only` walk is unchanged.
* **Codegen** (`codegen_calls.h`, `codegen.h`, `codegen_emit.h`). The
  pointer, the two tables and the bind call as §5 shows; `ExternProto`
  writes a pointer declaration for a dynamic spec and a prototype for a
  static one; `EmitExternCall` calls the pointer's name. The sidecar's
  spellings reach codegen through the `Ast`'s package records
  (`dylibs` on the package, merged program-over-package when the tables
  are emitted).
* **Native parts** (`native.h`). `NativePackagesUsed` skips a dynamic spec,
  so a package whose externs are all dynamic has no native part and a
  package with both does not have its native part pulled in by a dynamic
  call. `ExternNamesIn`, the lexer-level scan that lists the symbols a
  package's `goose_symbols` table must export, skips from `extern strlit
  {` to the matching `}`: those symbols are not the native part's.
* **Driver** (`main.cpp`, `build.h`, `jit.h`). `-ldl` on Linux links, and
  `tcc_add_library(s, "dl")` in `RunJit`, tolerated absent (§4). No
  compile-time probing, no new command-line options.
* **Sidecar** (`packages.h`). `dylibs` with the three suffixes joins the
  known keys; entries are `name=spelling`, checked for a `=`, kept on the
  package record. The program's entries override a package's when the
  tables are emitted; a name no block declares is an error at that point,
  when every block is known.
* **Runtime** (`runtime.h` for the declarations, `runtime_os.h` for the
  definitions). §4.
* **Editors.** The tree-sitter grammar (`tree-sitter/grammar.js`, the
  `extern` qualifier at the function declaration rule) and the VS Code
  grammar take the block; the language server sees ordinary functions
  and needs nothing. The Goose-in-Goose parser of the self-hosting tests
  (`test/goose_in_goose/parser.goose`) parses the block too, since it
  parses the stdlib and samples.
* **Wasm.** Not a target of this work; `wasm.md` §3 lists externs as a
  decoded-pointer boundary. Noted in §12 because the declaration is a
  wasm import by another name.

## 8. Alternatives considered

* **Link against the library instead of opening it.** Emit the prototype
  as today, put `-lSDL2` (`SDL2.lib`) on the link line, and for a JIT run
  have the compiler `dlopen` the library and `tcc_add_symbol` each used
  function, as it does for native parts. Rejected: the build then needs the
  import library or the unversioned `.so`, which is the `-dev` package or a
  hand-made `.lib`; the JIT and build paths resolve differently, and a
  mistake shows up as a link error in one and a load error in the other;
  and a DLL shipped without a `.lib` is unreachable. The pointer costs
  nothing the PLT does not.
* **The compiler opens the library at compile time** to verify the symbols
  exist, as a check. Rejected as the default: the machine that builds is
  often not the machine that runs (a CI builder without SDL installed),
  and a run-time error naming library and symbol before `main` is as
  early as it needs to be for the machine that matters. A `--probe-libs`
  check could be added for the developer's own machine (§12).
* **Per-declaration syntax**, `extern "SDL2" "SDL_Init" fn init(...)`.
  Rejected: two adjacent strings read as concatenation to anyone from a
  C-family language, and a binding is a library's worth of functions,
  which the block says once. A library-handle declaration (`lib sdl =
  "SDL2"; extern sdl fn ...`) was considered and dropped: a new
  declaration kind for one use.
* **The sidecar alone names the libraries** (`dylibs = ["SDL2"]`, with the
  externs declared as today and resolved by searching the listed
  libraries). Rejected: a package's binding should be complete in its
  source, a symbol's library should be visible where the symbol is
  declared, and a bare `extern fn` would silently mean two things.
* **Lazy binding on first call**, a check per call. Rejected for now: a
  branch per call for a feature (optional libraries) nobody has asked for,
  and an error in the middle of a run where one before `main` is
  clearer. See §12.

## 9. Testing

* **A library built for the test**, `test/extern_dylib.py`, with the
  toolchain the other native tests use (`scripts/toolchain.py`): a C file
  with the parameter shapes `extern_types.py` covers (scalars, a struct by
  value and by reference, a reference to an element, a builder) plus a
  function returning a handle and one taking it back, compiled into a
  shared library placed beside the test program. The program binds it with
  a bare name and the sidecar's spelling (`dylibs_linux = ["dltest=./libdltest.so"]`
  and its siblings, exercising the override and the relative path), and
  runs both ways, JIT and `goose build`, comparing output.
* **The failure paths**, same file: a block naming a library that does not
  exist, with one member called, exits with the runtime error naming the
  spelling tried; a member whose symbol the library lacks, likewise; the
  same declaration unused, runs. A static extern beside a dynamic one, in
  a package with a native part, still links the native part and not more.
* **Syntax and checking errors**, `test/errors_tc` and `test/syntax`: a
  member with a body, a nested block, a generic member, `checkfree` and
  `simd` members, an empty library string, a block inside a function, a
  sidecar `dylibs` entry without `=` or naming no block.
* **Determinism**: the generated C of the test program is compared across
  two compiles, as the self-hosting check compares its stages' C.
* **No-toolchain JIT**: a test that runs a dynamic-extern program under
  `goose` with no C compiler found (as `native_toolchain.py` arranges)
  and sees it run, which is the feature's headline and the case the
  native-part path cannot do. It binds the library the first test built,
  so it runs after it.
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
* A sample, `samples/`, binding SDL2 if it is present and otherwise
  printing why not, is tempting but a sample suite runs everywhere; the
  test's library is the example, and the spec's SDL2 block the
  illustration.

## 11. The plan

One working commit each, tests green at every step:

1. **Runtime**: `gs_dllib`, `gs_dlbind`, `gs_dl_bind`, the error texts,
   `-ldl` in `SystemLibs()`, `tcc_add_library("dl")` in `RunJit`. No user
   yet; the runtime version moves.
2. **Parser and AST**: the block, `dylib` on `SFunction`, the errors, the
   tree-sitter and Goose-in-Goose grammars, `ExternNamesIn` skipping
   blocks. Codegen rejects a dynamic spec with "not supported yet", so the
   tree builds and the error tests land here.
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
  That is lazy binding with a per-call check or an explicit `bind` call
  the program makes; deferred until a program needs it.
* **Candidate spellings in the source**, `extern "libSDL2-2.0.so.0",
  "SDL2" { ... }` tried in order, with spellings not of this platform's
  form skipped. Would let a package work on distros without the `-dev`
  symlink with no sidecar entry by the user; deferred because the sidecar
  covers it and the ordering rules are a second thing to explain.
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
  import, `(import "env" "log")`, with no runtime binding at all. When
  `wasm.md` is implemented the block maps onto imports by its library
  string; nothing here prevents it.
* **Cross-compilation** is not something the compiler does, but the
  generated C is portable and this design keeps it so (the runtime picks
  the spelling); if the compiler ever targets another platform, the
  sidecar's suffixed lists are already per target.
* **`long` and other platform-sized C types.** A declaration cannot say
  "C `long`". Two blocks under a `const` platform test, if the language
  gets one, or an adapter, until then.
