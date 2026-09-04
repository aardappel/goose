# The Goose standard library

Five modules under `stdlib/`, found by `import std;` (and `dictionary`,
`vec`, `math`, `os`) wherever the compiler was built from a source tree, or
through `--stdlib <dir>` / `GOOSE_STDLIB`. Everything is written in Goose
except the C behind `os` (`src/runtime/runtime_os.h`) and libm behind
`math`, both reached through `extern fn` (spec §7.10). The design and its
rationale are in `design/stdlib_design.md`; this is the reference.

Conventions that hold throughout:

* A function that reads or mutates elements in place takes `xs: T[:]`; every
  array kind and every slice coerces to it: `sort(arr)`, `sort(arr[1..])`.
* A function that changes the length takes `xs: A&`, the whole array by
  reference, and instantiates for whatever array kind it is given, provided
  that kind has the operations used (`remove_at` on a grow-only array is a
  compile error naming the missing `pop`).
* Nothing non-fixed is taken by value (spec §4.1): `f(xs)` binds by
  reference; a function wanting its own copy says `copy(xs)`.
* Fresh arrays come back as `T[>..]`, built straight into the caller's
  destination: `let ys = filter(xs) { it > 0 };`, `out.append(map(xs) { … })`.
* "Maybe an element" is a `T?` — a reference into the input, null for none;
  narrow with `if`/`guard`. Positions are `i64`, `-1` for none. Two-outcome
  scalars return a trailing `bool`: `let v, ok = parse_int(s);`.
* Blocks: `xs.find() { it > 3 }`, `sort(xs) { a, b => a.age < b.age }`. A
  comparator is always "a goes before b". Sorting, heap and extremum
  functions come with and without a block; without one means `<`.
* Text output goes into a `u8[>..]&` builder the caller passes; `str(…)` is
  the fresh-string form.
* UFCS applies: `xs.sort()`, `d.insert(k, v)`, `r.rand_int(6)`.
* A function taking an element *by value* (`push_n`, `insert_at`, `fill`,
  `heap_push`) cannot take one that contains self-relative references, which
  no copy can carry (spec §3.9); build those in place.
* The names here live in the program's one namespace, so a local called
  `fill` or `count` shadows the library's and the error lands at the call,
  not at the declaration.

## std

### Scalars and bits

```goose
fn min<T>(a: T, b: T) -> T          fn max<T>(a: T, b: T) -> T
fn clamp<T>(x: T, lo: T, hi: T) -> T
fn abs<T>(x: T) -> T                // integers; abs(f32) and abs(f64) overloads
fn sign<T>(x: T) -> T               // -1, 0, 1 at x's type; f32/f64 overloads
fn lerp(a: f64, b: f64, t: f64) -> f64      // and f32
fn next_pow2(x: i64) -> i64         // smallest power of two >= max(x, 1)
fn popcount(x: u64) -> i64
fn clz(x: u64) -> i64               // 64 for 0
fn ctz(x: u64) -> i64               // 64 for 0
fn swap<T>(a: T&, b: T&)            // swap(x, y)
```

### Hashing

```goose
fn hash(x: i8) -> u64               // ... one overload per integer type
fn hash(x: bool) -> u64             fn hash(x: f32) -> u64      fn hash(x: f64) -> u64
fn hash(s: u8[:]) -> u64            // FNV-1a over the bytes; any u8 array coerces
fn hash_combine(seed: u64, h: u64) -> u64
```

A user key type provides its own overload, which `dictionary` picks up:

```goose
struct key { a: i64, b: i64 }
fn hash(k: key) -> u64 { hash_combine(hash(k.a), hash(k.b)) }
```

### Random numbers

```goose
struct rng { s: u64 }                    // splitmix64; the seed is the state
fn rand_u64(r: rng&) -> u64
fn rand_int(r: rng&, n: i64) -> i64      // uniform in [0, n)
fn rand_flt(r: rng&) -> f64              // uniform in [0, 1)
fn shuffle<T>(xs: T[:], r: rng&)         // Fisher-Yates
```

