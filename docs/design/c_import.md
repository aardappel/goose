# `import c`: C headers read by the compiler

Status: a prototype on the branch `c-import`, with measurements, to discuss.
The open points of its first version are addressed; what remains is at the
end.

## The problem

Every C library a Goose program uses needs its interface written twice:
once in C, by the library, and once in Goose, as `extern fn` declarations,
in the shapes §7.10 and reference §24 allow. Where a C function does not fit
those shapes, there is a third copy: an adapter in C. The adapter turns a
pointer into a `u64`, a slice into a NUL-terminated string, a returned
string into a length plus a copy, a Goose function into a callback, and a
variadic call into a fixed one.

In a larger program using several C libraries, most of the adapter C is
mechanical, and it soon outgrows the Goose side of the boundary. Nothing
checks that the hand-written Goose signatures match the C ones. A wrong
width or a swapped parameter compiles, links and misbehaves, and adapters
are often built with warnings off, so not even a warning appears.

A typical adapter function, for one SQLite call:

```c
typedef struct { const uint8_t *data; int64_t len; } Slice;

uint64_t db_open(Slice path) {
    char *p = malloc((size_t)path.len + 1);       /* Goose strings have no NUL */
    memcpy(p, path.data, (size_t)path.len);
    p[path.len] = 0;
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(p, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    free(p);
    if (rc != SQLITE_OK) { sqlite3_close(db); return 0; }
    return (uint64_t)(uintptr_t)db;               /* a handle as u64 */
}
```

and on the Goose side, the same signature again:

```goose
extern fn db_open(path: const u8[:]) -> u64;
```

The same function with this proposal, with nothing else written:

```goose
import c "sqlite3.h";

fn open(path: const u8[:]) -> sqlite3 {
    var db = sqlite3 {};
    if sqlite3_open_v2(path, db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, null) != SQLITE_OK {
        sqlite3_close(db);
        return sqlite3 {};
    }
    return db;
}
```

## What `import c` does

```goose
import c "sqlite3.h";
```

The compiler reads the header with the TinyCC it already contains, and
declares what it finds, under the C names, as a module of the importing
file's package. The module is generated Goose text that goes through the
normal pipeline, so diagnostics point into it and `--dump` shows it.
`goose --c-import x.h` prints it. The header is found beside the importing
file or in the sidecar's `c_include`.

The module declares:

- **functions**, as `extern fn` bound to their symbols, variadic ones with
  `...`;
- **handles** for structs the header never completes, or that a function
  hands out: `extern struct sqlite3 { p: u64 = 0 }`. A handle is named by a
  typedef of the pointer where the header has one (`XML_Parser`), and a
  `T **` to one is `T?`, where C writes the pointer;
- **complete structs**, as extern structs (below) with `pad` where C pads;
- **constants**: enum constants and constant object-like macros as `let`s,
  evaluated by TinyCC in the header's scope, so
  `((sqlite3_destructor_type)-1)` works;
- **typedefs** of scalars and of handle pointers, as aliases
  (`type sqlite3_int64 = i64;`, `type HPDF_Page = HPDF_Dict;`), used in the
  signatures, which then read as the header does;
- **wrapper macros**: a function-like macro whose body calls a bound
  function with its parameters and constants becomes a Goose function of
  untyped parameters. zlib's `deflateInit(strm, level)` is
  `deflateInit_(strm, level, "1.3.2", 112)`, with the constants as C
  computes them (`ZLIB_VERSION`, `sizeof(z_stream)`).

What has no Goose shape is left out: variables, statics, `va_list`
functions, and unions, bitfields or `long double` by value. The module
lists them at its end. Using one is an error that says why, where it is
used, rather than an unknown name:

```
cis_take: not imported from c_import_skipped.h (parameter 1, a union by value)
```

## Additions at the C boundary

The translation is reference §24 applied mechanically. Where §24 needed an
adapter, the boundary gains a shape, usable in hand-written `extern fn`s
too. These are the language part of the proposal, each its own commit:

1. **`cstr`**, a parameter type. The caller passes any `const u8[:]`, or
   `null`. The call copies the bytes with a NUL after them, on the C stack
   when short, and the prototype says `const char *`.
2. **Optional references `T?` cross the boundary** as pointers that may be
   NULL. At an extern's `T?`, a slice or a stored array of `T` also passes
   its elements' address, which an empty slice has too. So
   `(const int *p, size_t n)` takes `(v, v.len as u64)`, with no dummy
   element for an empty `v`.
