# The Goose Standard Library — Design

<details>
<summary>Table of contents</summary>

- [0. Summary](#0-summary)
- [1. Principles](#1-principles)
- [2. Conventions](#2-conventions)
- [3. Module map](#3-module-map)
- [4. `std`](#4-std)
- [5. `dictionary`](#5-dictionary)
- [6. `vec`](#6-vec)
- [7. `math` and `os` (extern-backed)](#7-math-and-os-extern-backed)
- [8. Builtins and language rules the library needed](#8-builtins-and-language-rules-the-library-needed)
- [9. Deliberately not included](#9-deliberately-not-included)

</details>

This document records the original standard library design: its scope,
conventions, and the builtins and language rules it required (§8). It
preserves the reasoning at the time. Later changes include `const` types,
namespaces, generic aliases, serialization, graphics, physics, user
interfaces, directories and atomic replacement in `os`, and the `base64`,
`csv`, `json` and `regex` modules; some rules and limitations below have
therefore been superseded. See `../stdlib.md` for the current API and
`../goose_spec.md` for current language rules. References of the form §N
refer to that specification.

---

## 0. Summary

The original standard library comprised five Goose source files (`std`,
`math`, `dictionary`, `vec`, `os`) with about 120 functions in total. It
introduced three families of named types: `dictionary<K, V>` (the only
facility the language has no building block for), `rng` (one word of PRNG
state), and the `vec2/3/4<T>` math vectors that §6.1 already promises.
Everything else is a function over the user's own arrays, structs and
pools.

Everything is written in Goose except:

* the builtins of the `print`/`str`/`format` family, `default<T>()`,
  `abort(msg)`, `exit(code)`, and `copy(x)` from the passing rule of §8.7;
  and
* the `extern fn` declaration form (spec §7.10), through which the `math`
  and `os` modules reach libm and a thin C runtime layer without a builtin
  per function.

The Goose library code uses no heap allocator: every temporary is a local resizable or a
limited array, every result is constructed in its destination (§4.3), and
every higher-order function compiles to a loop (§7.6).

---

## 1. Principles

**Functions over types.** Goose's strength is the array family: eight kinds of
array, pools, relative references, variable-mode ADTs, all named and laid out
by the user. The library should operate on these user-defined containers
rather than require wrappers of its own. A library function takes a slice
(`T[:]`) or a reference to whatever array the caller has, never a
library-defined wrapper. It returns either a scalar, a reference into the
input, or a fresh array built at the caller's destination. New nominal types
are introduced only where the language has no representation to build on (a
hash table) or the spec already asks for them (vectors).

**Small.** The library provides one function per job, with a call that covers
common uses. There are no `_if`/`_by_key`/`_reverse`/`_indices` families. A
function is included only when the usual hand-written loop takes more than three
lines or is easy to get wrong, as with sorting, hashing, UTF-8, float printing,
and binary search. §9 lists omitted functions and the one-line loops that
replace them.

**Goose first, builtins by exception.** A builtin is justified only when it
provides an operation the language cannot express: custom typechecking
(`print` of any type, `default<T>()`), a diverging call the checker must
know about (`abort`), or a way out to the OS. Speed alone is not a reason
yet, since the library's Goose code is expected to compile to the same loops
a C programmer would write. Where measurement later shows that a library
function would benefit from custom codegen (a word-at-a-time string hash,
`memchr` for byte search), that function moves into the builtin table
without changing its signature. The `extern fn` mechanism (§8.4) keeps that
option open without growing the typechecker.

**Make costs explicit.** The library does no hidden allocation and makes no
hidden copies. The conventions in §2 are chosen so that the cheap call is
the natural one to write and the expensive one cannot be written by
accident.

**Lobster's shape, Goose's cost model.** From Lobster's `std.lobster`, we take
an API organized around functions, `it` blocks, `find`/`filter`/`fold`/`map`,
and practical helpers such as `insert_ordered`. Results are built in place
instead of allocating a new vector for each call. Broad builtins such as
`tokenize` and `concat_string` are replaced by smaller functions that can be
combined. C++ and Rust guide the list of operations to cover: sort, stable sort,
binary search, heap, dedup, retain, hash map, and string search. Goose
implements these without their iterator protocols, trait bounds, or wrapper
types. It uses `T?` references, multiple returns, and `return from` in place of
`Option`, `Result`, and exceptions.

---

## 2. Conventions

The syntax at call sites follows the passing rule of spec §4.1, whose design
is recorded in §8.7: no `&` is written at a reference-typed destination.

### 2.1 How containers are passed

| The function… | Parameter | Call sites | Works on |
|---|---|---|---|
| reads, or mutates elements in place without changing the length (`sort`, `reverse`, `fill`, `find`, `sum`, …) | `xs: T[:]` | `f(arr)`, `f(arr[1..])`, `f(slice)` | every array kind and slices |
| changes the length (`push_n`, `insert_at`, `remove_at`, `retain`, `heap_push`, …) | `xs: A&`, where `A` is a generic bound to the whole array type | `f(arr)` | every array kind that has the operations used |
| takes a library type (`dictionary`, `rng`) | `d: dictionary<K, V>&` | `d.insert(k, v)` | — |

Rationale, and what was ruled out:

* Arrays coerce to slice parameters without a copy (§3.10), so `sort(arr)`
  has no extra cost and reads naturally. Writes through the slice are legal
  when the array is writable (§9.5), so in-place algorithms need no second
  form. Passing a `let` array to `sort` is a compile error that reports the
  call chain, which is the intended behaviour.
* A generic reference parameter `xs: A&` binds `A` to the whole array type
  and is instantiated separately for each array kind: one
  `push_n<A, T>(xs: A&, v: T, n: i64)` body serves a `[>..<]`, a `[>..]` and
  a `[..k]` field. It cannot accept a slice, so it is reserved for
  length-changing operations, where a slice would be meaningless anyway.
* An untyped parameter (`fn f(xs)`) binds the argument's exact type, which
  includes a reference type for `f(&arr)`. By §8.7's rule, a non-fixed
  lvalue argument reaches an untyped parameter by reference and a fixed
  lvalue argument reaches it by value, so `fn total(xs)` works for every
  array kind. The library still prefers `T[:]` for read-only functions
  because it names the element type (`find -> T?`, `contains(xs, v: T)`)
  and accepts sub-ranges.
* Now that grow-shrink arrays `[>..<]` can be sliced (§8.7), they are passed
  to the functions of the first row like any other array. Shrinking such an
  array while a slice of it is in scope is an error the checker reports, not
  something the library has to handle.
* No non-fixed value is ever taken by value. The language enforces this
  (spec §4.1), so a library function that wants its own copy writes
  `copy(x)`.

### 2.2 Results

* **A fresh array** (`filter`, `map`, `split`, `concat`, `str`) is returned as
  `T[>..]`: a resizable local built by NRVO directly into the caller's
  destination (§7.3). So for `let ys = xs.filter() { it > 0 }`,
  `var ys = xs.filter() { … }` (and keep pushing), `out.append(xs.map() {
  … })` and `words.push(str("w", i))`, the only work done is building the
  elements. The result type is `T[>..]` rather than `T[]` because it has no
  length-field limit and the caller most often wants to keep growing or
  sorting it. The "fresh stack" it needs is a stack index for the value's
  lifetime, not a runtime cost. A `T[]` destination converts.
* **"Maybe an element"** (`find`, `get`, `last`) is a `T?`: an optional
  reference into the input, rooted where the input is rooted, and null for
  "none". The caller narrows with `if`/`guard` (§3.8) and can write through
  it. There is no `Option<T>` value type and the library does not invent one.
* **A position** (`find_index`, `index_of`, `min_by`, `lower_bound`) is an
  `i64`, with `-1` for "none" where that can happen.
* **Two-outcome scalars** (`parse_int`, `binary_search`, `read_line`) use the
  spec's convention of a trailing `bool` in a multiple return: `let v, ok =
  parse_int(s);`. There is no error type; programming errors `abort`.
* **Text output** goes into a builder that the caller passes in
  (`format(out, …)`, `read_file(path, out)`), so the result is appended to
  whatever the caller is assembling. The only exception is `str(…)`, which
  exists to produce a fresh string.

### 2.3 Function-value parameters

* Function values are trailing blocks (§7.6): `xs.find() { it.key == k }`,
  `xs.sort() { a, b => a.age < b.age }`, or a named function.
* A comparator is always a strict "a goes before b" predicate `lt(a, b)`.
  Sorting, heap and extremum functions come in two arities: with a block,
  and without a block, which means `a < b` (so `sort(xs)` works for numbers
  and `sort(xs) { a, b => compare(a, b) < 0 }` for strings). Overload
  resolution tells the two apart by whether a function value is supplied;
  this was verified.
* A *key* function returns a value (`min_by(xs) { it.score }`).
* A block parameter binds a **reference** when the library passes it `&x`,
  so blocks can mutate: `d.each() { k, v => v += 1; }` works because `each`
  calls `F(s.key, &s.val)`. Functions document which arguments they pass by
  reference.
* Inside a block, `return` returns from the *enclosing named function*
  (§7.6), so callers of `each_*` functions can exit early without any extra
  mechanism.

### 2.4 Trait-like overload sets

Goose's overloading-by-type is its trait mechanism. The library defines a few
names that user code is expected to extend by adding overloads for its own
types:

| Set | Provided for | Users add | Consumed by |
|---|---|---|---|
| `hash(x) -> u64` | every scalar type, `bool`, `u8[:]` | `fn hash(k: mykey) -> u64` | `dictionary` |
| `compare(a, b) -> i64` | `u8[:]` | `fn compare(a: ver, b: ver) -> i64` | sorting strings |
| `format(out: u8[>..]&, x)` | every type, structurally, by the builtin (§8.1) | `fn format(out: u8[>..]&, p: point)` | `print`/`str`/`format`, which use a user overload for its type wherever that type occurs |

Two resolution facts (§7.1) shape how these are written:

* A concrete overload is preferred over a generic one (`hash(k: mykey)` is
  chosen over `hash<T>(x: T)`), so user overloads take precedence
  automatically.
* Every integer width needs its **own** overload: a `u8` argument widens
  equally well into `i64` and `u64`, and two coercion-tier matches are an
  ambiguity error. So `hash` is ten one-line functions, not one generic.

### 2.5 UFCS

`x.f(args)` is `f(x, args)` (§7.1). By the passing rule (§8.7), `x.f()`
binds `x` by reference exactly when `f`'s first parameter is a reference
type, for the same reason that a free call `f(x)` does. So `d.insert(k, v)`,
`q.heap_push(v)` and `r.rand_int(6)` all bind their receivers as their
signatures specify. An earlier idea, a per-function marker for by-reference
receivers, was withdrawn for two reasons. It would have taught callers to
drop `&` everywhere, which brings back the accidental copy as soon as
someone forgets to declare the marker. And it made the call form depend on
knowing how each function was declared.

A reference-returning call is not an assignable location
(`get_or_insert(d, k, 0) += 1` is rejected); binding it needs the annotated
form `let c: i64& = d.get_or_insert(k, 0);` (§3.8). The `dictionary` API
sidesteps this with the block-taking `update` (§5.2).

### 2.6 Naming

* Library types are lowercase, like the builtin types: `dictionary<K, V>`,
  `rng`, `vec3<T>`, `float3`. Heavy types get long names.
* `each_*`: iteration higher-order functions (`each_rev`, `each_split`,
  `each_utf8`, `each_chunk`).
* `*_by`: the key-function variant of a value function (`min`/`min_by`).
* `*_at`: index-addressed array mutation (`insert_at`, `remove_at`), keeping
  the bare verbs (`insert`, `remove`) for `dictionary`. Two generic
  functions of the same name and arity whose first parameters are `A&` and
  `dictionary<K, V>&` would be ambiguous (both bind at the generic tier),
  so the array and dictionary vocabularies are kept disjoint.
* `format_*`: append a textual form to a `u8[>..]` builder with formatting
  control; `parse_*`: the inverse; `to_*`: in-place case conversion;
  `write_*`: I/O only.
* No `is_` prefix (`any`, `all`, `contains`, `starts_with`).
* There is no namespacing in v1 (§11.1), so every library name is global.
  Names were chosen to be the ones a user would also pick. The reasoning is
  that a user who defines their own `find` with a concrete signature gets
  their own version (an exact match is preferred over a generic one), and a
  user who defines a generic `find` gets a clear duplicate or ambiguity
  error rather than silent shadowing.

### 2.7 Strings

There is no string type (§3.7); the library's string functions are functions
on `u8` arrays with these roles:

| Role | Type | Notes |
|---|---|---|
| input | `u8[:]` | literals, any `u8` array and any slice coerce |
| output being built | `u8[>..]&` | the "builder"; `push`/`append`/`format` are the primitives |
| owned, stored | `u8[]`, `u8[varint]`, `u8[..k]` | fields and elements; a `u8[>..]` result can be stored in them |

All functions are byte-oriented and ASCII by default (`to_lower`, `trim`);
`each_utf8`/`push_utf8` are the only encoding-aware functions. Comparison is
`==` (structural, §4.5) and `compare`. A character literal is an integer
constant (§2): `format(out, 'x')` appends `120`, and a byte is appended with
`out.push('x')` or `format(out, "x")`.

---

## 3. Module map

| File | Imports | Contents | Leans on (§8) |
|---|---|---|---|
| `stdlib/std.goose` | — | scalars, bits, hashing, `rng`; array HOFs, searching, sorting, heaps, resizing; strings, formatting control, parsing, UTF-8 | `default<T>()` for `sum`; `abort` |
| `stdlib/math.goose` | — | libm as `extern fn`, `PI`/`TAU`, `radians`/`degrees`, `is_nan`/`is_inf` | `extern fn` |
| `stdlib/dictionary.goose` | `std` | `dictionary<K, V>` and its functions | `default<T>()` |
| `stdlib/vec.goose` | `std`, `math` | `vec2/3/4<T>`, `float*`, `double*`, `int*`, vector functions | — |
| `stdlib/os.goose` | `std` | files, stdio, arguments, environment, time, sleep, entropy | `extern fn`, runtime C in `src/runtime/runtime_os.h` |

`import std;` must be written explicitly; nothing is imported
automatically. The compiler resolves `import std;` by looking in three
places, in order. First it looks in the importing file's package root (spec
§11.1: the main file's directory for the program's own files, the library's
directory for a library module). Next it looks in `--stdlib <dir>` or
`GOOSE_STDLIB`, if given. Finally it walks up from its own executable's
directory, looking for `stdlib/std.goose` (three levels). This way
`build/Debug/goose.exe`, `build/goose`, `bin/goose.exe` and an installed
`<prefix>/bin/goose` beside `<prefix>/stdlib` all find it, without
requiring a single installation path. Unused library functions cost only
parse time: functions are typechecked in call-graph order from `main`, so
an unreferenced generic is never instantiated.

---

## 4. `std`

Signatures are written in documentation form. Where a function's return type
is the element type of an `A&` parameter, the source omits the annotation (it
is inferred from the body, §7.1) and the documentation writes `-> T` with
"T = the element type". `xs: T[:]` accepts every sliceable array (§2.1).

### 4.1 Scalars and bits

```goose
fn min<T>(a: T, b: T) -> T
fn max<T>(a: T, b: T) -> T
fn clamp<T>(x: T, lo: T, hi: T) -> T
fn abs<T>(x: T) -> T            // integers; plus abs(f32) and abs(f64) overloads
fn rem<T>(a: T, b: T) -> T         // integers; pairs with `/`
fn div_euclid<T>(a: T, b: T) -> T  // integers; pairs with `%`
fn sign<T>(x: T) -> T           // -1, 0, 1 at x's type; f32/f64 overloads
fn lerp(a: f64, b: f64, t: f64) -> f64      // and an f32 overload
fn next_pow2(x: i64) -> i64     // smallest power of two >= max(x, 1)
fn popcount(x: u64) -> i64
fn clz(x: u64) -> i64           // 64 for x == 0
fn ctz(x: u64) -> i64           // 64 for x == 0
fn swap<T>(a: T&, b: T&)
```

`rem` and `div_euclid` supply the missing partners of truncating `/` and
Euclidean `%` without changing either operator. They compute at the
operands' integer type, using the truncating quotient and its remainder,
whose product and subtraction fit whenever the quotient does. Forming
`a - a % b` instead can underflow. `rem` handles divisor -1 before division,
since its zero result fits even when the quotient does not. The test for a
negative value with all bits set accepts unsigned types too and requires
integer operands. `div_euclid`
uses `/` for its initial quotient, preserving its overflow checks.

`abs` and `sign` have explicit float overloads. `popcount`/`clz`/`ctz` use
the runtime's `extern` intrinsics (§8.4).

### 4.2 Hashing

```goose
fn hash(x: i8) -> u64        // … one overload per integer type, i8 … u64
fn hash(x: bool) -> u64
fn hash(x: f32) -> u64
fn hash(x: f64) -> u64
fn hash(s: u8[:]) -> u64     // 8 bytes per multiply; any u8 array coerces
fn hash_combine(seed: u64, h: u64) -> u64   // for composite keys
```

The integer hashes are a multiply-xorshift mix (Fibonacci hashing), so the
low bits are well distributed and `dictionary` can mask rather than divide.
The byte hash reads 8 bytes at a time, with the length as the seed. Each
word but the last gets a multiply and a rotate. The last word is the final
8 bytes (or, for inputs shorter than 8 bytes, the bytes gathered into one
word), and it gets two multiply-xorshift rounds, so that every input bit
reaches the low 32 bits that `dictionary` masks and keeps as slot tags.
Otherwise, keys that differ only in their last characters (`item_17`,
`item_18`) would share their low bits.

Each word's round has to wait for
the previous word's multiply. So for keys longer than 24 bytes, the words
before the last 24 bytes or fewer are split over two lanes (the even and
the odd words of each 16 bytes), and a processor can overlap the two lanes'
rounds. The second lane is rotated and folded into the first before the
remaining words. This two-lane form is a separate function (`hash_lanes`),
called from an early return, which the optimizer keeps out of line. That
keeps the code for short keys small enough for the C compiler to inline
into a dictionary's lookups.

A user key type defines `fn hash(k: key) -> u64 { hash_combine(hash(k.a),
hash(k.b)) }`. Fixed-capacity inline strings (`u8[..16]`) hash through the
slice overload, which is what makes them usable as `dictionary` keys.

### 4.3 Random numbers

```goose
struct rng { s: u64 }                    // splitmix64 state; the seed is the state
fn rand_u64(r: rng&) -> u64
fn rand_int(r: rng&, n: i64) -> i64      // uniform in [0, n)
fn rand_flt(r: rng&) -> f64              // uniform in [0, 1)
fn shuffle<T>(xs: T[:], r: rng&)         // Fisher-Yates
```

Create a generator with `var r = rng { 12345 };`, then call
`r.rand_int(6)`. splitmix64 has one word of state, uses only 64-bit
arithmetic, and passes the usual test batteries; the benchmarks keep their
own xorshift. To seed from the OS, use `os`'s `random_seed()`.

### 4.4 Arrays: reading and searching

```goose
fn each_rev<T, F>(xs: T[:])                    // F(x), last to first
fn each_chunk<T, F>(xs: T[:], n: i64)          // F(chunk: T[:], i); the last chunk may be short
fn find<T, F>(xs: T[:]) -> T?                  // first x with F(x), as a reference; null if none
fn find_index<T, F>(xs: T[:]) -> i64           // …or its index, -1 if none
fn position<T>(xs: T[:], v: T) -> i64          // first i with xs[i] == v, -1 if none
fn contains<T>(xs: T[:], v: T) -> bool
fn any<T, F>(xs: T[:]) -> bool
fn all<T, F>(xs: T[:]) -> bool
fn count<T, F>(xs: T[:]) -> i64                // number of x with F(x)
fn min<T>(xs: T[:]) -> T                       // asserts xs.len > 0; F(a, b) forms too
fn max<T>(xs: T[:]) -> T
fn min_by<T, F>(xs: T[:]) -> i64               // index of the smallest F(x); -1 if empty
fn max_by<T, F>(xs: T[:]) -> i64
fn last<T>(xs: T[:]) -> T&                     // asserts xs.len > 0
fn lower_bound<T, F>(xs: T[:]) -> i64          // first i with !F(xs[i]); F must be true on a prefix
fn binary_search<T>(xs: T[:], v: T) -> i64, bool   // sorted by <; position (or insertion point), found
fn find<T>(xs: T[:], sub: T[:]) -> i64         // first occurrence of a subsequence, -1 if none
fn rfind<T>(xs: T[:], sub: T[:]) -> i64
fn starts_with<T>(xs: T[:], p: T[:]) -> bool
fn ends_with<T>(xs: T[:], p: T[:]) -> bool
```

`each_chunk` exists because row slices are the documented way to get bounds
checks out of 2-D kernels (`bench/goose/blur_rows.goose`); `for y in h {
each_chunk(img, w) … }` is that idiom with the arithmetic done once.
`find`/`rfind`/`starts_with`/`ends_with` are generic over the element type,
so they also serve as the string functions (`find(line, "://")`). They
coexist with the predicate `find` because a function-value argument and a
slice argument never match the same overload.

### 4.5 Arrays: transforming

```goose
fn map<T, F>(xs: T[:]) -> U[>..]                 // U = F's result type (§8.5)
fn filter<T, F>(xs: T[:]) -> T[>..]
fn fold<T, A, F>(xs: T[:], acc: A) -> A          // acc = F(acc, x) for each x
fn sum<T>(xs: T[:]) -> T                         // fold from default<T>()
fn concat<T>(a: T[:], b: T[:]) -> T[>..]
```

`map`'s body is `var out = []; for x in xs { out.push(F(x)); } return out;`,
which uses the empty-array declaration of §8.5. `sum` needs `default<T>()`;
`fold(xs, 0.0) { a, x => a + x }` is a form that does not need it.

### 4.6 Arrays: in place

```goose
fn fill<T>(xs: T[:], v: T)
fn copy_into<T>(dst: T[:], src: T[:])            // asserts equal lengths; memmove semantics
fn reverse<T>(xs: T[:])
fn sort<T>(xs: T[:])                             // by <
fn sort<T, F>(xs: T[:])                          // by F(a, b); unstable, in place, no allocation
fn stable_sort<T>(xs: T[:])
fn stable_sort<T, F>(xs: T[:])                   // natural merge sort; one temporary of xs.len elements
fn to_lower(s: u8[:])                            // ASCII, in place
fn to_upper(s: u8[:])
```

`sort` is a pattern-defeating quicksort modelled on pdqsort and Rust's
ipnsort. A first pass finds where the run at the start of the input ends;
if that run is the whole input, the sort is done. Otherwise each round
takes a median pivot (the median of three elements, or, from 128 elements
on, the median of three medians of three). It partitions with a branchless
Lomuto loop (every element is swapped, and the boundary advances by the
comparison's outcome). It then pushes the larger side onto an `i64[..144]`
stack of (lo, hi, budget) ranges and continues with the smaller side. So
the sort never recurses, allocates nothing, and holds at most log₂(2⁴⁸)
ranges.

Everything to the left of a range sorts before or equal to
everything in it. So when the pivot does not sort after the element just
before the range, one pass moves the elements equal to the pivot to the
front and drops them from the range, as pdqsort does. As a result, many
equal keys take only linear passes.

Each range carries a budget of
2·log₂(n) rounds. When the budget runs out, heapsort finishes the range,
which bounds the worst case at O(n log n). `test/stdlib/stdlib_sort.goose`
checks this by using McIlroy's adversary to build an input that targets the
pivot choice. Ranges of ten elements or fewer are insertion sorted: at
those sizes, the branchy insertion sort measured faster than further
partitioning.

One input shape is slower than it was with the Hoare partition that `sort`
used before: input that is in order except for a few misplaced elements.
The branchless partition swaps every element, which rotates each side of a
sorted range by one. So nothing below the first run check sees any order to
exploit, and such input takes about two thirds of the time of random input.
The old quicksort's well-predicted scans took half the new time on it (200k
elements: 1.6 ms against 0.8 ms; random input 2.5 ms against 8.3 ms).

`stable_sort` is a natural merge sort. One pass splits the input into runs
that are in order: it reverses strictly descending runs and extends runs
shorter than 16 by insertion sort. If the input is a single run, the sort
ends there. The ends of the runs are stored in an `i64[>..]`. Each pass then
merges neighboring pairs of runs between `xs` and a `T[>..]` temporary on a
fresh data stack (a stack index, not an allocation). The passes alternate
direction, so that no pass has to copy back.

A merge of two runs that are
already in order just copies them. Otherwise each merge step either
branches on the comparison or uses conditional moves to select the element
and the side. A merge branches when the previous merge switched between its
runs on fewer than one step in four. This is because a branch on random
input mispredicts about every other step, while on structured input a
predicted branch lets the comparisons overlap. That matters most for a
comparator that loads what it compares (BWT's rank pairs: 3.7x between the
two forms on its unsorted rounds, against 1.5x the other way on random
integers).

Both sorts leave a permutation of the input, whatever the comparator
returns. Every index stays in range however the comparisons come out, so a
comparator that is not a strict weak ordering only results in an
unspecified order.

### 4.7 Arrays: changing the length

```goose
fn push_n<A, T>(xs: A&, v: T, n: i64)            // n pushes; grow-only arrays' answer to resize
fn insert_at<A, T>(xs: A&, i: i64, v: T)         // shifts [i..) up; needs push
fn remove_at<A>(xs: A&, i: i64) -> T             // shifts down; needs pop; returns the element
fn swap_remove<A>(xs: A&, i: i64) -> T           // O(1), reorders
fn retain<A, F>(xs: A&)                          // keeps x with F(x), stable; needs resize
fn dedup<A>(xs: A&)                              // drops adjacent duplicates (sort first for all)
fn heap_push<A, T>(xs: A&, v: T)                 // min-heap by <; F(a, b) forms of all three exist
fn heap_pop<A>(xs: A&) -> T
fn make_heap<A>(xs: A&)                          // O(n) heapify
```

Each instantiates only for array kinds that have the operations it uses
(a grow-only array pops through the reference wherever nothing in the
caller points into it, spec §5.1). A `[>..<]` is the expected heap and
queue container; a limited `[..k]` inside a struct works too.

### 4.8 Strings

```goose
fn format_int(out: u8[>..]&, v: i64, base: i64, width: i64, pad: u8)   // base 2..36, right-aligned
fn format_uint(out: u8[>..]&, v: u64, base: i64, width: i64, pad: u8)
fn format_flt(out: u8[>..]&, v: f64, decimals: i64)   // fixed decimals
fn parse_int(s: u8[:]) -> i64, bool              // optional sign, base 10, whole string
fn parse_int(s: u8[:], base: i64) -> i64, bool
fn parse_flt(s: u8[:]) -> f64, bool              // sign, fraction, exponent; correctly rounded
fn compare(a: u8[:], b: u8[:]) -> i64            // bytewise lexicographic: -1, 0, 1
fn trim(s: u8[:]) -> u8[:]                       // ASCII whitespace; a sub-slice, no copy
fn trim_start(s: u8[:]) -> u8[:]
fn trim_end(s: u8[:]) -> u8[:]
fn split(s: u8[:], sep: u8) -> u8[:][>..]        // slices into s; empty parts kept
fn split(s: u8[:], sep: u8[:]) -> u8[:][>..]     // a separator of several bytes
fn each_split<F>(s: u8[:], sep: u8)              // F(part: u8[:]); no array built
fn each_split<F>(s: u8[:], sep: u8[:])
fn words(s: u8[:]) -> u8[:][>..]                 // the runs between whitespace; none empty
fn each_word<F>(s: u8[:])                        // F(word: u8[:])
fn join<T>(out: u8[>..]&, parts: T[:], sep: u8[:])   // T any u8 array/slice type
fn format_replaced(out: u8[>..]&, s: u8[:], old: u8[:], with: u8[:])   // `from` is a keyword
fn each_utf8<F>(s: u8[:])                        // F(cp: i64); malformed bytes yield 0xFFFD
fn push_utf8(out: u8[>..]&, cp: i64)             // the UTF-8 bytes of a code point
```

Plain rendering of scalars and strings is the builtin `format` (§8.1); the
functions here add formatting control and parsing. `split`, which returns
slices into its input (no copies, with elements rooted at the caller's
text), is the Goose replacement for Lobster's `tokenize`; the `each_split`
form does not build an array at all. `words` is the whitespace tokenizer a
command line needs, and `split` at `' '` is not: `words` drops the empty
parts that repeated spaces and tabs would leave.

---

## 5. `dictionary`

### 5.1 Representation

```goose
struct dictionary_slot<K, V> { key: K, val: V, tag: u32 }
struct dictionary<K, V> { count: i64 = 0, slots: dictionary_slot<K, V>[>..<] = [] }
```

* The table uses open addressing, linear probing, a power-of-two capacity
  and `hash(key) & mask`. `K` and `V` must be fixed-size (the slot array is
  indexed randomly, §3.3); strings are keyed as `u8[:]` slices into text
  the caller owns, or as inline `u8[..k]`. Deletion is backward-shift (the
  technique `bench/goose/lru.goose` uses), so there are no tombstones and no
  periodic cleaning.
* A `dictionary` is resizable-class (its tail is a `[>..<]`), so it lives
  where resizables live: a local, a global, a by-reference parameter, the
  tail of a struct. It generalizes spec A.3.
* Each slot keeps a tag: 0 when it is unused, else the low 31 bits of the
  key's hash with bit 31 set. A probe compares keys only where the tags
  match (for a slice key, its length and bytes; for an inline key, its
  bytes). For a table of up to 2^31 slots the tag holds the home slot, so
  growth and removal never hash a key again; a larger table hashes the keys
  it moves. The tag costs 3 bytes per slot more than a `used` byte (12
  bytes for a `u32 → i32` table where 9 would do). N-gram counting with
  `u32` keys, which compare as cheaply as tags, measured between 3% slower
  and the same speed, while string-keyed tables (word counts, an LRU cache,
  an interpreter's variables, a template engine's) gained 18-57% from the
  dictionary changes alone.
* The table doubles in place at ⅔ load. The slot array is at the top of its
  stack, so `resize` extends it with unused slots. The entries are then
  re-placed by a single walk over the old slots in cyclic order, starting
  just after an unused slot. Every entry ends up within a run of slots the
  walk has already passed (its new home is its old home, or that plus a
  multiple of the old size), so no entry that is still to be moved lies on
  the probe path of an entry that has been moved. No second table is built
  and nothing is copied back. The slot array is grow-shrink so that it can
  be resized through a reference (§5.2). A maximum load of ½ measured 16%
  faster on a small LRU table and 9% faster on n-gram counting, but 8%
  slower on growth-heavy word counting and up to 1.8 times slower on
  150K-entry tables, which no longer fit in the cache at twice the size. A
  maximum load of ¾ was within noise of ⅔, except for the word count, where
  it was 8% slower.
* `insert`, `update` and `get_or_insert` check for growth first and then
  walk the probe sequence once, to the key or to the unused slot where it
  goes.
* Fresh slots are filled with `default<K>()`/`default<V>()` (§8.2), so a
  reference value is an optional one (`Cell?`, or `in pool`). An update
  stores a whole slot, since `=` into a reference `V` would write its
  pointee (spec §3.8).

### 5.2 API

```goose
fn get<K, V>(d: dictionary<K, V>&, key: K) -> V?              // reference to the value, or null
fn contains<K, V>(d: dictionary<K, V>&, key: K) -> bool
fn insert<K, V>(d: dictionary<K, V>&, key: K, val: V) -> bool  // overwrites; true if the key was new
fn get_or_insert<K, V>(d: dictionary<K, V>&, key: K, v0: V) -> V&
fn update<K, V, F>(d: dictionary<K, V>&, key: K, v0: V)        // F(val&): inserts v0 if absent, then calls F
fn remove<K, V>(d: dictionary<K, V>&, key: K) -> bool
fn clear<K, V>(d: dictionary<K, V>&)
fn reserve<K, V>(d: dictionary<K, V>&, n: i64)                 // capacity for n entries without rehash
fn each<K, V, F>(d: dictionary<K, V>&)                         // F(key, val&); order unspecified
```

Word counting, the canonical use (prototyped end to end):

```goose
var counts = dictionary<u8[:], i32> {};
each_split(text, ' ') { counts.update(it, 0) { it += 1; } };
counts.each() { w, n => if n > 100 { print(w, " ", n); } };
```

`update` exists because the natural form, `get_or_insert(d, k, 0) += 1`, is
not an assignable location, and the annotated binding needs the value type
written out (§2.5). A block receives the reference without either problem.

A reference returned by `get` or `get_or_insert`, or an `each` binder, may
not be in scope at the next `insert`, `update`, `remove` or `clear`. The
slot array is grow-shrink, so the checker reports this (spec §5.2) rather
than letting a rehash or a backward shift leave the reference pointing at a
different, well-typed slot. Bind such references in their own block, or use
`update`.

### 5.3 Sets and other idioms

* A set is `dictionary<K, bool>`: `s.insert(k, true)`, `s.contains(k)`. A
  dedicated `set<K>` needs generic type aliases (§8.7) and a zero-size value
  type, neither of which exists yet; saving the byte per slot is not worth a
  second implementation.
* Owned string keys: keep a `u8[>..]` text pool next to the
  `dictionary<u8[:], V>` (two locals of one scope, or a global pool); keys
  are slices into the pool. A struct cannot hold both (one resizable per
  struct, §3.4), which is spec TODO 8. An interner built on this idiom is a
  candidate for a later module.
* Insertion order: `dictionary` does not keep it. Where order matters, store
  values in the user's own `[>..]` and keep indices in the dictionary, which
  is what `lru.goose` does.

---

## 6. `vec`

```goose
struct vec2<T> { x: T, y: T }
struct vec3<T> { x: T, y: T, z: T }
struct vec4<T> { x: T, y: T, z: T, w: T }
type float2 = vec2<f32>;   type float3 = vec3<f32>;   type float4 = vec4<f32>;
type double2 = vec2<f64>;  type double3 = vec3<f64>;  type double4 = vec4<f64>;
type int2 = vec2<i32>;     type int3 = vec3<i32>;     type int4 = vec4<i32>;

fn dot<T>(a: vec2<T>, b: vec2<T>) -> T            // and vec3, vec4
fn cross<T>(a: vec3<T>, b: vec3<T>) -> vec3<T>
fn length_sq<T>(v: vec3<T>) -> T                  // all three sizes
fn length(v: float3) -> f32                       // f32 and f64 instances, all sizes
fn normalize(v: float3) -> float3
fn distance(a: float3, b: float3) -> f32
fn lerp<T>(a: vec3<T>, b: vec3<T>, t: T) -> vec3<T>
fn xy<T>(v: vec3<T>) -> vec2<T>                   // also xy(vec4), xyz(vec4)
let float3_0 = float3 { 0.0, 0.0, 0.0 };          // _0 and _1 for all nine aliases
let float3_1 = float3 { 1.0, 1.0, 1.0 };
```

Elementwise `+ - * /` are provided by the language (§6.1). The prototype
confirmed that a `type float3 = vec3<f32>` alias constructs positionally,
adds memberwise and instantiates generic `dot` for `f32` and `i64`. The
`int*` aliases are `i32` because vectors are data (§3.1) and an `i32`
component widens into an `i64` index implicitly. Elementwise
`min`/`max`/`abs` are deliberately absent: a
`min<T>(a: vec3<T>, b: vec3<T>)` overload is ambiguous with the scalar
`min<T>(a: T, b: T)` (both are at the generic tier), and `min(a.x, b.x)`
per component is what a user writes anyway. The lengthening constructors
(`vec3(v, z)`) were dropped: `vec3 { v.x, v.y, z }` is as short and avoids
a function named like a type.

---

## 7. `math` and `os` (extern-backed)

`math` is libm, declared once per function and float width:

```goose
extern fn sqrt(x: f64) -> f64;        extern fn sqrt(x: f32) -> f32 = "sqrtf";
// sin cos tan asin acos atan atan2 exp log log2 log10 pow floor ceil round trunc, likewise
let PI = 3.141592653589793;  let TAU = 6.283185307179586;
fn radians(deg: f64) -> f64
fn degrees(rad: f64) -> f64
fn is_nan(x: f64) -> bool             // x != x
fn is_inf(x: f64) -> bool
```

`floor`/`ceil`/`round`/`trunc` return floats, as in C; `as i64` converts.
Float `%` is already `fmod` (§6.1).

`os` wraps a limited set of C functions in `src/runtime/runtime_os.h`, enough to
write tools and benchmarks:

```goose
fn read_file(path: u8[:], out: u8[>..]&) -> bool     // appends the whole file
fn write_file(path: u8[:], data: u8[:]) -> bool
fn append_file(path: u8[:], data: u8[:]) -> bool
fn file_exists(path: u8[:]) -> bool
fn delete_file(path: u8[:]) -> bool
fn read_line(out: u8[>..]&) -> bool                  // stdin; strips the newline; false at EOF
fn read_stdin(out: u8[>..]&)                         // everything until EOF
fn write_stdout(s: u8[:])
fn write_stderr(s: u8[:])
fn flush_stdout()
fn args() -> u8[:][>..]                              // slices of a global buffer, over extern arg_count()/arg(i, out)
fn env(name: u8[:], out: u8[>..]&) -> bool
fn time() -> f64                                     // seconds since the epoch
fn clock() -> f64                                    // monotonic, high resolution
fn time_ns() -> i64      fn clock_ns() -> i64
fn sleep(seconds: f64)   fn sleep_ms(ms: i64)
fn random_seed() -> u64                              // OS entropy, for rng
```

Directory listing, subprocesses, sockets and date formatting are not in v1.
`exit(code)` is a builtin rather than an extern because the checker must
know it diverges (§8.3).

---

## 8. Builtins and language rules the library needed

All of these are now in the spec. They are ordered by how much of the
library depends on them.

### 8.1 The `print` / `str` / `format` family

Three builtins share one rendering engine. Each takes any number of
arguments of any type and renders them in sequence with no separator:

```goose
print(a, b, …)                 // stdout, newline at the end
str(a, b, …) -> u8[>..]        // a fresh string, constructed at the destination
format(out, a, b, …)           // append to any growable u8 array; out.format(a, b, …) via UFCS
```

`str` covers the need to quickly concatenate a few things into a new
string, which Goose does in place and should encourage: `words.push(str(
"item", i))` writes directly into the element slot. Each type is rendered
as follows:

* integers, floats (shortest round-tripping form), `bool`.
* a `u8` array or slice: raw bytes at the top level (concatenation is the
  common case), quoted and escaped when nested inside an aggregate.
* other arrays and slices: `[1, 2, 3]`.
* structs and ADT variants: the type name and the positional literal form,
  `float3 { 1, 2, 3 }`, `Circle { 1.5 }`, no field labels. The name keeps
  nested values and variants readable, and the output stays valid Goose.
* references: their pointee; optionals: `null` or the pointee; relative
  references likewise.
* a type with a user overload `fn format(out: u8[>..]&, v: T)` is rendered
  through that overload instead, wherever it occurs. The builtin is the
  structural fallback of the `format` overload set, and user overloads are
  found by the same resolution as any call.

Code generation specializes rendering for each type, using a traversal like
the one for equality. This follows Lobster's approach to printing values for
debugging (spec §3.7).

### 8.2 `default<T>()`

This is the value a fixed-size `T` has before anything is written to it,
with declared defaults honoured: numbers 0, `false`, empty arrays (length
0, capacity as declared), null optionals, an empty slice, variant 0 with
its own default payload, and structs field by field. A field with a
declared default (§3.2) gets that default, and every other field gets its
type's default. So a struct that declares `count: i64 = 1` as an invariant
keeps it when the library fills a slot with `default`. `sum` needs it (an
accumulator seed for generic `T`), and so does `dictionary` (fill values
for fresh slots); it is also useful anywhere `resize(n, v)` needs a neutral
`v`. It is a builtin because a generic function has no way to write a
literal of an unknown type (§4.1), and because only the compiler knows the
field defaults.

### 8.3 `abort(msg: u8[:])` and `exit(code: i64)`

Both are diverging calls: code after them is unreachable, so a branch that
ends in one gives no value (§6.4). `abort` prints `goose runtime error:
<msg>` and exits nonzero, like the checked aborts of §9.3; `exit` flushes
and exits with the code. The library uses `abort` for contract violations
that are not plain `assert`s (an unknown base in `format_int`).

### 8.4 `extern fn` (spec §7.10)

```goose
extern fn sqrt(x: f64) -> f64;
extern fn sqrt(x: f32) -> f32 = "sqrtf";
extern fn read_file(path: u8[:], out: u8[>..]&) -> bool = "gs_os_read_file";
extern fn gl_vertex3(v: float3&);
```

* An `extern fn` is a declaration without a body; the optional string is
  the C symbol (by default, the Goose name). It takes part in overload
  resolution like any function and is called directly from the generated C,
  with a prototype emitted unless the runtime defines the symbol.
* The return type is a scalar, a flat fixed struct, or nothing. Parameter
  types can be:
  * the integer and float scalars and `bool`;
  * any *flat, fixed-size* struct or fixed array, passed by value as its
    generated packed C type (`gs_float3`);
  * `T&` to such a `T`, passed as a pointer (`const gs_float3 *`, which is
    what an OpenGL-style API wants, since a packed `float3` is three
    contiguous floats);
  * `T[:]` of such a `T`, passed as pointer and count;
  * `u8[>..]&`, passed as the resizable's header plus its stack, so C code
    can append through a runtime helper (`gs_bld_append(bld, ptr, n)`).
    This is the parameter shape that lets the whole `os` layer be externs
    instead of builtins.
* These types cannot cross: `varint`, references inside structs, variable
  and resizable values by value, and slices of variable elements. The
  checker rejects them and gives the reason.
* Writability is checked at the call site as for `push` (§9.5). Extern
  functions are usable from thread programs when their C is re-entrant, as
  the runtime's is.
* The initial implementation leaves room for later extensions. The compiler
  always emits its own prototype from the Goose declaration. The C for the
  library's externs is in `src/runtime/runtime_os.h`, which is part of the
  runtime like the other runtime files. User externs get their C in via
  `--include <header>`; the header is emitted after the generated type
  declarations, so it can implement shims against the `gs_<name>` typedefs.
  An existing C API whose declaration differs from the emitted prototype
  (alignment, `APIENTRY`, pointer types) gets a one-line shim in that
  header.

### 8.5 `map` and the empty-array declaration (spec §4.2)

No change to generics is needed: a generic body is checked only at an
instantiation where every type is concrete, including what `F(x)` returns
(spec §7.7). What `map` does need is its result local, `var out = [];`,
whose `[]` has no element type *yet*. An empty-array local without an
annotation gets a pending array type, which the first
`push`/`append`/assignment of a concrete element completes. Using the local
in a way that needs the element type before then (`out[0]`) is an error,
while `out.len` is fine. A body that never completes the type is an error at
the end of the body. Codegen sees only completed types. So `map` is

```goose
fn map<T, F>(xs: T[:]) { var out = []; for x in xs { out.push(F(x)); } return out; }
```

with the return type inferred as usual (§7.1), and the same rule makes `var
names = [];` idiomatic user code.

### 8.6 Where `import std;` looks (spec §11.1)

This consists of the search path of §3, a `--stdlib` flag and a
`GOOSE_STDLIB` variable. The compiler finds the source tree's `stdlib/` from
its own location, so the test runner needs no flag.

### 8.7 Checker and language rules

**Passing by size class (spec §4.1).** This rule replaced "explicit `&` on
both ends". It depends on the size class of the value being connected to a
destination, where a destination is a call argument, a `let`/`var`
initializer, an assignment, a field initializer, a `push`, a `for` binding,
a match binder or a queue put:

* A **fixed** value connects by value. A reference-typed
  destination (`x: T&`, `T?`, an annotated `let r: T&`) binds the lvalue's
  address implicitly; `&x` stays legal and means the same, so forwarding a
  reference variable is unchanged.
* A **non-fixed** value is never copied implicitly. A reference-typed
  destination binds it. An untyped or generic destination binds it by
  reference too, since that is the only option that avoids a copy. A
  value-typed destination accepts an rvalue (a call result, a literal, a
  slice or other conversion, a local being returned) or an explicit
  `copy(x)`. `copy` is a builtin that constructs the copy at the destination
  (§4.3 semantics), and it is an explicit marker that is easy to search for.
* `&` at call sites becomes optional. If the reference-typed destination would
  already bind the source, writing `&` produces a warning. This encourages a
  consistent calling style.

The rule applies to individual constructs as follows:

* `let y = x;` for a non-fixed `x` is an error: write `let y = &x;` (a
  reference, the inferred type) or `let y = copy(x);`. For fixed `x` it
  copies.
* `push(v)` is a construction context (§4.2): a non-fixed lvalue `v` needs
  `copy(v)`; a slice `v[..]`, a literal or a call result does not.
  `append(src)` takes its source as a slice, and copying elements is its
  defined operation, so `g.append(h)` stays as it is.
* `g = h` between resizables (§4.4's clear-then-copy) becomes `g = copy(h)`;
  `g = f()` stays as it is. This is where a `move(x)` (spec TODO 3) would
  first be wanted: an assignment from a local at the end of its lifetime.
* `return x` of a non-fixed local moves it (NRVO, no copy); returning a
  non-fixed *field* or parameter copies it and needs `copy`. The only
  implicit copy of §7.3, which happens when two different locals are
  returned on different paths, stays implicit, because the programmer
  cannot know which of the two will be copied.
* `for x in xs` and `Circle c =>` copy fixed elements and payloads;
  non-fixed ones already require the `&x` binder form (§6.5, §8.1), and the
  rule makes that a consequence rather than a special case.
* When an untyped or `<T>` parameter is given a non-fixed lvalue, it binds
  `T` to the reference type; when it is given a fixed lvalue, it binds `T`
  to the value type. A type variable normally adopts the caller's type
  exactly. This is the only exception, a pragmatic one, and it is what lets
  one body of `fn total(xs)` serve a fixed array (copied), a `[>..]` and a
  `[>..<]` (by reference). A body that writes through `xs` is
  an error at the fixed instantiation (an immutable copy) rather than a
  silently discarded write.
* Overloads `f(x: T)` and `f(x: T&)` for the same fixed `T` become
  ambiguous for an lvalue argument. The pair `f<T>(xs: T[:])` / `f<A>(xs:
  A&)` does not (a `[>..<]` reaches only the second, an array reaches the
  second at the generic tier, and a slice reaches only the first), so the
  library's two container conventions can coexist.
* An explicit `&x` argument whose destination would have bound `x` without
  it gets the redundancy warning. Forwarding a reference *variable*
  (`f(r)` with `r: T&`) involves no `&` and gives no warning. Neither does
  `&x` where the destination is inferred (`let r = &x;`), since there the
  `&` is what selects the reference type.

For the library, this means no binder parameters and no `&` in any
example; UFCS works for every mutator; generic HOFs could even be untyped;
and the library's own copies become visible where they are intended
(`stable_sort`'s temporary is a construction).

Pros, beyond the UFCS fix that motivated the rule:

* The rule matches the cost model (registers for fixed values, in-place
  construction or a reference for everything else).
* Non-fixed copies become *impossible* to write by accident. This is
  stronger than the explicit-`&` rule, under which `f(arr)` to a by-value
  non-fixed parameter compiled and silently copied.
* One rule covers calls, `let`, `=`, fields, `push` and `for` alike.
* `copy` gives `move` a natural sibling.
* §4.1's "everything is a value" simplifies to "fixed values are values;
  non-fixed values live in one place and are copied only on request".

Cons, and what limits them:

* Mutation of a fixed value is no longer visible at the call site:
  `normalize(v)` may write `v`. This was accepted: the UFCS discussion had
  already concluded that mutable arguments would go unmarked at the call
  site, and mutating a fixed value is rarer than mutating arrays.
  Writability provenance (§9.5) limits it to `var` locals: a `let` argument
  yields a non-writable reference, and a callee that writes through it is a
  compile error that reports the call chain. So the reader's rule is "a
  `var` passed to a function may change", which is the C++ situation
  restricted to values the programmer already declared mutable.
* "Fixed" is not "small". A limited array `u8[..4096]` or a fixed
  `f32[1024]` is fixed-class and copies silently. This was accepted, with
  no mitigation for now: resizables will be far more common in this
  language than large fixed arrays, and the compiler may pass an unwritten
  by-value argument by pointer when the specialization's roots prove
  nothing writes the source during the call.
* `copy` will appear in places that feel like plain data flow, such as
  `items.push(copy(item))` and `let name = copy(rec.name)`, so readers can
  identify operations that copy data. If it turns out to be everywhere, a
  `*` form can be considered. The existing tests and benchmarks
  change in a handful of places (`let copy: u8[][] = src;`, `g = h`,
  `d.slots = ns`, pushes of variable-size locals); every `&` they already
  write stays valid, apart from the redundancy warning.
* `&` stays in the language. It is dropped only in places where the
  destination's type already says "reference": inferred `let r = &x;`
  keeps it, because there the `&` chooses the type. A reference-typed
  *field* initializer and `.=` also bind an lvalue implicitly, following
  the same rule: the struct declaration is the source of truth, just as a
  parameter list is.

**Slices of `[>..<]` (spec §5.2).** Creating a slice or reference
into a `[>..<]` is legal. A shrinking operation on that array (`pop`,
`resize`, `clear`, whole assignment) while a variable in scope may refer
into it is a compile error at the shrink. The error names the variable and
where it was bound, so the programmer can fix either end: end the slice's
block before the shrink, or move the shrink. Scope stands in for liveness.
This is the same test that a grow-only local's shrink already used (§5.1),
and it needs no last-use analysis. Inside a loop it is exact, since a
binding made earlier in the body is in scope at the shrink. Such a slice
may be passed down and returned, but not stored into a container (a field,
an element, a global); that restriction is what keeps every holder a
variable the scan can see. Some shrinks cannot be seen by a scope scan:
those through a reference parameter, and those of a global. These are
summarized per function specialization (which globals, which parameters'
pointees, transitively through callees), and a call is checked against the
summary in the same way, so the error for a callee's shrink is reported at
the call. The memory argument still holds: a live `[>..<]` owns its stack
region exclusively (§1.3(2)), so the rule catches logic errors, not memory
errors. The same pass treats the whole assignment of a grow-only array as
the shrink that it is (§5.1).

**References to resizable tails (spec C.2).** Before the frame object
existed, `&bag.items` was rejected. A resizable-tailed struct was a single
frame header `{ base, len }`, whose `base` is the start of the *struct* on
the data stack (static prefix fields, then tail elements) and whose `len` is
the tail's count. A `T[>..]&` is a pointer to a header whose `base` must be
the element region, and there was no such header for the tail alone. An
earlier option, offsetting the struct's header, fixes this at the cost of
addressing every struct relative to its tail. The review's instinct, to
give the tail its own independent header, leads to this layout: a
resizable-tailed struct is represented as a *frame object* that holds its
fixed prefix fields exactly as a fixed local would, followed by the tail's
own `gs_rhdr`. Only the tail's elements are stored on the data stack. With
this layout:

* `&bag.items` is a pointer to that header field. It is an ordinary
  `gs_rref`, indistinguishable from a reference to a standalone resizable,
  so nothing downstream needs to know it came from a struct.
* `&bag` is a pointer to the frame object.
* `bag.n` through either is a plain field access.
* `d.slots` via `d: dictionary&` is a normal header.
* The implicit slice coercion `count(bag.items)` follows.

Copying such a value copies the frame object and the elements (the
`RzShape` prefix walk of the bytes-on-stack shape becomes a struct copy
plus `EmitCopyElems`). By-value parameters and returns pass the frame
object where an array passes its header (C.3 passes "the metadata
appropriate to the type"). Nesting works the same way: a struct whose tail
is itself a resizable-tailed struct nests frame objects. The two awkward
cases keep the bytes-on-stack shape, behind a base pointer in the frame
object: a *variable-size* prefix (a `u8[]` field before the tail, which
`RzShape` already refuses to copy), and a resizable-class ADT, whose
payload shape differs per variant. Spec C.2 changes from "outermost values
have a frame header" to "a resizable-class value's fixed part lives in the
owning frame, and every resizable tail has its own header there". In the compiler,
`StructInst::frameobj` marks the shape, and codegen's `IsFrameObj` routes
locals, parameters, returns, globals, copies, literals and references
through the frame object. The checker's "nested resizable" error remains
only for the variable-size-prefix and ADT shapes.

**Untyped parameters bind exact types (spec §3.8).** An untyped parameter
binds a reference argument as a reference, exactly as an explicit `<T>`
parameter does, so `f(&gs)` into `fn f(xs)` mutates `gs`.

**`return null` is root-neutral (spec §9.2).** `fn find<T, F>(xs: T[:]) ->
T?` written as `for &x in xs { if F(x) { return x; } } return null;`
typechecks: a null return names no root, so it adds nothing to the roots
the other returns give.

**Generic type aliases** (`type set<K> = dictionary<K, bool>;`) do not
parse. Non-generic aliases of generic instances (`type float3 =
vec3<f32>;`) work and are all `vec` needs, so this is a convenience only.

**Constants in thread programs.** `let` globals of flat fixed type are
static data (§11.1); a `thread_fn` should be allowed to read them (`PI`,
`float3_0`), since they are not mutable shared memory. This is still open:
the checker bans every global from a thread program.

**Identity of references** (spec TODO 0b) is not needed by the library.
`lru.goose` relinks its entries with `index_of` (spec §3.3), the
subtraction the checker validates through roots.

---

## 9. Deliberately not included

These operations can be expressed using existing functions and short loops:

* `zip`/`map2`/`each2`: `for i in min(a.len, b.len) { … a[i] … b[i] … }`.
  Without tuples there is nothing for `zip` to return.
* `reduce` (fold without a seed): `fold(xs[1..], xs[0]) { … }`.
* `reversed` copies, `enumerate`: `each_rev`, and `for x, i in xs`.
* `product`, `average`: two-line folds.
* `first`/`top`: `xs[0]`, and `last` exists only because `xs[xs.len - 1]`
  is the expression everybody gets wrong by one.
* `flatten`, `partition`, `filter_indices`, `find_or_push`, `insert_ordered`:
  each is `filter`/`lower_bound`/`insert_at` plus a line.
* `keys`/`values` of a dictionary: `each` plus a push.
* String `concat` of many pieces, `repeat`, `pad`, `capitalize`, `escape`:
  `str(a, b, c)`, `push_n`, `format_int`'s width, and a loop.
* `lines`: `each_split(s, '\n')` plus `trim`. (`words` is in §4.8, because
  `each_split(s, ' ')` keeps the empty parts that repeated spaces leave, and
  a command line wants them dropped.)
* Multiple-resizable containers (a deque, an owning string dictionary): the
  language has one resizable per struct (§3.4); idioms in §5.3.
* Binary serialization: Goose data is already its own serialized form
  (`bench/design.md`); `read_file` into a `u8[>..]` and spec TODO 16's
  whole-region copy are the way to do it, and typed `read_u32_le`-style
  accessors can wait for a format that needs them.
* Date/time formatting, directories, subprocesses, networking: `os` grows
  when a program needs them, one extern at a time.
* Thread helpers: `thread_fn`s take no function values and no generics
  (§11.2), so a `parallel_for` cannot be written in the library. A
  `wait_all(ids: i64[:])` loop is one line. Revisit with spec TODO 9.

---