```goose
var r = rng { os.random_seed() };   // or any fixed seed
let d = r.rand_int(6);
```

### Arrays: reading and searching

```goose
fn each_rev<T, F>(xs: T[:])                    // F(x), last to first
fn each_chunk<T, F>(xs: T[:], n: i64)          // F(chunk: T[:], i); the last chunk may be short
fn find<T, F>(xs: T[:]) -> T?                  // first x with F(x), as a reference
fn find_index<T, F>(xs: T[:]) -> i64
fn position<T>(xs: T[:], v: T) -> i64          // first i with xs[i] == v
fn contains<T>(xs: T[:], v: T) -> bool
fn any<T, F>(xs: T[:]) -> bool                 fn all<T, F>(xs: T[:]) -> bool
fn count<T, F>(xs: T[:]) -> i64                // number of x with F(x)
fn min<T>(xs: T[:]) -> T                       // asserts xs.len > 0; min(xs) { a, b => ... } too
fn max<T>(xs: T[:]) -> T
fn min_by<T, F>(xs: T[:]) -> i64               // index of the smallest F(x); -1 if empty
fn max_by<T, F>(xs: T[:]) -> i64
fn last<T>(xs: T[:]) -> T&                     // asserts xs.len > 0
fn lower_bound<T, F>(xs: T[:]) -> i64          // first i with !F(xs[i]); F true on a prefix
fn binary_search<T>(xs: T[:], v: T) -> i64, bool   // sorted by <: position (or insertion point), found
fn find<T>(xs: T[:], sub: T[:]) -> i64         // first occurrence of a subsequence, -1 if none
fn rfind<T>(xs: T[:], sub: T[:]) -> i64
fn starts_with<T>(xs: T[:], p: T[:]) -> bool   fn ends_with<T>(xs: T[:], p: T[:]) -> bool
```

`find`/`rfind`/`starts_with`/`ends_with` are generic over the element, so
they are the string functions too: `find(line, "://")`. (`position` rather
than `index_of`, which is the builtin turning an element reference into its
index.)

```goose
let big = xs.find() { it > 100 };
if big { print(big); }
var row = 0;
each_chunk(pixels, width) { line, y => row += line.len; };
```

### Arrays: transforming

```goose
fn map<T, F>(xs: T[:])                          // -> U[>..], U the block's result type
fn filter<T, F>(xs: T[:]) -> T[>..]
fn fold<T, A, F>(xs: T[:], acc: A) -> A         // acc = F(acc, x)
fn sum<T>(xs: T[:]) -> T                        // from default<T>()
fn concat<T>(a: T[:], b: T[:]) -> T[>..]
```

```goose
let squares = xs.map() { it * it };
let evens = xs.filter() { it % 2 == 0 };
let total = fold(xs, 0.0) { a, x => a + x };
```

### Arrays: in place

```goose
fn fill<T>(xs: T[:], v: T)
fn copy_into<T>(dst: T[:], src: T[:])           // equal lengths; first to last
fn reverse<T>(xs: T[:])
fn sort<T>(xs: T[:])                            // by <; sort(xs) { a, b => ... } by the block
fn stable_sort<T>(xs: T[:])                     // merge sort; one temporary of xs.len elements
fn to_lower(s: u8[:])                           // ASCII, in place
fn to_upper(s: u8[:])
```

`sort` is a quicksort with median-of-three pivots, insertion sort below 16
elements and an explicit range stack; unstable, in place, no allocation.
A `let` array is not sortable: the writes are the compile error.

### Arrays: changing the length