3. **`cptr` and `const cptr`**, parameter types for `void *` and
   `const void *`. They take a `u64` (an address C gave), `null`, a
   reference, a slice or a stored array (its elements), or other storage,
   bound by reference as at a `T&`. Another integer is an error, since C
   would take its value for an address: `b[0]` meant as the byte's.
4. **C function pointers**: an extern parameter written `fn(P...) -> R`,
   with scalar `P` and `R` (pointers as `u64`).
   - It takes `cfn(f)`, `null`, or an address held as `u64`.
   - `cfn(f)` is the address of the C wrapper an `export fn f` already has,
     and at such a parameter `f`'s parameters and result must be the
     signature's.
   - Function values stay compile-time only (§7.6): C gets the wrapper,
     not a Goose value.
   - C may call `f` back during any extern call. The call saves the top of
     the caller's data stacks in `gs_export_sp`, where the wrapper starts
     `f`'s. The stack count and the `gs_sp` analysis take `f` as a callee of
     every extern call.
5. **`extern struct`**: a struct whose pads put each field where C places
   it, which the checker verifies, saying which pad is missing.
   - **By value** (at an extern call, in an export's wrapper and in a
     result), it crosses as a C struct of the same fields in C's natural
     layout, with asserts on every offset and the size.
   - **The bytes are the same**, so a copy converts, and the calling
     convention is C's: `{ f32, pad 4, f64 }` arrives in the float
     registers, where a packed struct with a pad would not.
