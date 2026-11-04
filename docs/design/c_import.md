# `import c`: C headers read by the compiler

Status: a prototype on the branch `c-import`, with measurements, to discuss.

## The problem

Every C library a Goose program uses needs its interface written twice:
once in C, by the library, and once in Goose, as `extern fn` declarations,
in the shapes §7.10 and reference §24 allow. Where a C function does not fit
those shapes, there is a third copy: an adapter in C. The adapter turns a
pointer into a `u64`, a slice into a NUL-terminated string, and a returned
string into a length plus a copy.

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

## The proposal

```goose
import c "sqlite3.h";
```

**What it does.** The compiler reads the header with the TinyCC it already
contains, and declares what it finds, under the C names, as a module of the
importing file's package. The module is generated Goose text that goes
through the normal pipeline, so diagnostics point into it and `--dump`
shows it. `goose --c-import x.h` prints it.

**The translation is reference §24 applied mechanically.** It has three
additions to the language, which make up the core of this proposal:

1. **`import c "x.h"`.** The header is found beside the importing file or
   in the sidecar's `c_include`.
2. **`cstr`, a parameter type of `extern fn`.**
   - The caller passes any `const u8[:]`, or `null`.
   - The call copies the bytes with a NUL after them, on the C stack when
     short, and frees the copy after the call.
   - The prototype says `const char *`, which is the library's own type.
3. **Optional references `T?` cross the boundary**, as pointers that may be
   NULL. Their C form already was one; `ExternParamOk` only had to stop
   rejecting them.

**Handles** need no language change.
- **What becomes a handle:**
  - a pointer to a struct the header never completes (`sqlite3 *`);
  - a pointer to a struct some function returns (`HPDF_Doc`).
- **The type** is `struct sqlite3 { p: u64 = 0 }`. It is typed, so a
  statement cannot be passed where a connection goes, and it crosses as the
  `u64` it holds on every 64-bit ABI.
- **A double pointer** (`sqlite3 **`) is `sqlite3?`: C writes the pointer
  into the handle.

**What the generated C checks.** It asserts what the module assumed about
the target (`sizeof(void *) == 8`, `sizeof(long) == 8` when `long` was
used). A build for Windows, where `long` has 4 bytes, therefore fails to
compile instead of passing wrong values.

**Implementation: about 1,100 lines.**
- `third_party/tinycc/tcc_cdecl.c` (315 lines):
  - It includes `libtcc.c`, so TinyCC's static internals are visible. No
    TinyCC file changes; the library is built from this file instead of
    `libtcc.c`.
  - It parses the header without generating code.
  - It walks the symbol table before TinyCC frees it.
  - It evaluates each object-like macro as a C constant expression, in the
    header's scope, so `((sqlite3_destructor_type)-1)` works.
  - It reports through a small C API (`tcc_cdecl.h`).
- `src/cimport.h` (589 lines) holds the translation.
- The parser, loader, typecheck and codegen need small changes (about 140
  lines).
- **Telling the header's names from the system's:** TinyCC reports every
  file the parse included. The system ones (outside the include
  directories) are parsed again on their own, and their names are
  subtracted.

## Measurements

### How much of a header translates

`goose --c-import`, macOS arm64:

| Header | Functions bound | Constants | Handles | Structs | Left out |
|---|---|---|---|---|---|
| sqlite3.h (3.53) | 287 | 511 | 10 | 3 | 14: 8 variadic, 3 `va_list`, 3 variables |
| zlib.h (1.3.2) | 86 | 42 | 2 | 2 | 2: 1 variadic, 1 `va_list` |
| expat.h (2.7.5) | 67 | 89 | 2 | 4 | 0 |
| hpdf.h (libharu) | 528 | 947 | 20 | 18 | 4: `HPDF_Date` by value, which C pads |
| SDL3/SDL.h | n/a | n/a | n/a | n/a | the whole header: TinyCC cannot read the macOS SDK's `<inttypes.h>` |

### One adapter replaced

A SQLite wrapper (connections, prepared statements, binding, columns,
online backup) of a larger program was rewritten on `import c
"sqlite3.h"`:

| | Before | After |
|---|---|---|
| C adapter | 118 lines | none |
| Goose `extern fn` written by hand | 22 | none |
| Goose for the wrapper | 145 lines | 158 lines, the logic that was in C now in Goose |
| Sidecar | | one `c_include` line |

The program's tests, which go through SQLite heavily, pass unchanged.

## What is still missing, in order of what it would save

1. **A pointer and a length as one slice.** `const void *p, size_t n` is
   two parameters, `const u8?` and `u64`.
   - **Passing the bytes:** the caller passes `v[0]` and `v.len`, which needs
     a non-empty slice. An empty blob needs a one-byte dummy.
   - **The trap:** the natural
     `let p = if n > 0 { &v[0] } else { &none[0] }` binds `p` to a *copy* of
     the byte, so C read one byte and then garbage. This was the one bug of
     the rewrite above, and only the tests caught it.
   - **Proposal:** a `bytes`-like parameter shape, a slice crossing as
     `(data, len)` with the data never NULL. The header cannot tell which
     pointer and length go together, so it would come from a small
     annotation, or from a parameter-name convention as an option.
2. **The JIT symbol table.** For `goose run`, the package's table names
   *every* `extern fn` in its sources. An imported header declares functions
   the library may be compiled without (sqlite's `sqlite3_carray_bind`), and
   the JIT library then fails to link. The table should name the externs
   that are called, or bind them weakly. `goose build` is unaffected.
3. **Callbacks.**
   - **Today:** a function pointer is a `u64`. expat's handlers and sqlite's
     `exec` callback still need C.
   - **Needed:** taking the address of an `export fn` as a C function pointer
     of a matching prototype, with `void *` user data.
4. **Structs C pads, by value.**
   - **Today:** `HPDF_Date { int ...; char ind; int ... }` cannot cross by
     value, because Goose structs are packed (§24).
   - **Needed:** a struct with C's natural layout (`extern struct`, say),
     usable only at the boundary.
5. **Names TinyCC drops.**
   - **Typedef names are lost where types are used:** a pointer typedef
     like `HPDF_Page` comes out as its struct, `HPDF_Dict_Rec`, and
     `sqlite3_int64` as `i64`.
   - **Function-like macros are not imported,** so `deflateInit` is missing
     and only `deflateInit_` is bound.
   - **Fix:** keeping typedef names needs a hook in TinyCC's declarator
     parsing. Simple function-like macros could become Goose functions when
     their body is an expression the module can type.
6. **Platform headers.** On macOS, TinyCC needs two clang predefines
   (`__has_extension`, the deployment target) to get past the SDK's
   availability headers, and still fails on `<inttypes.h>`. Headers that
   include only freestanding ones (`stddef.h`, `stdarg.h`, `stdint.h`)
   work. Linux glibc headers are untested.
7. **Returned memory.** A returned `const char *` is a `u64`, read with
   `ffi::cstr`. Nothing says whether it is borrowed or must be freed; that
   is the library's documentation, not its header.
8. **The target's layout.** The module is the compiling machine's view; a
   cross build only gets the assertions. Parsing for the target needs a
   TinyCC configured for it, or a native check part that includes the
   header and asserts each struct's size and offsets.

## Not changed

- **What may cross the boundary** is the same as in §7.10, plus `cstr` and
  `T?`.
- **Exports** still reject optional parameters.
- **Programs without `import c`** compile to the same C as before.
- **The test suite** passes as before, plus `packages/pkg_c_import` (every
  rule of the translation, built and under the JIT) and
  `errors/c_import_missing`.