```goose
fn push_n<A, T>(xs: A&, v: T, n: i64)           // n pushes
fn insert_at<A, T>(xs: A&, i: i64, v: T)        // shifts [i..) up; needs push
fn remove_at<A>(xs: A&, i: i64) -> T            // shifts down; needs pop
fn swap_remove<A>(xs: A&, i: i64) -> T          // O(1), reorders
fn retain<A, F>(xs: A&)                         // keeps x with F(x), in order; needs resize
fn dedup<A>(xs: A&)                             // drops adjacent duplicates
fn heap_push<A, T>(xs: A&, v: T)                // min-heap by <; block forms of all three
fn heap_pop<A>(xs: A&) -> T
fn make_heap<A>(xs: A&)                         // O(n)
```

A `[>..<]` is the natural heap and queue container; a limited `[..k]` field
works as well.

```goose
var q: i64[>..<] = [];
q.heap_push(5); q.heap_push(1);
let smallest = q.heap_pop();
```

### Strings

```goose
fn format_int(out: u8[>..]&, v: i64, base: i64, width: i64, padc: u8)   // base 2..36, right-aligned
fn format_uint(out: u8[>..]&, v: u64, base: i64, width: i64, padc: u8)
fn format_flt(out: u8[>..]&, v: f64, decimals: i64)        // fixed decimals, rounded half up
fn parse_int(s: u8[:]) -> i64, bool                        // optional sign, whole string
fn parse_int(s: u8[:], base: i64) -> i64, bool
fn parse_flt(s: u8[:]) -> f64, bool                        // sign, fraction, exponent
fn compare(a: u8[:], b: u8[:]) -> i64                      // bytewise: -1, 0, 1
fn trim(s: u8[:]) -> u8[:]                                 // ASCII whitespace; a sub-slice
fn trim_start(s: u8[:]) -> u8[:]                           fn trim_end(s: u8[:]) -> u8[:]
fn split(s: u8[:], sep: u8) -> u8[:][>..]                  // slices into s; empty parts kept
fn each_split<F>(s: u8[:], sep: u8)                        // F(part); no array built
fn each_split<F>(s: u8[:], sep: u8[:])
fn join<T>(out: u8[>..]&, parts: T[:], sep: u8[:])         // T any u8 array/slice type
fn format_replaced(out: u8[>..]&, s: u8[:], old: u8[:], with: u8[:])
fn each_utf8<F>(s: u8[:])                                  // F(codepoint); malformed bytes give 0xFFFD
fn push_utf8(out: u8[>..]&, cp: i64)
```

Plain rendering of scalars and strings is the builtin `format`/`str`/
`print`; these add control and parsing. Strings are `u8` arrays: input is
`u8[:]`, output a `u8[>..]&` builder, storage `u8[]`/`u8[..k]`.

```goose
var line: u8[>..] = [];
format(line, "x=");
format_int(line, x, 16, 8, '0');
each_split(text, '\n') { handle(trim(it)); };
let n, ok = parse_int(trim(field));
```

## dictionary

```goose
struct dictionary<K, V> { count: i64, slots: dictionary_slot<K, V>[>..<] }
fn get<K, V>(d: dictionary<K, V>&, key: K) -> V?               // reference to the value, or null
fn contains<K, V>(d: dictionary<K, V>&, key: K) -> bool
fn insert<K, V>(d: dictionary<K, V>&, key: K, val: V) -> bool   // overwrites; true if new
fn get_or_insert<K, V>(d: dictionary<K, V>&, key: K, v0: V) -> V&
fn update<K, V, F>(d: dictionary<K, V>&, key: K, v0: V)         // F(val&); inserts v0 first if absent
fn remove<K, V>(d: dictionary<K, V>&, key: K) -> bool
fn clear<K, V>(d: dictionary<K, V>&)
fn reserve<K, V>(d: dictionary<K, V>&, n: i64)                  // room for n entries without a rehash
fn each<K, V, F>(d: dictionary<K, V>&)                          // F(key, val&); order unspecified
```