6. **`...`** ends an extern's parameters.
   - **How a call is typed:** a call with N more arguments is a call of an
     instance with N untyped parameters, so each call's further arguments
     are specialized by their types, as an untyped parameter's are (§7.1).
   - **What may follow:** integers (a literal that fits as C's `int`),
     floats, `bool`, references (pointers), and bytes, which go as a C
     string as for `cstr`.
   - **The prototype** is the function's own with `...`, so C promotes the
     arguments.

## Checks

The module is TinyCC's view of the header on the machine the compiler runs
on. Three things keep a build from trusting it where it is wrong:

- **The generated C asserts** what the module relies on of the target:
  `sizeof(void *)`, and `sizeof(long)` where `long` was used.
- **The header check.** Where the package has C sources, its native part
  also compiles a generated file that includes the header itself. That file
  asserts the size and every offset of each struct the module declares,
  and the size of each typedef it names. It is compiled by the program's
  own compiler, so a cross build or a header TinyCC reads differently stops
  there. `goose --c-import-check x.h` prints the file.
- **The JIT library's symbol table** names only the imported functions a
  program calls. A header declares functions the library may be built
  without (sqlite's for options left out), which would not link. The names
  add up in the build directory, so programs calling different ones do not
  rebuild it in turn.

## Implementation

The branch is a series of commits on the current master, one per piece, each
building and passing the tests on its own.

- `third_party/tinycc/tcc_cdecl.c` (415 lines):
  - It includes `libtcc.c`, so TinyCC's static internals are visible.
  - It parses the header without generating code, and walks the symbol
    table before TinyCC frees it.
  - It evaluates object-like macros, and the constant arguments of wrapper
    macros, as C constant expressions in the header's scope.
  - It reports through a small C API (`tcc_cdecl.h`).
- **One optional commit changes TinyCC:** `CType` gets an `int tdef`, the
  typedef a type was written with, in what was padding. Dropping that
  commit loses the typedef names (only the header check of the commit after
  it reads them).
- **One commit fixes TinyCC's x86-64 calling convention.**
  - **The bug:** it gave a struct mixing integer and float eightbytes one
    class, so `{ i64, f64 }` went to C in two integer registers, where the
    double belongs in an SSE one.
  - **Who it hit:** a JIT run passing or returning such a struct by value
    to C on Linux or macOS x86-64. It hit any extern, not only
    `import c`'s; a build was right.
  - **What changed:** the struct is now classified per eightbyte, as System
    V requires.
- `src/cimport.h` (953 lines) holds the translation and the header check.
- **The boundary additions and the loader** are about 840 lines in the
  parser, loader, typecheck and codegen.
- **Telling the header's names from the system's:** TinyCC reports every
  file the parse included. The system ones (outside the include
  directories) are parsed again on their own, and their names are
  subtracted.

## Measurements

### How much of a header translates

`goose --c-import`, macOS arm64:

| Header | Functions (variadic) | Constants | Extern structs (handles) | Aliases | Wrapper macros | Callback parameters | Header check asserts | Left out |
|---|---|---|---|---|---|---|---|---|
| sqlite3.h 3.53 | 295 (8) | 511 | 13 (10) | 2 | 0 | 60 | 44 | 6: 3 `va_list`, 3 variables |
| zlib.h 1.3.2 | 87 (1) | 42 | 4 (2) | 5 | 5 | 2 | 34 | 1 `va_list` |
| expat.h 2.7.5 | 67 | 89 | 6 (2) | 4 | 0 | 31 | 21 | 0 |
| hpdf.h (libharu) | 532 | 947 | 38 (20) | 31 | 0 | 14 | 104 | 0 |
| SDL3/SDL.h | 1224 (16) | 2233 | 99 (49) | 37 | 3 | 47 | 401 | 23: 16 static inline, 7 `va_list` |

The first version of the prototype bound 287, 86, 67 and 528 functions of
the first four, and could not read SDL3/SDL.h at all. The header checks of
all five hold with clang.

### One adapter replaced

A SQLite wrapper (connections, prepared statements, binding, columns,
online backup) of a larger program was rewritten on `import c
"sqlite3.h"`:

| | Before | After |
|---|---|---|
| C adapter | 118 lines | none |
| Goose `extern fn` written by hand | 22 | none |
| Goose for the wrapper | 145 lines | 155 lines, the logic that was in C now in Goose |
| Sidecar | | one `c_include` line |

The program's tests, which go through SQLite heavily, pass unchanged. The
first version needed a one-byte dummy to bind an empty blob, and binding the
pointer with `let p = if n > 0 { &v[0] } else { &none[0] }` copied the byte,
which was the rewrite's one bug. Now the slice itself is passed:
`sqlite3_bind_blob64(st, i, v, v.len as u64, SQLITE_TRANSIENT)`.

## Comparison with Zig

Zig is the closest design: `@cImport` translated a header at compile time
with libclang, and since 0.16 the build system does it (`b.addTranslateC`),
backed by a C compiler written in Zig (Aro).

| | Zig | This prototype |
|---|---|---|
| Where the translation runs | the build system (0.16); before, a language builtin | the compiler's loader, on `import c` |
| C parser | Aro (before, libclang) | the TinyCC every Goose compiler already links |
| Untranslatable names | `@compileError` stubs, an error only where used | the same, through the unknown-name errors |
| Function-like macros | inline functions where the body allows | functions of untyped parameters, for bodies that call a function |
| C integer types | `c_int`, `c_long`, sized per target | fixed widths, asserted per target |
| Strings | sentinel-terminated pointers `[*:0]const u8`, no copy | `cstr`, copied with a NUL |
| Pointer and length | `s.ptr, s.len` | a slice at a `T?` or `cptr` passes its elements |
| Callbacks | `callconv(.c)` functions | `cfn(f)` of an `export fn` |
| C layout | `extern struct` | `extern struct` |
| Variadic functions | callable | callable, with `...` |
| Other targets | headers of many C libraries shipped, parsed for the target | the target's own compiler checks the module against the header |

Zig moved C translation out of the language into the build system: to drop
a dependency on libclang from the compiler, and to let the build step take
options. Goose's sidecar could host `import c` the same way, as a
`c_import = ["sqlite3.h"]` list making the module importable. The
boundary additions above are needed either way.

## What is still open

1. **Returned memory.** A returned `const char *` is a `u64`, read with
   `ffi::cstr`. Whether it is borrowed or must be freed is the library's
   documentation, not its header.
2. **Function pointers outside parameters.** A function pointer in a struct
   field (`sqlite3_vfs`) or returned by C stays a `u64`. A struct field
   could take `cfn(f)` as a `u64` today, unchecked.
3. **A callback that leads back to itself while it holds data stacks** is
   a cycle the stack analysis rejects with an internal error; it should be
   a user-facing one.
4. **Platforms.** Linux and Windows are untried. On Linux the system
   directories are added (`/usr/include` and the multiarch one). On
   Windows, `long` is 4 bytes, which the asserts catch, but the headers of
   the platform's C library are not looked for.
5. **The header check needs C sources** in the package, since it is
   compiled with its native part. A package binding a system library with
   `c_libs` alone gets the target asserts only.
6. **Where the import is written.** `import c` in the source, or a sidecar
   list as Zig's build step does: the semantics are the same, and both
   could be offered.
