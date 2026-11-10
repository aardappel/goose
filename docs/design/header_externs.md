# Header externs: calling C headers, intrinsics, and fixed-length slice windows

<details>
<summary>Table of contents</summary>

- [1. Decisions](#1-decisions)
- [2. The boundary today](#2-the-boundary-today)
- [3. The declaration](#3-the-declaration)
- [4. Fixed-length slice windows: `s[lo..+N]`](#4-fixed-length-slice-windows-slo+n)
- [5. Target dispatch: `target_has` and `GS_TARGET`](#5-target-dispatch-target_has-and-gs_target)
- [6. The generated C](#6-the-generated-c)
- [7. What happens in `goose run`](#7-what-happens-in-goose-run)
- [8. The compiler's pieces](#8-the-compilers-pieces)
- [9. Alternatives considered](#9-alternatives-considered)
- [10. Testing](#10-testing)
- [11. Deferred and open](#11-deferred-and-open)

</details>

Today an `extern fn` is a C symbol that the linker resolves
(`docs/design/dynamic_externs.md` adds shared libraries opened by name). That
covers functions in `libc`, a package's native part, or a DLL. It does not
cover macros, `static inline` functions, or compiler intrinsics declared in a
C header (`<emmintrin.h>`, `<wmmintrin.h>`, `<arm_neon.h>`, `<string.h>`).
Those have no symbol in any library: the C compiler expands them in place into
one or two instructions (`_mm_aesenc_si128`, `_mm_shuffle_epi8`,
`_mm_clmulepi64_si128`), often requiring an immediate integer constant and a
target attribute on the calling C function. Calling them through a
hand-written C file in a native part adds a C source per package and loses
constant-immediate folding, register allocation across calls, and inlining.

This document specifies header-backed `extern` blocks, together with the three
language pieces that let SIMD and systems headers work without C types or
pointers in Goose: fixed-length slice windows `s[lo..+N]`, `const` integer
parameters, and the `target_has` builtin. Written and implemented against the
tree at `335fddc1`.

## 1. Decisions

* The source names the header. `extern "<emmintrin.h>" target "sse2" { ... }`
  declares functions, constants, and type aliases backed by a C header. A
  string after `extern` names a header when it is enclosed in `<...>` or ends
  in `.h`, `.hpp`, `.hh`, or `.inc`. Codegen emits `#include <emmintrin.h>`
  only when at least one member of the block is reached after dead-branch
  pruning and optimization, and omits C prototypes for the block's functions.
* Nothing new crosses the boundary. Inside Goose, every value has an ordinary
  Goose type (`u8[16]`, `const u8[:]`, `u8[:]`, `u8[16]&`), packed to 1 byte
  as in C.2. A block member `type m128i = u8[16] = "__m128i";` is a type alias
  for `u8[16]` everywhere in the compiler (`SAlias`). At the C call site
  (`EmitExternCall`), codegen converts between `a16_u8` and `__m128i` through
  `static inline` helpers (`gs_to_c_a16_u8`, `gs_from_c_a16_u8`) using
  `memcpy` and checked by `_Static_assert(sizeof(__m128i) == 16)`. Pointers
  cross as Goose slices (`const T[:]`, `T[:]`, passing `.data`) or references
  (`const T&`, `T&`, `const void?`, `void?`).
* Fixed-length slice windows are a general expression. `s[lo..+N]` (and
  `s[..+N]`, where `lo` is `0`) is an lvalue of fixed-array type `T[N]`
  (`A_FIXED`), valid anywhere an expression or assignment target of type
  `T[N]` is valid. Loading 16 bytes from a slice into an `m128i` is
  `let v: m128i = s[i..+16];` and storing one back is `dst[i..+16] = v;`, so
  `_mm_loadu_si128` and `_mm_storeu_si128` are not needed. A window is checked
  by `gs_bc_win(lo, N, len, file, line)` and elided by `bce.h` when the
  surrounding loop or guard proves `lo >= 0` and `lo + N <= s.len`.
* `const` parameters specialize in the checker. A parameter declared
  `const imm: i32` requires a compile-time integer constant (or an
  `extern const` symbol) at the call site. For a Goose function,
  `GetOrCreateSpec` includes the constant values in the `FnSpec` key and binds
  the parameter as a constant in the cloned body before checking it.
  Forwarding a `const` parameter through helper functions works regardless of
  inlining, and `EmitExternCall` writes the integer literal or C constant name
  into the C call so macro and intrinsic immediate operands compile.
* `target_has("...")` handles compile-time pruning and runtime CPUID dispatch,
  and target attributes propagate along call edges. There are no `@target`
  attributes or `when` statements.
  `if target_has("x86_64,sse2,aes,pclmul") { ... } else if target_has("aarch64,neon") { ... }`
  folds to compile-time `false` when the architecture or OS in the string does
  not match the target, and `CheckIf` skips typechecking the dead branch so
  foreign intrinsics and headers (`<arm_neon.h>` on x86-64) are not
  instantiated or included. When the architecture and OS match and the string
  names non-baseline ISA features, `target_has` lowers to a runtime check
  `gs_cpu_has(mask)` cached on first call. Any ISA feature on a called
  `extern` function that is not guarded by an enclosing `if target_has(...)`
  at the call site propagates to the caller's `FnSpec::target_attr`
  (`GS_TARGET("...")`), and the inliner does not inline a callee into a caller
  with a different `target_attr`.
* `goose run` caches a thunk library when TinyCC cannot parse a system
  intrinsic header. Standard C headers (`<string.h>`, `<stdlib.h>`) compile
  in-process with TinyCC. When a reached header uses compiler-specific
  intrinsics (`<emmintrin.h>`, `<wmmintrin.h>`), `CompileHeaderExternThunks`
  builds a shared library of C wrapper functions once into
  `~/.goose/cache/hdr-thunks/<hash>/` and registers them with
  `tcc_add_symbol`.

## 2. The boundary today

An `extern fn` today (`spec.md` §7.10, `implementation.md` §6) emits a C
prototype in `/* ---- extern prototypes ---- */` using Goose's generated types
(`gs_slice`, `a16_u8`, pointers to packed structs) and calls the symbol by
name. That works when the implementation is written against Goose's types (the
runtime or a `--header` host) or is a standard C function taking scalars
(`sin`, `puts`).

Three problems arise when the C side is a system header such as
`<emmintrin.h>` or `<wmmintrin.h>`:

1. The header defines the symbol already, often as a macro or `static inline`
   taking a compiler-specific vector type. Emitting
   `extern a16_u8 _mm_xor_si128(a16_u8, a16_u8);` clashes with
   `<emmintrin.h>`'s `__m128i _mm_xor_si128(__m128i, __m128i)`. Putting
   `__m128i` inside Goose's own structs or activation frames would break
   C.2's `#pragma pack(push, 1)` layout, where every Goose type has alignment
   1.
2. Intrinsics take immediate operands and raw pointers.
   `_mm_clmulepi64_si128(a, b, imm8)` and `_mm_prefetch(p, sel)` are macros or
   `__builtin_ia32_*` wrappers whose selector argument must be a C constant
   expression at the call site. A C function taking `const void *` or
   `uint8_t *` expects a pointer, whereas a Goose slice is a `gs_slice` struct
   passed by value.
3. GCC and Clang refuse to inline target-specific intrinsics into a baseline
   function. In `<wmmintrin.h>`, `_mm_aesenc_si128` is marked
   `__attribute__((__always_inline__, __target__("aes")))`. Calling it from a
   C function that lacks `__attribute__((target("aes")))` is a compile error
   (`target specific option mismatch`), even inside `if (gs_cpu_has(...))`.

## 3. The declaration

```goose
extern "<emmintrin.h>" target "sse2" {
    type m128i = u8[16] = "__m128i";

    fn mm_set1_epi8(a: i8) -> m128i = "_mm_set1_epi8";
    fn mm_xor_si128(a: m128i, b: m128i) -> m128i = "_mm_xor_si128";
    fn mm_cmpeq_epi8(a: m128i, b: m128i) -> m128i = "_mm_cmpeq_epi8";
    fn mm_movemask_epi8(a: m128i) -> i32 = "_mm_movemask_epi8";
    fn mm_slli_si128(a: m128i, const imm8: i32) -> m128i = "_mm_slli_si128";
}

extern "<xmmintrin.h>" target "sse2" {
    const MM_HINT_T0: i32 = "_MM_HINT_T0";
    fn mm_prefetch(p: const u8[:], const sel: i32) = "_mm_prefetch";
}

extern "<string.h>" fn c_memcmp(a: const void?, b: const void?, n: u64) -> i32 = "memcmp";
```

The grammar extends `extern` at top level:

```text
externdecl   := "extern" headerspec? targetclause? (externfn | externconst | "{" externmember* "}")
headerspec   := strlit
targetclause := "target" strlit
externmember := externfn | externconst | externtype
externfn     := "fn" declname "(" params? ")" ("->" rettypes)? ("=" strlit)? ";"
externconst  := "const" ident ":" type ("=" strlit)? ";"
externtype   := "type" ident "=" type ("=" strlit)? ";"
param        := ("var" | "const")? ident (":" type)? ("=" expr)?
```

Here `target` is a contextual identifier checked after the header string, not
a keyword. A header string enclosed in `<...>` is emitted as `#include <...>`;
any other header string (`"my_header.h"`) is emitted as `#include "..."`.

Inside an `extern` block, `type Name = GooseType = "CType";` defines a
standard `SAlias` whose underlying type is `GooseType`, and records `"CType"`
on the alias (`c_repr`) together with the block's header (`c_header`).
`GooseType` must be a flat, fixed-size type (`SC_FIXED`, holding no references
or slices). In variable declarations, array literals `[0; 16]`, indexing
`v[i]`, struct fields, equality `==`, and slice windows `s[i..+16]`, `m128i`
is `u8[16]`. When a value of type `m128i` is passed to or returned from a
header-backed `extern fn`, codegen converts between `a16_u8` and `__m128i`
(§6).

A header-backed `extern fn` takes ordinary Goose slices and references, which
codegen lowers to the pointers C expects:

| Goose parameter | Lowered C argument |
|---|---|
| `T` (scalar `i8`..`u64`, `f32`, `f64`, `bool`) | scalar value |
| `Name` (`type Name = GooseType = "CType"`) | `gs_to_c_<GooseCType>(arg)` |
| `const T&` / `T&` (where `T` has `c_repr = "CType"`) | `(const CType *)(arg)` / `(CType *)(arg)` |
| `const T&` / `T&` (plain fixed type `T`) | `(const void *)(arg)` or typed pointer |
| `const T[:]` / `T[:]` (such as `const u8[:]`, `u8[:]`) | `(const ElemC *)(slice.data)` / `(ElemC *)(slice.data)` |
| `const void?` / `void?` | `arg` (`const void *` / `void *`, nullable; `null` is `NULL`) |

In Goose, a local `let x = ...` has writable storage so its fields can be
mutated, but passing a `let` local to a non-`const` reference parameter `T&`
of a header-backed `extern fn` is rejected: the C function receives a mutable
pointer, so the caller must declare the variable `var`.

A `const` member inside a header block (`const NAME: T = "C_MACRO";`) declares
a read-only global of integer type `T` whose C representation is the given
macro or enum constant name (or `NAME` when `= "..."` is omitted). It can be
used both as a runtime value and as the argument to a `const` parameter
(`mm_prefetch(s[i..], MM_HINT_T0)`).

A parameter marked `const` on an `extern fn` or a Goose `fn` must have an
integer type (`i8`..`i64`, `u8`..`u64`). Every call must pass a compile-time
integer constant that fits the parameter's type (`FitsIntStorage`), an
`extern const` (or arithmetic on `extern const`s), or an enclosing function's
`const` parameter. On a Goose function, `GetOrCreateSpec` keys the
specialization on the `const` arguments (`FnSpec::const_args`) and binds the
parameter's `VarDef` as a constant (`constlit = true` or `c_const_expr`)
before checking the body. `const` parameters are omitted from the emitted C
function's parameter list and substituted at each use.

## 4. Fixed-length slice windows: `s[lo..+N]`

A slice expression written with `..+` instead of `..` produces a fixed-length
window of `N` elements starting at `lo` (or at `0` when written `s[..+N]`):

```goose
let block: m128i = plaintext[off..+16];
let c = mm_xor_si128(block, round_key);
dst[off..+16] = c;
```

`s` may be a slice (`T[:]`, `const T[:]`), fixed array (`T[M]`), or resizable
array (`T[..]`, `T[>..]`, `T[M..]`). `lo` is checked as `i64` (defaulting to
`0`). `N` is evaluated by `ConstIntOrError` as a compile-time integer and must
be at least `1`. When `s` has a static capacity `M` (`A_FIXED` or static
`A_LIMITED`) and `N > M`, or `lo` is a constant with `lo < 0` or `lo + N > M`,
the compiler reports an error at the slice expression.

`s[lo..+N]` has type `T[N]` (`A_FIXED`) and is an lvalue with the provenance
and writability of `s`. Reading it copies `N` elements
(`*(aN_T *)(... + lo)`); assigning `s[lo..+N] = v` writes `N` elements in
place when `s` is writable and is a compile error when `s` is `const`; passing
`s[lo..+N]` to a `const T[N]&`, `T[N]&`, or `T[:]` parameter binds the
reference or slice directly to those `N` elements without copying.

At run time, `s[lo..+N]` checks `lo` and `N` against `len` in a single
unsigned comparison in `runtime.h`:

```c
static GS_INLINE int64_t gs_bc_win(int64_t lo, int64_t n, int64_t len,
                                   const char *file, int line) {
    if (GS_UNLIKELY((uint64_t)lo > (uint64_t)(len - n) || len < n))
        gs_panic_slice(lo, lo + n, len, file, line);
    return lo;
}
```

`ProveBounds` in `bce.h` sets `SliceExpr::elide = true` (omitting `gs_bc_win`)
when `0 <= lo` and `lo + N <= s.len` follow from:

1. a fixed array `T[M]` with constant `0 <= lo <= M - N`;
2. a `while i + K <= s.len` (or `<`) loop where `i` starts non-negative, only
   increments in the body, and the window is `s[i + off..+N]` with `off >= 0`
   and `off + N <= K`;
3. a limit binding `let limit = s.len - K` under a guard `s.len >= K` in a
   `while i <= limit` loop; or
4. a tail window `s[s.len - N..+N]` inside `if s.len >= N` or
   `guard s.len >= N`.

## 5. Target dispatch: `target_has` and `GS_TARGET`

The builtin `target_has("...") -> bool` handles cross-architecture conditional
compilation and runtime CPU feature detection:

```goose
fn dispatch_block(blk: u8[16], key: u8[16]) -> u8[16] {
    if target_has("x86_64,sse2,aes,pclmul") {
        return aes_pclmul_helper(blk, key);
    } else if target_has("aarch64,neon") {
        return neon_helper(blk, key);
    }
    return scalar_helper(blk, key);
}
```

Both `extern "..." target "..."` and `target_has("...")` validate their
comma-separated list of tokens at compile time against `CanonicalTargetTable`
(`ast.h`):

* Architectures: `x86_64`, `aarch64`, `wasm32`, `wasm64`.
* Operating systems: `linux`, `windows`, `macos`, `wasi`.
* x86-64 ISA features: `sse2` (baseline on `x86_64`), `ssse3`, `sse4.1`,
  `sse4.2`, `avx`, `avx2`, `avx512f`, `aes`, `pclmul`, `bmi1`, `bmi2`,
  `popcnt`.
* AArch64 ISA features: `neon` (baseline on `aarch64`), `pmull`, `crc`,
  `sha2`.

An empty string, an unknown token (`"avx999"`), or an ISA token paired with a
different architecture (`"aarch64,avx2"`) is a compile-time error.

When any architecture token, OS token, or foreign-architecture ISA token in
`target_has("...")` does not match the compilation target, the call evaluates
to compile-time `false` (`target_const = 0`). When every token is satisfied by
the target's baseline (`"x86_64,sse2"` on x86-64, `"aarch64,neon"` on
AArch64), it evaluates to compile-time `true` (`target_const = 1`). In
`CheckIf` (`typecheck_flow.h`), `EvalStaticTargetCond` evaluates
`target_has(...)` and `!`, `&&`, `||` combinations of it before checking the
branches: when the condition is statically `true` or `false`, the dead branch
is not typechecked, its calls create no `FnSpec`s, and headers used only by
functions in that branch are not emitted.

When the architecture and OS match and the string names non-baseline ISA
features, `target_has` lowers to `gs_cpu_has(mask)` (`runtime.h`), where
`mask` is a bitmask of `GS_CPU_*` flags. `gs_cpu_detect_features()` runs
`cpuid` and `xgetbv` once on x86-64 (checking OS XMM/YMM/ZMM state support in
XCR0 before setting AVX/AVX2/AVX-512 bits) and caches the bitmask in an atomic
`uint32_t` (`__F_INIT` bit).

A function declared in `extern "..." target "sse2,aes,pclmul"` starts with
`target_attr = "sse2,aes,pclmul"`. While `CheckIf` checks the `then` branch of
`if target_has("...")`, it pushes the condition's ISA features onto the
current body's `guarded_targets` stack (`ActiveGuardedTargets()`). At every
call from `caller` to `callee`, `ResolveCall` records a
`TargetEdge { callee, guarded }`. After all roots are checked,
`PropagateTargetAttrs()` runs a fixed-point loop over `ast.fnspecs`: any
feature in `callee->target_attr` that is not in `edge.guarded` is merged into
`caller->target_attr`.

Helper functions that call intrinsics (`aes_pclmul_helper`), and mid-level
helpers that call them without a guard, therefore receive
`GS_TARGET("sse2,aes,pclmul")` without a target annotation in user code. At
the `if target_has("x86_64,sse2,aes,pclmul")` boundary in `dispatch_block`,
the features are guarded, so `dispatch_block` keeps an empty `target_attr` and
compiles for the baseline CPU. `Optimizer::TryInline` checks
`curspec->target_attr == K->target_attr` before inlining `K` into `curspec`,
so `aes_pclmul_helper` is not inlined across the `target_has` guard into
`dispatch_block`.

## 6. The generated C

For a program on x86-64 that calls `mm_aesenc_si128`, `mm_clmulepi64_si128`,
and `mm_xor_si128` inside `aes_pclmul_helper`, guarded by
`if target_has("x86_64,sse2,aes,pclmul")` in `dispatch_block`, with an
`extern "<arm_neon.h>"` block also declared in the file:

```c
/* ---- includes ---- */
#include <wmmintrin.h>
#include <emmintrin.h>

/* ---- extern prototypes ---- */
_Static_assert(sizeof(__m128i) == 16, "C type size mismatch for __m128i");
static GS_INLINE __m128i gs_to_c_a16_u8(a16_u8 v) { __m128i r; memcpy(&r, &v, 16); return r; }
static GS_INLINE a16_u8 gs_from_c_a16_u8(__m128i v) { a16_u8 r; memcpy(&r, &v, 16); return r; }

/* ---- prototypes ---- */
static void main_g(int64_t gs_sp);
static GS_TARGET("sse2,aes,pclmul") a16_u8 aes_pclmul_helper_g(a16_u8 blk_g, a16_u8 key_g);

/* ---- code ---- */
static GS_TARGET("sse2,aes,pclmul") a16_u8 aes_pclmul_helper_g(a16_u8 blk_g, a16_u8 key_g) {
    a16_u8 enc_g = gs_from_c_a16_u8(_mm_aesenc_si128(gs_to_c_a16_u8(blk_g), gs_to_c_a16_u8(key_g)));
    a16_u8 mul_g = gs_from_c_a16_u8(_mm_clmulepi64_si128(gs_to_c_a16_u8(enc_g), gs_to_c_a16_u8(key_g), 0));
    return gs_from_c_a16_u8(_mm_xor_si128(gs_to_c_a16_u8(enc_g), gs_to_c_a16_u8(mul_g)));
}
```

`<arm_neon.h>` and `veorq_u8` are absent from the generated C because the
`target_has("aarch64,neon")` branch was pruned in `CheckIf`. In `main_g` (into
which `dispatch_block` was inlined, since both have empty `target_attr`), the
guard is `if (gs_cpu_has(384u))`, calling `aes_pclmul_helper_g`. When
`HoistAggregateDecls` rearranges sections for `--standalone` or separate
compilation, `/* ---- includes ---- */` stays above `#pragma pack(push, 1)` so
system headers are not parsed under 1-byte struct packing.

## 7. What happens in `goose run`

In `goose run`, generated C is compiled in memory by TinyCC (`jit.h`). When
the program uses only standard headers that TinyCC can compile directly
(`<string.h>`, `<stdlib.h>`), `goose run` compiles the program in TinyCC
without invoking an external C compiler.

System SIMD headers (`<emmintrin.h>`, `<tmmintrin.h>`, `<wmmintrin.h>`,
`<immintrin.h>`) shipped with GCC, Clang, and MSVC define intrinsics using
compiler-specific builtins (`__builtin_ia32_*`) or MSVC intrinsics that TinyCC
does not recognize. When `CodeGen` is invoked with `jit_header_thunks = true`
(which `main.cpp` enables when reached headers need the host toolchain, or as
a fallback if TinyCC rejects a header):

1. For each reached header-backed `FnSpec` and `extern const`, `CodeGen` emits
   an `extern` prototype for a generated C wrapper name
   (`__goose_hdr_thunk_<id>`) instead of `#include <...>` in the C string
   handed to TinyCC, and appends the wrapper's definition (with
   `GS_TARGET("...")` and the `#include`s) to `cg.jit_thunk_c_source`.
2. `CompileHeaderExternThunks` (`native.h`) hashes `cg.jit_thunk_c_source`
   together with the discovered C toolchain (`DiscoverToolchain`) and checks
   `~/.goose/cache/hdr-thunks/<hash>/`. If the shared library is not cached
   yet, it compiles the wrapper once into a `.dll` / `.so` / `.dylib`
   exporting `goose_symbols`.
3. `LoadSharedSymbols` loads the cached library and `RunJit` registers each
   `__goose_hdr_thunk_<id>` with `tcc_add_symbol`. Subsequent runs load the
   cached library without starting the C compiler.

## 8. The compiler's pieces

* AST (`ast.h`). `SliceExpr` gains `bool is_window = false;` and
  `int64_t win_len = -1;`. `Param` gains `bool is_const = false;`. `SFunction`
  gains `string cheader;` and `string target_attr;`. `SAlias` and `TypeExpr`
  gain `string_view c_repr;` and `string_view c_header;`. `VarDef` gains
  `string c_header;`, `string c_cname;`, and `Node *c_const_expr = nullptr;`.
  `FnSpec` gains `vector<ConstArgBind> const_args;`, `string target_attr;`,
  and `vector<TargetEdge> target_edges;`. `Call` gains
  `uint32_t target_mask = 0;` and `int target_const = -1;`.
* Lexer and parser (`lexer.h`, `parser.h`). No new tokens. `ParsePostfix`
  checks for `..+` (`T_DOTDOT` followed by `T_PLUS`) inside `[...]` to set
  `SliceExpr::is_window = true`. `ParseTopLevel` handles
  `extern "<header.h>" [target "..."]`, blocks
  `{ fn ...; const ...; type ...; }`, and `const` in parameter lists.
* Type resolution and cloning (`resolve.h`, `clone.h`, `type_queries.h`,
  `typecheck_types.h`). `ResolveTypeNames` copies `c_repr` and `c_header` from
  `SAlias` onto the resolved `TypeExpr`, and `CloneType`, `Subst`, and
  `ConstOf` preserve them.
* Checker (`typecheck.h`, `typecheck_exprs.h`, `typecheck_flow.h`,
  `typecheck_calls.h`, `typecheck_builtins.h`). `CheckSlice` and `CheckLValue`
  check `s[lo..+N]` as an `A_FIXED` lvalue `T[N]`. `ResolveCall` validates
  `const` arguments and records `TargetEdge`s; `GetOrCreateSpec` keys
  specializations on `const_args`; `CheckSpecBodyOnce` binds `const`
  parameters in the cloned body. `CheckIf` runs `EvalStaticTargetCond` to
  prune dead `target_has` branches and pushes guarded ISA features onto
  `cur.guarded_targets` (`ActiveGuardedTargets` inspects only
  `RealFrameIndex()`'s `BodyState`). `PropagateTargetAttrs` propagates
  unguarded `target_attr` sets across `ast.fnspecs`.
* BCE and optimizer (`bce.h`, `optimize.h`). `ProveBounds` elides `gs_bc_win`
  on proven slice windows. `Call::Opt` folds `B_TARGET_HAS` when
  `target_const >= 0`, and `Optimizer::TryInline` refuses to inline across
  differing `target_attr` strings.
* Codegen and runtime (`codegen.h`, `codegen_types.h`, `codegen_exprs` /
  `codegen_values.h`, `codegen_calls.h`, `codegen_builtins.h`,
  `codegen_emit.h`, `runtime/runtime.h`). `runtime.h` defines `gs_bc_win`,
  `GS_TARGET`, and `gs_cpu_has` (regenerated into `src/runtime_inline.h` via
  `goose --gen-runtime-header`). `CodeGen` collects live headers, emits
  `_Static_assert` and `gs_to_c_*`/`gs_from_c_*` helpers, lowers boundary
  arguments in `EmitExternCall`, and keeps `/* ---- includes ---- */` at the
  top of the file in `HoistAggregateDecls`.

## 9. Alternatives considered

* Opaque C types (`c_type(size = 16, align = 16)`) and raw C pointers
  (`c_ptr<T>`) in the Goose type system. Putting a 16-byte-aligned `__m128i`
  into Goose's type system conflicts with C.2, where every struct and
  activation frame is emitted under `#pragma pack(push, 1)` (`TypeAlign == 1`).
  Any `m128i` stored in a Goose struct, array, or frame slot risked an
  unaligned `movdqa` fault unless every aggregate containing it was
  special-cased, and an opaque `TY_CTYPE` could not be initialized with
  `[0; 16]`, indexed with `v[i]`, or compared with `==`. Mapping
  `type m128i = u8[16] = "__m128i"` at the call boundary keeps `m128i` a
  `u8[16]` inside Goose and uses `memcpy` at the call site.
* Special-casing `s[a..a+16]` only inside `extern` call arguments
  (`SameIndexExpr` + `Val::slicenode`). This required a field in `Val` to
  carry the `SliceExpr` past decay, skipped bounds checks when the two
  endpoints were not recognized by `SameIndexExpr`, and still required
  declaring `_mm_loadu_si128` and `_mm_storeu_si128` in each SIMD file. Making
  `s[lo..+N]` a `T[N]` lvalue works everywhere in Goose and removes load/store
  intrinsics.
* Expanding `const` parameters in `Optimizer::TryInline` instead of
  `TypeCheck`. Relying on the optimizer meant a function with a `const`
  parameter failed whenever `TryInline` declined to inline it (recursion,
  multiple blocks, or `target_attr` boundaries), and prevented using a `const`
  parameter inside an array size `u8[N]` or slice window `s[i..+N]`.
  Specializing in `GetOrCreateSpec` handles all of those cases.
* Explicit `@target("...")` attributes and a `when` statement. `@` added a
  sigil to the lexer, `when` added a second conditional syntax beside `if`,
  and manual `@target` annotations on helper functions repeated what the
  `extern "<header.h>" target "..."` block already declared while breaking
  whenever a helper omitted an annotation or was inlined into a baseline
  caller. `target_has("...")` inside `if` together with `target_attr`
  propagation along unguarded call edges avoids annotations on Goose
  functions.

## 10. Testing

`test/extern_headers.py` covers the feature in both `goose run` (JIT) and
`goose build` (AOT):

* `test_slice_windows`: reading `s[lo..+N]` and `s[..+N]` by value, mutating
  `s[lo..+N] = [...]` in place, passing `s[lo..+N]` by `const T[N]&`,
  `T[N]&`, and `T[:]`, runtime bounds panics on negative `lo`, `lo + N > len`,
  and `INT64_MAX` overflow (`gs_bc_win`), compile-time rejection when `N <= 0`
  or `N` exceeds a static array's capacity, and BCE elision of `gs_bc_win` in
  `while i + 16 <= s.len` and `let limit = s.len - 16; while i <= limit`
  loops.
* `test_header_externs_and_const_params`: `<string.h>` (`memcmp`, `memset`,
  `strlen`), `<emmintrin.h>` target `"sse2"`
  (`type m128i = u8[16] = "__m128i"`, `mm_set1_epi8`, `mm_xor_si128`,
  `mm_cmpeq_epi8`, `mm_movemask_epi8`, `mm_slli_si128` with `const imm8: i32`),
  `<xmmintrin.h>` `extern const MM_HINT_T0` forwarded through a Goose helper
  with a `const` parameter, compile-time rejection of non-constant arguments
  or out-of-range constants for `const` parameters, compile-time rejection of
  passing a `let` local to a mutable `T&` extern parameter, and header
  omission when a block is unused.
* `test_target_has_and_propagation`: `if target_has("x86_64,sse2,aes,pclmul")`
  and `else if target_has("aarch64,neon")` pruning `<arm_neon.h>` and
  `veorq_u8` on x86-64, `GS_TARGET("sse2,aes,pclmul")` propagation through two
  levels of unannotated helper functions (`aes_pclmul_helper` and
  `aes_pclmul_outer_helper`), inliner guard keeping `GS_TARGET` off
  `dispatch_block` and `main_g`, and compile-time errors on unknown or
  mismatched target features (`target_has("avx999")`,
  `target_has("aarch64,avx2")`).

## 11. Deferred and open

* Cross-architecture intrinsic emulation. A program can guard x86-64 and
  AArch64 paths with `if target_has(...)`, and each target compiles only its
  own branch; translating one architecture's intrinsics into the other's is
  left to libraries.
* Function-level multi-versioning (`target_clones`). Today a caller dispatches
  with `if target_has(...)` to a helper whose `target_attr` is inferred from
  the intrinsics it calls. Compiling one Goose function body multiple times
  for several ISA levels without separate helpers could build on
  `FnSpec::target_attr` if needed later.
