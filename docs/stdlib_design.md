# The Goose Standard Library — Design Proposal

Status: revision 3, after the second review round. Section references of the
form §N are to `goose_spec.md`. Every calling convention and idiom below was
prototyped against the compiler; where a prototype hit a bug or a language
gap, that is recorded in §9 rather than designed around silently.

Changes since revision 2, from the review:

* The binder-parameter idea is withdrawn. In its place §8.7 works through
  the proposal to pass values by *size class*: fixed values by value,
  non-fixed values by reference or by explicit `copy`, with `&` no longer
  needed at call sites. The library's signatures are ordinary typed
  references again; the examples are written the way that rule reads.
* Text goes into strings with `format`, not `write`: the builtin family is
  `print`/`str`/`format`, the helpers are `format_int` and friends, and
  `write_*` is reserved for I/O. User `format` overloads extend the builtin.
* Aggregates print with their type name and positional fields.
* Slices of `[>..<]`: the error lands on the shrink and cites where the
  slice was taken and where it is still used.
* References to resizable tails: the recommended representation gives the
  tail its own frame header instead of offsetting the struct's.
* `extern fn` takes the simplest route (compiler-emitted prototypes, shims
  in an included header).

---

## 0. Summary

The standard library is five Goose source files (`std`, `math`,
`dictionary`, `vec`, `os`), about 120 functions in total, and it introduces
three families of named types: `dictionary<K, V>` (the one facility the
language has no building block for), `rng` (one word of PRNG state), and the
`vec2/3/4<T>` math vectors that §6.1 already promises. Everything else is a
function over the user's own arrays, structs and pools.

Everything is written in Goose except:

* a small extension of the builtin table: the `print`/`str`/`format`
  family, `default<T>()`, `abort(msg)`, `exit(code)`, and `copy(x)` if the
  passing rule of §8.7 is adopted; and
* a new `extern fn` declaration form, through which the `math` and `os`
  modules reach libm and a thin C runtime layer without adding a builtin per
  function.

Nothing in the library allocates: every temporary is a local resizable or a
limited array, every result is constructed in its destination (§4.3), and
every higher-order function compiles to a loop (§7.6).

---

## 1. Principles

**Functions over types.** Goose's strength is the array family: eight kinds of
array, pools, relative references, variable-mode ADTs, all named and laid out
by the user. The library must not compete with that by handing the user *its*
containers; it hands them operations that work on *theirs*. Concretely: a
library function takes a slice (`T[:]`) or a reference to whatever array the
caller has, never a library-defined wrapper, and returns either a scalar, a
reference into the input, or a fresh array built at the caller's destination.
New nominal types are introduced only where the language has no
representation to build on (a hash table) or the spec already asks for them
(vectors).

**Small.** One well-chosen function per job, chosen so that the obvious call
covers the common cases; no `_if`/`_by_key`/`_reverse`/`_indices` families.
The test applied to every candidate: is the idiomatic hand-written loop more
than three lines, *or* is it easy to get subtly wrong (sorting, hashing,
UTF-8, float printing, binary search)? If neither, it is not in the library.
§11 lists what was left out and the one-line loop that replaces it.

**Goose first, builtins by exception.** A builtin is justified only when it
buys something the language cannot express: custom typechecking (`print` of
any type, `default<T>()`), a diverging call the checker must know about
(`abort`), or a way out to the OS. Speed alone is not a reason yet — the
library's Goose code is expected to compile to the same loops a C programmer
would write — but where measurement later shows a library function would gain
from custom codegen (a word-at-a-time string hash, `memchr` for byte search),
that function moves into the builtin table without changing its signature.
The `extern fn` mechanism (§8.4) keeps that door open without growing the
typechecker.

**Zero cost, visibly.** No hidden allocation, no hidden copies. The
conventions in §2 are chosen so that the cheap call is the natural one to
write and the expensive one cannot be written by accident.

**Lobster's shape, Goose's cost model.** From Lobster's `std.lobster` we take
the function-heavy surface, the `it` blocks, `find`/`filter`/`fold`/`map`,
`insert_ordered`-style pragmatism; we do not take the
allocate-a-new-vector-per-call semantics (results are built in place) or the
do-everything builtins (`tokenize`, `concat_string`), which become small
composable pieces. From C++ and Rust we take the *coverage list* —
sort/stable sort/binary search/heap/dedup/retain/hash map/string search —
and reject the iterator protocols, trait bounds and wrapper types that carry
it there; Goose has `T?` references, multiple returns and `return from`
instead of `Option`, `Result` and exceptions.

---

## 2. Conventions

Each of these was checked against the compiler; the prototype results are
summarized in §9. Call-site spellings are given in the form of the passing
rule proposed in §8.7 (no `&` at call sites); until it is adopted, a
reference parameter takes an explicit `&x`.

### 2.1 How containers are passed