Open addressing with linear probing, power-of-two capacity, backward-shift
deletion. Keys and values are fixed-size; a key type needs `hash` and `==`.
Strings are keyed as `u8[:]` slices into text the caller keeps, or as inline
`u8[..k]`. A set is `dictionary<K, bool>`.

The slot array is grow-shrink, so a reference into it — a `get` or
`get_or_insert` result, an `each` binder — may not be in scope at the next
`insert`, `update`, `remove` or `clear`; the checker reports it (spec §5.2).
Bind such references in their own block, or use `update`:

```goose
var counts = dictionary<u8[:], i32> {};
each_split(text, ' ') { counts.update(it, 0) { it += 1; } };
counts.each() { w, n => if n > 100 { print(w, " ", n); } };
block { let n = counts.get("the"); if n { print(n); } }
```

## vec

```goose
struct vec2<T> { x: T, y: T }       struct vec3<T> { x: T, y: T, z: T }
struct vec4<T> { x: T, y: T, z: T, w: T }
type float2/float3/float4 = vec*<f32>   double2/3/4 = vec*<f64>   int2/3/4 = vec*<i32>
let float3_0 = float3 { 0.0, 0.0, 0.0 };  // _0 and _1 for all nine aliases

fn dot<T>(a: vec3<T>, b: vec3<T>) -> T          // all sizes
fn cross<T>(a: vec3<T>, b: vec3<T>) -> vec3<T>
fn length_sq<T>(v: vec3<T>) -> T
fn length<T>(v: vec3<T>) -> T                   // float vectors
fn normalize<T>(v: vec3<T>) -> vec3<T>
fn distance<T>(a: vec3<T>, b: vec3<T>) -> T
fn lerp<T>(a: vec3<T>, b: vec3<T>, t: T) -> vec3<T>
fn xy<T>(v: vec3<T>) -> vec2<T>                 // also xy(vec4), xyz(vec4)
```

Elementwise `+ - * /` are the language's: `a + b`, `p - q`. Per-component
`min`/`max` is written out (`min(a.x, b.x)`).

## math

libm, both widths (`sqrt(x)` picks `sqrt` or `sqrtf` by the argument's
type): `sqrt sin cos tan asin acos atan atan2 exp log log2 log10 pow floor
ceil round trunc`, returning floats as C does (`as i64` converts). Plus:

```goose
let PI = 3.141592653589793;   let TAU = 6.283185307179586;
fn radians(deg: f64) -> f64   fn degrees(rad: f64) -> f64
fn is_nan(x: f64) -> bool     fn is_inf(x: f64) -> bool
```

## os

A deliberately thin layer over `src/runtime/runtime_os.h`:

```goose
fn read_file(path: u8[:], out: u8[>..]&) -> bool     // appends the whole file
fn write_file(path: u8[:], data: u8[:]) -> bool
fn append_file(path: u8[:], data: u8[:]) -> bool
fn file_exists(path: u8[:]) -> bool
fn delete_file(path: u8[:]) -> bool
fn read_line(out: u8[>..]&) -> bool                  // stdin, newline stripped; false at end
fn read_stdin(out: u8[>..]&)                         // everything until end of input
fn write_stdout(s: u8[:])   fn write_stderr(s: u8[:])   fn flush_stdout()
fn arg_count() -> i64       fn arg(i: i64, out: u8[>..]&)
fn args() -> u8[][>..]                               // argument 0 is the program
fn env(name: u8[:], out: u8[>..]&) -> bool
fn time() -> f64                                     // seconds since the epoch
fn clock() -> f64                                    // monotonic, high resolution
fn time_ns() -> i64         fn clock_ns() -> i64
fn sleep(seconds: f64)      fn sleep_ms(ms: i64)
fn random_seed() -> u64                              // entropy, for rng
```

`exit(code)` and `abort(msg)` are builtins, since the checker knows they
diverge. Directory listing, subprocesses and networking are not in v1; they
arrive as `extern fn`s when a program needs them.