| The function… | Parameter | Call sites | Works on |
|---|---|---|---|
| reads, or mutates elements in place without changing the length (`sort`, `reverse`, `fill`, `find`, `sum`, …) | `xs: T[:]` | `f(arr)`, `f(arr[1..])`, `f(slice)` | every array kind and slices (`[>..<]` once §8.7's slice rule lands) |
| changes the length (`push_n`, `insert_at`, `remove_at`, `retain`, `heap_push`, …) | `xs: A&` — `A` a generic bound to the whole array type | `f(arr)` (today `f(&arr)`) | every array kind that has the operations used |
| takes a library type (`dictionary`, `rng`) | `d: dictionary<K, V>&` | `d.insert(k, v)` (today `insert(&d, k, v)`) | — |

Rationale, and what was ruled out:

* Arrays coerce to slice parameters copy-free (§3.10), so `sort(arr)` costs
  nothing and reads naturally; writes through the slice are legal when the
  array is writable (§9.5), so in-place algorithms need no second form. A
  `let` array passed to `sort` is a compile error carrying the call chain,
  which is the intended behaviour.
* A generic reference parameter `xs: A&` binds `A` to the whole array type
  and instantiates per kind: one `push_n<A, T>(xs: A&, v: T, n: i64)` body
  serves a `[>..<]`, a `[>..]`, a `[..k]` field. It cannot accept a slice,
  so it is reserved for length-changing operations, where a slice would be
  meaningless anyway.
* An untyped parameter (`fn f(xs)`) binds the argument's exact type, which
  since revision 2 includes a reference type for `f(&arr)`. Under §8.7 a
  non-fixed lvalue argument reaches an untyped parameter by reference and a
  fixed one by value, so `fn total(xs)` would serve every kind; the library
  still prefers `T[:]` for read-only functions because it names the element
  type (`find -> T?`, `contains(xs, v: T)`) and accepts sub-ranges.
* Grow-shrink arrays `[>..<]` cannot be sliced today (§5.2), so they cannot
  reach the first row; the checker rule of §8.7 lifts that. Until then only
  the length-changing functions apply to them.
* Nothing is ever taken by value. Under §8.7 that is enforced by the
  language for every non-fixed type; today it is a convention.

### 2.2 Results

* **A fresh array** (`filter`, `map`, `split`, `concat`, `str`) is returned as
  `T[>..]`: a resizable local built by NRVO straight into the caller's
  destination (§7.3), so `let ys = xs.filter() { it > 0 }`,
  `var ys = xs.filter() { … }` (and keep pushing), `out.append(xs.map() {
  … })` and `words.push(str("w", i))` all cost the elements and nothing
  more. `T[>..]` rather than `T[]` because it has no length-field limit and
  the caller most often wants to keep growing or sorting it; the "fresh
  stack" it needs is a stack index for the value's lifetime, not a runtime
  cost. A `T[]` destination converts.
* **"Maybe an element"** (`find`, `get`, `last`) is a `T?` — an optional
  reference into the input, rooted where the input is rooted, null for
  "none". The caller narrows with `if`/`guard` (§3.8) and can write through
  it. There is no `Option<T>` value type and the library does not invent one.
* **A position** (`find_index`, `index_of`, `min_by`, `lower_bound`) is an
  `i64`, with `-1` for "none" where that can happen.
* **Two-outcome scalars** (`parse_int`, `binary_search`, `read_line`) use the
  spec's convention of a trailing `bool` in a multiple return: `let v, ok =
  parse_int(s);`. There is no error type; programming errors `abort`.
* **Text output** goes into a builder passed in (`format(out, …)`,
  `read_file(path, out)`): the result appends transparently to whatever the
  caller is assembling. The one exception is `str(…)`, whose whole point is
  to *be* the fresh string.

### 2.3 Function-value parameters

* Function values are trailing blocks (§7.6): `xs.find() { it.key == k }`,
  `xs.sort() { a, b => a.age < b.age }`, or a named function.
* A comparator is always a strict "a goes before b" predicate `lt(a, b)`.
  Sorting, heap and extremum functions come in two arities: with a block,
  and without one meaning `a < b` (so `sort(xs)` works for numbers and
  `sort(xs) { a, b => compare(a, b) < 0 }` for strings). Overload resolution
  separates the two by whether a function value is supplied, which was
  verified.
* A *key* function returns a value (`min_by(xs) { it.score }`).
* A block parameter binds a **reference** when the library passes it `&x`,
  so blocks can mutate: `d.each() { k, v => v += 1; }` works because `each`
  calls `F(s.key, &s.val)`. Functions document which arguments they pass by
  reference.
* Inside a block, `return` returns from the *enclosing named function*
  (§7.6), so early exits are available to callers of `each_*` functions
  for free.

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

* A concrete overload beats a generic one (`hash(k: mykey)` wins over
  `hash<T>(x: T)`), so user overloads take precedence automatically.
* Every integer width needs its **own** overload: a `u8` argument widens
  equally well into `i64` and `u64`, and two coercion-tier matches are an
  ambiguity error. So `hash` is ten one-line functions, not one generic.

### 2.5 UFCS

`x.f(args)` is `f(x, args)` (§7.1). Today `x` is passed by value, which means
a function taking `T&` cannot be called as `x.f()` on a local value `x`
("cannot pass dictionary<…> as dictionary<…>&"): it must be `f(&x, …)`, or
`x` must already be a reference variable or parameter (`fn handle(d:
dictionary<…>&) { d.insert(k, v); }` compiles; so does `let r = &out;
r.format("!")`). The builtin members (`push`, `format`) are exempt, since
their receiver is an lvalue.

Under the passing rule of §8.7, `x.f()` binds `x` by reference exactly when
`f`'s first parameter is a reference type, for the same reason a free call
`f(x)` does, so `d.insert(k, v)`, `q.heap_push(v)` and `r.rand_int(6)` all
work and mean the obvious thing. The binder-parameter idea of revision 2 was
withdrawn because it would have taught callers to drop `&` everywhere,
which brings back the accidental copy the moment someone forgets to declare
a binder, and because it made the call form depend on knowing how each
function was declared.

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
  so the array and dictionary vocabularies are kept disjoint by construction.
* `format_*`: append a textual form to a `u8[>..]` builder with formatting
  control; `parse_*`: the inverse; `to_*`: in-place case conversion;
  `write_*`: I/O, and only I/O.
* No `is_` prefix (`any`, `all`, `contains`, `starts_with`).
* There is no namespacing in v1 (§11.1), so every library name is global.
  Names were chosen to be the ones a user would also pick, on the theory
  that a user who defines their own `find` with a concrete signature gets
  theirs (exact match beats generic), and a user who defines a generic one
  gets a clear duplicate or ambiguity error rather than silent shadowing.

### 2.7 Strings

There is no string type (§3.7); the library's string functions are functions
on `u8` arrays with these roles:

| Role | Type | Notes |
|---|---|---|
| input | `u8[:]` | literals, any `u8` array and any slice coerce |
| output being built | `u8[>..]&` | the "builder"; `push`/`append`/`format` are the primitives |
| owned, stored | `u8[]`, `u8[varint]`, `u8[..k]` | fields and elements; a `u8[>..]` result lands in them |

All functions are byte-oriented and ASCII by default (`to_lower`, `trim`);
`each_utf8`/`push_utf8` are the only encoding-aware pieces. Comparison is
`==` (structural, §4.5) and `compare`. A character literal is an integer
constant (§2): `format(out, 'x')` appends `120`, and a byte goes in with
`out.push('x')` or `format(out, "x")`.

---

## 3. Module map

| File | Imports | Contents | Depends on compiler work (§8) |
|---|---|---|---|
| `stdlib/std.goose` | — | scalars, bits, hashing, `rng`; array HOFs, searching, sorting, heaps, resizing; strings, formatting control, parsing, UTF-8 | `default<T>()` for `sum`; `abort`; bugs in §9 |
| `stdlib/math.goose` | — | libm as `extern fn`, `PI`/`TAU`, `radians`/`degrees`, `is_nan`/`is_inf` | `extern fn` |
| `stdlib/dictionary.goose` | `std` | `dictionary<K, V>` and its functions | `default<T>()` |
| `stdlib/vec.goose` | `std`, `math` | `vec2/3/4<T>`, `float*`, `double*`, `int*`, vector functions | — |
| `stdlib/os.goose` | `std` | files, stdio, arguments, environment, time, sleep, entropy | `extern fn`, runtime C in `src/runtime/runtime_os.h` |

`import std;` is explicit; nothing is auto-imported. The compiler resolves
`import std;` by looking, in order, in the importing program's directory (as
today), in `GOOSE_STDLIB` or `--stdlib <dir>` if given, and then by walking
up from its own executable's directory looking for `stdlib/std.goose` (three
levels), so `build/Debug/goose.exe`, `build/goose`, `bin/goose.exe` and an
installed `<prefix>/bin/goose` beside `<prefix>/stdlib` all find it without
a blessed location. Unused library functions cost parse time only:
functions are typechecked in call-graph order from `main`, so an
unreferenced generic never instantiates.

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
fn sign<T>(x: T) -> T           // -1, 0, 1 at x's type; f32/f64 overloads
fn lerp(a: f64, b: f64, t: f64) -> f64      // and an f32 overload
fn next_pow2(x: i64) -> i64     // smallest power of two >= max(x, 1)
fn popcount(x: u64) -> i64
fn clz(x: u64) -> i64           // 64 for x == 0
fn ctz(x: u64) -> i64           // 64 for x == 0
fn swap<T>(a: T&, b: T&)
```

Generic numeric bodies cannot use literals for floats (`x < 0` does not
typecheck at `T = f64`, since an integer constant never becomes a float,
§6.3), hence the explicit float overloads. `popcount`/`clz`/`ctz` are pure
Goose bit tricks in v1 and become `extern` intrinsics once §8.4 lands.

### 4.2 Hashing

```goose
fn hash(x: i8) -> u64        // … one overload per integer type, i8 … u64
fn hash(x: bool) -> u64
fn hash(x: f32) -> u64
fn hash(x: f64) -> u64
fn hash(s: u8[:]) -> u64     // FNV-1a over the bytes; any u8 array coerces
fn hash_combine(seed: u64, h: u64) -> u64   // for composite keys
```

The integer hashes are a multiply-xorshift mix (Fibonacci hashing), so the
low bits are well distributed and `dictionary` can mask rather than divide.
A user key type gets `fn hash(k: key) -> u64 { hash_combine(hash(k.a),
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

`var r = rng { 12345 };` then `r.rand_int(6)`. splitmix64 is one word of
state, 64-bit arithmetic only, and passes the usual batteries; the
benchmarks' xorshift stays where it is. Seeding from the OS is `os`'s
`random_seed()`.

### 4.4 Arrays: reading and searching

```goose
fn each_rev<T, F>(xs: T[:])                    // F(x), last to first
fn each_chunk<T, F>(xs: T[:], n: i64)          // F(chunk: T[:], i); the last chunk may be short
fn find<T, F>(xs: T[:]) -> T?                  // first x with F(x), as a reference; null if none
fn find_index<T, F>(xs: T[:]) -> i64           // …or its index, -1 if none
fn index_of<T>(xs: T[:], v: T) -> i64          // first i with xs[i] == v, -1 if none
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
so they are the string functions too (`find(line, "://")`), and they
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

`map`'s body is `var out = []; for x in xs { out.push(F(x)); } return out;`
once the empty-array declaration of §8.5 exists; in phase 1 it is spelled
`map<U, T, F>` and called as `xs.map<f64>() { … }`, which works today.
`sum` needs `default<T>()`; `fold(xs, 0.0) { a, x => a + x }` is the form
that needs nothing.

### 4.6 Arrays: in place

```goose
fn fill<T>(xs: T[:], v: T)
fn copy_into<T>(dst: T[:], src: T[:])            // asserts equal lengths; memmove semantics
fn reverse<T>(xs: T[:])
fn sort<T>(xs: T[:])                             // by <
fn sort<T, F>(xs: T[:])                          // by F(a, b); unstable, in place, no allocation
fn stable_sort<T>(xs: T[:])
fn stable_sort<T, F>(xs: T[:])                   // merge sort; one temporary of xs.len elements
fn to_lower(s: u8[:])                            // ASCII, in place
fn to_upper(s: u8[:])
```

`sort` is a quicksort with median-of-three pivots, insertion sort below 16
elements, and an explicit `i64[..128]` range stack that always defers the
larger partition, so it recurses nowhere, allocates nothing, and its stack is
bounded by 2·log₂(2⁴⁸). Its worst case is quadratic on adversarial input;
pdqsort's pattern defeat is the planned upgrade once there is a benchmark
for it. `stable_sort` is bottom-up merge sort with a `T[>..]` temporary on a
fresh data stack (a stack index, not an allocation).

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
(`remove_at` on a grow-only array is an instantiation error naming the
missing `pop`). A `[>..<]` is the expected heap and queue container; a
limited `[..k]` inside a struct works too.

### 4.8 Strings

```goose
fn format_int(out: u8[>..]&, v: i64, base: i64, width: i64, pad: u8)   // base 2..36, right-aligned
fn format_uint(out: u8[>..]&, v: u64, base: i64, width: i64, pad: u8)
fn format_flt(out: u8[>..]&, v: f64, decimals: i64)   // fixed decimals
fn parse_int(s: u8[:]) -> i64, bool              // optional sign, base 10, whole string
fn parse_int(s: u8[:], base: i64) -> i64, bool
fn parse_flt(s: u8[:]) -> f64, bool              // extern-backed (strtod); Goose fallback in phase 1
fn compare(a: u8[:], b: u8[:]) -> i64            // bytewise lexicographic: -1, 0, 1
fn trim(s: u8[:]) -> u8[:]                       // ASCII whitespace; a sub-slice, no copy
fn trim_start(s: u8[:]) -> u8[:]
fn trim_end(s: u8[:]) -> u8[:]
fn split(s: u8[:], sep: u8) -> u8[:][>..]        // slices into s; empty parts kept
fn each_split<F>(s: u8[:], sep: u8)              // F(part: u8[:]); no array built
fn each_split<F>(s: u8[:], sep: u8[:])
fn join<T>(out: u8[>..]&, parts: T[:], sep: u8[:])   // T any u8 array/slice type
fn format_replaced(out: u8[>..]&, s: u8[:], from: u8[:], to: u8[:])
fn each_utf8<F>(s: u8[:])                        // F(cp: i64); malformed bytes yield 0xFFFD
fn push_utf8(out: u8[>..]&, cp: i64)             // the UTF-8 bytes of a code point
```

Plain rendering of scalars and strings is the builtin `format` (§8.1); the
functions here add formatting control and parsing. `split` returning slices
into its input (no copies, elements rooted at the caller's text) is the
Goose replacement for Lobster's `tokenize`; the `each_split` form does not
even build the array.

---

## 5. `dictionary`

### 5.1 Representation

```goose
struct dictionary_slot<K, V> { key: K, val: V, used: bool }
struct dictionary<K, V> { count: i64 = 0, slots: dictionary_slot<K, V>[>..] = [] }
```

* Open addressing, linear probing, power-of-two capacity, `hash(key) & mask`.
  `K` and `V` must be fixed-size (the slot array is indexed randomly, §3.3);
  strings are keyed as `u8[:]` slices into text the caller owns, or as inline
  `u8[..k]`. Deletion is backward-shift (the technique `bench/goose/lru.goose`
  uses), so there are no tombstones and no periodic cleaning.
* A `dictionary` is resizable-class (its tail is a `[>..]`), so it lives
  where resizables live: a local, a global, a by-reference parameter, the
  tail of a struct. It is spec A.3, generalized.
* Growth doubles at ⅔ load: a fresh slot array local is filled by
  re-inserting the used slots and then assigned over `d.slots` — the
  whole-resizable assignment of §4.4, prototyped and working (under §8.7 it
  is spelled `d.slots = copy(ns)`, or `move(ns)` once that exists). The
  transient second table costs one data stack index during the call, never
  an allocation. In-place doubling (`append` then re-place) is possible
  with this layout and can replace it later without changing the API.
* Fresh slots are filled with `default<K>()`/`default<V>()` (§8.2).
* A hash is not cached in the slot in v1 (17 bytes for an `i64 → i64` table);
  caching it (`h: u32`) speeds rehash and slice-keyed probes and is a
  measured decision for later.

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

(Today, without §8.7: `update(&counts, it, 0) { … }` and `each(&counts) …`.)
`update` exists because the natural spelling, `get_or_insert(d, k, 0) += 1`,
is not an assignable location and the annotated binding needs the value
type spelled out (§2.5). A block gets the reference without either problem.

References returned by `get`/`get_or_insert` stay memory-safe forever (§5.1
of the spec: grow-only storage never moves or shrinks) but are *logically*
valid only until the next `insert`, `update` or `remove`: a rehash refills
the same stack region with the new table and a backward-shift moves
entries, so a stale reference reads a different, well-typed slot — the
language's type-safe reuse, documented per function.

### 5.3 Sets and other idioms

* A set is `dictionary<K, bool>`: `s.insert(k, true)`, `s.contains(k)`. A
  dedicated `set<K>` needs generic type aliases (§8.7) and a zero-size value
  type, neither of which exists yet; the byte per slot is not worth a second
  implementation.
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

Elementwise `+ - * /` are the language's (§6.1); the prototype confirmed a
`type float3 = vec3<f32>` alias constructs positionally, adds memberwise and
instantiates generic `dot` for `f32` and `i64`. `int*` are `i32` because
vectors are data (§3.1) and an `i32` component widens into an `i64` index
implicitly. Elementwise `min`/`max`/`abs` are deliberately absent: a
`min<T>(a: vec3<T>, b: vec3<T>)` overload is ambiguous with the scalar
`min<T>(a: T, b: T)` (both generic tier), and `min(a.x, b.x)` per component
is what a user writes anyway. The lengthening constructors (`vec3(v, z)`)
were dropped: `vec3 { v.x, v.y, z }` is as short and avoids a function
named like a type.

---

## 7. `math` and `os` (extern-backed)

`math` is libm, declared once per function and float width:

```goose
extern fn sqrt(x: f64) -> f64;        extern "sqrtf" fn sqrt(x: f32) -> f32;
// sin cos tan asin acos atan atan2 exp log log2 log10 pow floor ceil round trunc, likewise
let PI = 3.141592653589793;  let TAU = 6.283185307179586;
fn radians(deg: f64) -> f64
fn degrees(rad: f64) -> f64
fn is_nan(x: f64) -> bool             // x != x
fn is_inf(x: f64) -> bool
```

`floor`/`ceil`/`round`/`trunc` return floats, as in C; `as i64` converts.
Float `%` is already `fmod` (§6.1).

`os` is a thin, deliberately incomplete layer — enough to write tools and
benchmarks — over C functions in a new `src/runtime/runtime_os.h`:

```goose
fn read_file(path: u8[:], out: u8[>..]&) -> bool     // appends the whole file
fn write_file(path: u8[:], data: u8[:]) -> bool
fn file_exists(path: u8[:]) -> bool
fn delete_file(path: u8[:]) -> bool
fn read_line(out: u8[>..]&) -> bool                  // stdin; strips the newline; false at EOF
fn read_stdin(out: u8[>..]&) -> bool                 // everything until EOF
fn write_stdout(s: u8[:])
fn write_stderr(s: u8[:])
fn args() -> u8[][>..]                               // Goose over extern arg_count()/arg_get(i, out)
fn env(name: u8[:], out: u8[>..]&) -> bool
fn time() -> f64                                     // seconds since the epoch
fn clock() -> f64                                    // monotonic, high resolution
fn sleep(seconds: f64)
fn random_seed() -> u64                              // OS entropy, for rng
```

Directory listing, subprocesses, sockets and date formatting are not in v1.
`exit(code)` is a builtin rather than an extern because the checker must
know it diverges (§8.3).

---

## 8. Builtin and language additions the library needs

Ordered by how much of the library depends on them.

### 8.1 The `print` / `str` / `format` family

One rendering engine behind three builtins, each taking any number of
arguments of any type, rendered in sequence with no separator:

```goose
print(a, b, …)                 // stdout, newline at the end
str(a, b, …) -> u8[>..]        // a fresh string, constructed at the destination
format(out, a, b, …)           // append to any growable u8 array; out.format(a, b, …) via UFCS
```

`str` is the answer to "quickly concatenate a few things into a new
string", which Goose does in place and should encourage: `words.push(str(
"item", i))` writes straight into the element slot. Rendering per type:

* integers, floats (shortest round-tripping form, as today), `bool`.
* a `u8` array or slice: raw bytes at the top level (concatenation is the
  common case), quoted and escaped when nested inside an aggregate.
* other arrays and slices: `[1, 2, 3]`.
* structs and ADT variants: the type name and the positional literal form,
  `float3 { 1, 2, 3 }`, `Circle { 1.5 }`, no field labels. The name keeps
  nested values and variants readable, and the output stays valid Goose.
* references: their pointee; optionals: `null` or the pointee; relative
  references likewise.
* a type with a user overload `fn format(out: u8[>..]&, v: T)` renders
  through it instead, wherever it occurs — the builtin is the structural
  fallback of the `format` overload set, and user overloads are found by
  the same resolution as any call.

This is codegen per type, the same walk equality already does, and it is
the single most useful debugging affordance Lobster has. Scalars and
strings ship first; aggregates and user overloads follow.

### 8.2 `default<T>()`

The value a fixed-size `T` has before anything is written to it, with
declared defaults honoured: numbers 0, `false`, empty arrays (length 0,
capacity as declared), null optionals, an empty slice, variant 0 with its
own default payload, and structs field by field — a field with a declared
default (§3.2) gets it, every other field gets its type's default. So a
struct that declares `count: i64 = 1` as an invariant keeps it when the
library fills a slot with `default`. Needed by `sum` (an accumulator seed
for generic `T`) and by `dictionary` (fill values for fresh slots); useful
anywhere `resize(n, v)` wants a neutral `v`. It is a builtin because a
generic function has no way to write a literal of an unknown type (§4.1),
and because the field defaults are the compiler's to know.

### 8.3 `abort(msg: u8[:])` and `exit(code: i64)`

Diverging calls, so `guard c else { abort("bad input"); }` typechecks (§6.4
requires the else block to diverge). `abort` prints `goose runtime error:
<msg>` and exits nonzero, like the checked aborts of §9.3; `exit` flushes
and exits with the code. The library uses `abort` for contract violations
that are not plain `assert`s (an unknown base in `format_int`).

### 8.4 `extern fn`

```goose
extern fn sqrt(x: f64) -> f64;
extern "sqrtf" fn sqrt(x: f32) -> f32;
extern "gs_os_read_file" fn read_file(path: u8[:], out: u8[>..]&) -> bool;
extern fn gl_vertex3(v: float3&);
```

* A declaration without a body; the optional string is the C symbol (default:
  the Goose name). It joins overload resolution like any function and is
  called directly from the generated C, with a prototype emitted unless the
  runtime defines the symbol.
* Parameter and return types: the integer and float scalars and `bool`;
  any *flat, fixed-size* struct or fixed array, passed by value as its
  generated packed C type (`gs_float3`); `T&` to such a `T`, passed as a
  pointer (`const gs_float3 *` — what an OpenGL-style API wants, since a
  packed `float3` is three contiguous floats); `T[:]` of such a `T`, passed
  as pointer and count; and `u8[>..]&`, passed as the resizable's header
  plus its stack, so C code can append through a runtime helper
  (`gs_bld_append(bld, ptr, n)`) — the one parameter shape that lets the
  whole `os` layer be externs instead of builtins. Returns are a scalar, a
  flat fixed struct, or nothing.
* What does not cross: `varint`, references inside structs, variable and
  resizable values by value, slices of variable elements. The checker
  rejects them with the reason.
* Writability is checked at the call site as for `push` (§9.5). Extern
  functions are usable from thread programs when the C is re-entrant, which
  the runtime's are.
* Simplest workable plumbing, to be extended later: the compiler always
  emits its own prototype from the Goose declaration; the C for the
  library's externs lives in `src/runtime/runtime_os.h`, prepended like the
  other runtime files; user externs get their C in via `--include <header>`,
  emitted after the generated type declarations so a header can implement
  shims against the `gs_<name>` typedefs. An existing C API whose
  declaration differs from the emitted prototype (alignment, `APIENTRY`,
  pointer types) gets a one-line shim in that header.

### 8.5 `map` and the empty-array declaration

Nothing about generics is needed: a generic body is checked only at an
instantiation where every type is concrete, including what `F(x)` returns
(spec §7.7 now says so). The only thing `map` cannot write today is its
result local: `var out = [];` is rejected because `[]` has no element type
*yet*. The fix is local-variable inference in the checker: an empty-array
local without an annotation gets a pending array type that the first
`push`/`append`/assignment of a concrete element completes; using it in a
way that needs the element type before then (`out[0]`) is an error,
`out.len` is fine, and a body that never completes it is an error at its
end. Codegen sees only completed types. Then `map` is

```goose
fn map<T, F>(xs: T[:]) { var out = []; for x in xs { out.push(F(x)); } return out; }
```

with the return type inferred as usual (§7.1). The same rule makes `var
names = [];` idiomatic user code. Until it exists, `map<U, T, F>` with
`xs.map<f64>() { … }` is the phase-1 form.

### 8.6 Where `import std;` looks

The search path of §3, a `--stdlib` flag and a `GOOSE_STDLIB` variable; the
test runner points at the source tree's `stdlib/`.

### 8.7 Checker and language changes

**Passing by size class — proposed (question 1).** The review proposed
replacing "explicit `&` on both ends" (§7.2) with a rule keyed on the size
class of the value being connected to a destination — a call argument, a
`let`/`var` initializer, an assignment, a field initializer, a `push`, a
`for` binding, a match binder, a queue put:

* A **fixed** value connects by value, as today. A reference-typed
  destination (`x: T&`, `T?`, an annotated `let r: T&`) binds the lvalue's
  address implicitly; `&x` stays legal and means the same, so forwarding a
  reference variable is unchanged.
* A **non-fixed** value is never copied implicitly. A reference-typed
  destination binds it; an untyped or generic destination binds it by
  reference too, that being the only free option; a value-typed destination
  accepts an rvalue — a call result, a literal, a slice or other conversion,
  a local being returned — or an explicit `copy(x)`, a builtin that
  constructs the copy at the destination (§4.3 semantics; one word, and
  greppable).
* `&` at call sites is thereby redundant rather than required.

Where the rule has to be pinned down, with the reading this document
assumes:

* `let y = x;` for a non-fixed `x` is an error: write `let y = &x;` (a
  reference, the inferred type as today) or `let y = copy(x);`. For fixed
  `x` it copies, as today.
* `push(v)` is a construction context (§4.2): a non-fixed lvalue `v` needs
  `copy(v)`; a slice `v[..]`, a literal or a call result does not.
  `append(src)` takes its source as a slice and copies elements as its
  defined operation, so `g.append(h)` stays as it is.
* `g = h` between resizables (§4.4's clear-then-copy) becomes `g = copy(h)`;
  `g = f()` stays. This is where a `move(x)` (spec TODO 3) would first be
  wanted: the dictionary's rehash assigns a dying local.
* `return x` of a non-fixed local moves it (NRVO, no copy); returning a
  non-fixed *field* or parameter copies and takes `copy`. The one implicit
  copy of §7.3 — two different locals returned on different paths — stays
  implicit, because the programmer cannot know which of the two will be the
  copied one.
* `for x in xs` and `Circle c =>` copy fixed elements and payloads as today;
  non-fixed ones already require the `&x` binder form (§6.5, §8.1) — the
  rule makes that a consequence rather than a special case.
* An untyped or `<T>` parameter given a non-fixed lvalue binds `T` to the
  reference type; given a fixed lvalue, to the value type. `fn total(xs)`
  then serves a fixed array (copied), a `[>..]` and a `[>..<]` (by
  reference) with one body, and a body that writes through `xs` is an
  error at the fixed instantiation (an immutable copy) rather than a
  silently discarded write.
* Overloads `f(x: T)` and `f(x: T&)` for the same fixed `T` become
  ambiguous for an lvalue argument; the pair `f<T>(xs: T[:])` / `f<A>(xs:
  A&)` does not (a `[>..<]` reaches only the second, an array reaches the
  second at the generic tier, a slice only the first), so the library's two
  container conventions coexist.
* An explicit `&x` argument to a reference parameter is accepted silently
  (it is what forwarding a reference already looks like); a lint for
  redundant ones can come later.

For the library: no binder parameters, no `&` in any example, UFCS works
for every mutator, generic HOFs could even be untyped, and the library's
own copies become visible where they are intended (`stable_sort`'s
temporary is a construction; the dictionary rehash is a `copy`/`move`).

Pros, beyond the UFCS fix that motivated it: the rule matches the cost
model exactly (registers for fixed values, in-place construction or a
reference for everything else); non-fixed copies become *impossible* to
write by accident, which is stronger than today, where `f(arr)` to a
by-value non-fixed parameter compiles and silently copies; one rule
covers calls, `let`, `=`, fields, `push` and `for` alike; `copy` gives
`move` a natural sibling; and §4.1's "everything is a value" simplifies to
"fixed values are values; non-fixed values live in one place and are
copied only on request".

Cons, and what limits them:

* Mutation of a fixed value is no longer visible at the call site:
  `normalize(v)` may write `v`. Writability provenance (§9.5) confines this
  to `var` locals — a `let` argument yields a non-writable reference, and a
  callee that writes through it is a compile error with the call chain —
  so the reader's rule is "a `var` passed to a function may change", which
  is the C++ situation restricted to values the programmer already declared
  mutable. Naming conventions do the rest; a lint could flag writes through
  implicitly bound fixed references if it turns out to matter.
* "Fixed" is not "small". A limited array `u8[..4096]` or a fixed
  `f32[1024]` is fixed-class and would copy silently. Options: accept it
  (C's struct-by-value rule; the compiler may pass an unwritten by-value
  argument by pointer when the specialization's roots prove nothing writes
  the source during the call); a size-based lint; or treat limited arrays
  as copy-explicit. Question 1b.
* `copy` will appear in places that feel like plain data flow —
  `items.push(copy(item))`, `let name = copy(rec.name)` — which is the
  point, but is a migration cost. The existing tests and benchmarks change
  in a handful of places (`let copy: u8[][] = src;`, `g = h`, `d.slots =
  ns`, pushes of variable-size locals); every `&` they already write stays
  valid.
* `&` does not leave the language, only call sites: reference creation for
  inferred `let`s, `.=` rebinding, and reference-typed fields initialized
  from lvalues still spell it, and a reference-typed field initializer
  binding an lvalue implicitly would read oddly enough that the rule should
  perhaps stop at call sites, `let`s and `=`. Question 1c.
* Spec text to rewrite: §4.1, §4.4, §7.2, the §3.8 binding paragraph, §7.5's
  implementation note, and every "explicit on both ends" mention.

Recommendation: adopt it, in the reading above. Nothing in the library's
design depends on the outcome except the spelling of examples.

**Slices of `[>..<]` — decided, with the error placement from the review.**
Creating a slice or reference into a `[>..<]` becomes legal. A shrinking
operation on that array (`pop`, `resize`, `clear`, whole assignment) while
such a slice or reference is still *live* — used again on some later path —
is a compile error at the shrink, citing where the slice was taken and where
it is next used, so the programmer can fix either end. Liveness is the
last-use information the checker keeps for named results (§7.3); inside a
loop a shrink anywhere in the body counts against slices taken earlier in
the body, since the next iteration would see them. Such a slice may be
passed down and returned (the caller's copy of the same rule applies to
what it receives) but not stored into a container, which would escape the
analysis. The memory argument is that a live `[>..<]` owns its stack region
exclusively (§1.3(2)), so even a slice that outlives a shrink would read
type-safe reused memory exactly as a limited array's does (§5.3); the rule
catches the logic error, not a memory one. Spec §5.2, §3.10 and TODO 4
change accordingly. Until it lands, `[>..<]` reaches only the
length-changing functions.

**References to resizable tails — recommended representation (question
2).** `&bag.items` is rejected today because a resizable-tailed struct is
one frame header `{ base, len }` whose `base` is the *struct's* start on the
data stack (static prefix fields, then tail elements) and whose `len` is
the tail's count; a `T[>..]&` is a pointer to a header whose `base` must be
the element region, and there is no such header for the tail alone.
Offsetting the struct's header (revision 2's option a) fixes it at the cost
of every struct being addressed relative to its tail. The review's
instinct — give the tail its own independent header — leads to this layout:
a resizable-tailed struct is represented as a *frame object* holding its
fixed prefix fields exactly as a fixed local would, followed by the tail's
own `gs_rhdr`; only the tail's elements live on the data stack. Then
`&bag.items` is a pointer to that header field — an ordinary `gs_rref`,
indistinguishable from a reference to a standalone resizable, so nothing
downstream needs to know it came from a struct; `&bag` is a pointer to the
frame object; `bag.n` through either is a plain field access; `d.slots` via
`d: dictionary&` is a normal header; and the implicit slice coercion
`count(bag.items)` follows. Copying such a value copies the frame object
and the elements (today's `RzShape` prefix walk becomes a struct copy plus
`EmitCopyElems`); by-value parameters and returns pass the frame object
where they pass the header today (C.3 already passes "the metadata
appropriate to the type"). Nesting composes: a struct whose tail is itself
a resizable-tailed struct nests frame objects. The two awkward cases keep
today's bytes-on-stack shape behind a base pointer in the frame object: a
*variable-size* prefix (a `u8[]` field before the tail, which `RzShape`
already refuses to copy), and a resizable-class ADT, whose payload shape is
per variant. Spec C.2 changes from "outermost values have a frame header"
to "a resizable-class value's fixed part lives in the owning frame, and
every resizable tail has its own header there".

**Untyped parameters bind exact types — done.** `TryMatch` decayed a
reference argument to its pointee before binding an untyped parameter, so
`f(&x)` handed `f` a copy of `x`; an explicit `<T>` parameter already bound
the reference. The decay was removed, §3.8 reworded, and the suite passes
(commit `a6bde04`); `f(&gs)` into `fn f(xs)` now mutates `gs`, and the
codegen crash B7 of revision 1 no longer arises on that path.

**`return null` and root agreement.** `fn find<T, F>(xs: T[:]) -> T?`
written as `for &x in xs { if F(x) { return &x; } } return null;` is
rejected ("returns disagree on the returned reference's root"): `null` is
treated as rooted at static data. `null` should be root-neutral. The
library is written in the form that works today (`var r: T? = null; …
r .= &x; … return r;`, one return at the end), so this is not blocking, but
the natural form is the one users will write.

**Generic type aliases** (`type set<K> = dictionary<K, bool>;`) do not
parse. Non-generic aliases of generic instances (`type float3 =
vec3<f32>;`) work and are all `vec` needs, so this is a convenience only.

**Constants in thread programs.** `let` globals of flat fixed type are
static data (§11.1); a `thread_fn` should be allowed to read them (`PI`,
`float3_0`), since they are not mutable shared memory. To be verified
against the checker's global ban.

**Identity of references** (spec TODO 0b) and **index of a reference in
its array** (`pool.index_of(r)`, a subtraction the checker can validate
via roots) are not needed by the library but are what `lru.goose` and
`graph.goose` work around with parallel index tables; they are cheap
builtins for a later round.

---

## 9. What the prototypes found

About forty small programs exercised the conventions above; the shapes that
matter are recorded here and become `test/stdlib*.goose` cases in phase 0.
What worked, unmodified: slice parameters over fixed, grow-only, limited and
string-literal arguments, with writes through them; `A&` and (after the
fix) untyped parameters mutating and growing every array kind including
`[>..<]` and `[..k]`; an explicit `<T>` parameter binding a reference;
`find` returning a `T?` into the input and being written through; a generic
`dictionary<K, V>` with slice keys, reference-returning `get`, rehash by
whole-array assignment, and UFCS through a reference variable; pushing into
a struct's resizable tail through a reference to the struct; an iterative
in-place quicksort over `T[:]` with a two-parameter comparator block; a
recursive generic quicksort with a forwarded function value; heaps over
`[>..<]`; `split` returning an array of slices into its argument; the
`hash` overload set across every scalar width without ambiguity; generic
vectors with aliases and elementwise math; block parameters binding
references; both arities of `sort` and `min` resolving unambiguously; the
word-count example of §5.2 end to end.

Compiler bugs, with the shape that triggers each:

| # | Shape | Symptom |
|---|---|---|
| B1 | `fn f(xs: i64[:]) -> i64[] { var out: i64[>..] = []; …; return out; }` then `let ys = f(a); ys[0]` | wrong element read (element base off by the length prefix) whenever `f` is not inlined: -O0 always, -O2 when used more than once. `g.append(f(a))` is correct; `vs.push(f(a))` into a `i64[][>..]` reads a zero-length element |
| B2 | the same with a `T[>..]` return: `let ys = f(a)`, `var ys = f(a); ys.push(…)`, `g.append(f(a))` | all correct, but `vs.push(f(a))` into `i64[][>..]` again yields length 0 |
| B3 | `fn map<U, T, F>(xs: T[:]) -> U[]` called as `map<i64>(a) { it * 10 }` | elements read as 0 even when inlined; at -O0 length 0 |
| B4 | `fn sum<T>(xs: T[:])` called with `&g`, `g: i64[>..]` | typechecks; generated C fails (`gs_rref` → `sl_i64`) |
| B5 | `count(dict[0])` where `dict: u8[..8][>..]` (slicing a limited-array element) | codegen assertion (`codegen.h:4625`) |
| B6 | `var o: u8[>..] = a;` with `a: u8[:]` | "unsupported resizable construction from sl_u8" (from a string literal it works) |
| B7 | `struct unit {}` used as an array element | the C has an empty struct |

B1–B3 are the blocking ones: every fresh-array result in §4.5 and §4.8 goes
through that path. B4 matters because `T[>..]&` parameters are everywhere in
real code (`bench/goose/blur_rows.goose`) and they will be handed to slice
functions constantly. The rest are edges the library can avoid until fixed.

Language-level findings already folded into the conventions: untyped
parameters and copies (§2.1); `null` fixes a return's root (§8.7); UFCS
passes by value (§2.5); a reference-returning call is not an lvalue (§2.5);
integer-literal-versus-float in generic bodies (§4.1); per-width overloads
for trait-like sets (§2.4); a generic struct literal infers its type
arguments from the literal's own values, not from the destination
(`slot { key, 0, false }` pushed into a `dictionary_slot<u8[:], i32>[>..]`
infers `i64` and is rejected; the library writes typed values); `[>..<]`
and slices, nested-tail references, generic aliases (§8.7).

---

## 10. Implementation plan

Each phase ends with the test suite green at -O0 and -O2 under MSVC and
clang, as `test/run_tests.ps1` already checks.

**Phase 0 — compiler.** Bugs B1–B4 (B5–B7 as they are reached); the
stdlib search path (§8.6); `abort`/`exit` (§8.3); `print`/`str`/`format`
for scalars and strings (§8.1); `default<T>()` (§8.2); the empty-array
declaration (§8.5); slices of `[>..<]` (§8.7); the tail representation
(§8.7); and, if question 1 says yes, the passing rule with `copy`. Tests:
`test/stdlib_returns.goose` covering every receiver shape of §7.3 for
non-inlined generic results (the regression net for B1–B3), plus one test
per language change.

**Phase 1 — `std`.** Everything in §4 except the extern-backed
`parse_flt`, which carries a Goose fallback. `test/stdlib_std.goose` with
a blessed `expected/` output, one function per section; `test/errors_tc/`
cases for the intended compile errors (`sort` on a `let` array,
`remove_at` on a grow-only array). Benchmarks: `bench/goose/strlist.goose`
and `words.goose` gain `_std` variants written with `split`/`each_split`
and `sort`, run through `bench/run_bench.ps1`, and must land within noise
of the hand-written rows — that is the acceptance test for "zero cost".

**Phase 2 — `dictionary`, `vec`.** A `words_dictionary.goose` benchmark row
against the hand-rolled table in `words.goose` and `lru.goose`'s map — the
load factor and the cached-hash question (§5.1) get decided by that
measurement. `vec` is small and independent; `particles.goose` gains a
`float3` row.

**Phase 3 — `extern fn`, `math`, `os`, printing of aggregates.** The extern
form (§8.4) with `runtime_os.h`; `math`; `os`; `parse_flt` over `strtod`;
`print` of aggregates and user `format` overloads. `test/stdlib_os.goose`
writes and reads a temp file, reads its own arguments, and checks `clock()`
is monotonic.

**Documentation.** `docs/stdlib.md`, a reference in the layout of §4–§7 with
one example per function group, maintained by hand; a `--doc` mode that
extracts it from the source comments is a later convenience. The spec's §12
gets a pointer here, and A.1/A.3 gain a "with the library" line.

---

## 11. Deliberately not included

Each with the loop that replaces it, so the omission is a documented idiom
rather than a gap:

* `zip`/`map2`/`each2`: `for i in min(a.len, b.len) { … a[i] … b[i] … }`.
  Without tuples there is nothing for `zip` to return.
* `reduce` (fold without a seed): `fold(xs[1..], xs[0]) { … }`.
* `reversed` copies, `enumerate`: `each_rev`, and `for x, i in xs`.
* `product`, `average`: two-line folds.
* `first`/`top`: `xs[0]`, and `last` exists only because `xs[xs.len - 1]`
  is the one everybody gets wrong by one.
* `flatten`, `partition`, `filter_indices`, `find_or_push`, `insert_ordered`:
  each is `filter`/`lower_bound`/`insert_at` plus a line.
* `keys`/`values` of a dictionary: `each` plus a push.
* String `concat` of many pieces, `repeat`, `pad`, `capitalize`, `escape`:
  `str(a, b, c)`, `push_n`, `format_int`'s width, and a loop.
* `lines`/`words`: `each_split(s, '\n')` and `each_split(s, ' ')` plus
  `trim`; a whitespace-class tokenizer belongs in the user's lexer.
* Multiple-resizable containers (a deque, an owning string dictionary): the
  language has one resizable per struct (§3.4); idioms in §5.3.
* Binary serialization: Goose data is already its own serialized form
  (`bench/design.md`); `read_file` into a `u8[>..]` and spec TODO 16's
  whole-region copy are the path, and typed `read_u32_le`-style accessors
  can wait for a format that needs them.
* Date/time formatting, directories, subprocesses, networking: `os` grows
  when a program needs them, one extern at a time.
* Thread helpers: `thread_fn`s take no function values and no generics
  (§11.2), so a `parallel_for` cannot be written in the library today. A
  `wait_all(ids: i64[:])` loop is one line. Revisit with spec TODO 9.

---

## 12. Questions (round 3)

1. **Passing by size class** (§8.7): adopt it in the reading given there?
   Specifically: (a) `copy(x)` as the spelling, and `move(x)` now or later;
   (b) where fixed-but-large values land — accept, lint above a size, or
   make limited arrays copy-explicit; (c) whether the implicit reference
   binding stops at call sites, `let` and `=`, or also covers
   reference-typed field initializers; (d) `push(v)` needing `copy(v)` for
   a non-fixed lvalue while `append(v)` does not; (e) untyped and `<T>`
   parameters binding non-fixed lvalues by reference rather than erroring.
2. **Tail representation** (§8.7): the frame-object layout with the tail's
   own header, and bytes-on-stack kept for variable prefixes and
   resizable-class ADTs — the direction you had in mind?
3. **`[>..<]` slices**: error at the shrink citing the slice's creation and
   its next use, loops conservative, no stores into containers — as worded?
4. **`format` details**: `format_int`/`format_uint`/`format_flt`,
   `format_replaced`, `push_utf8`, and user `format` overloads extending the
   builtin — anything to rename?
