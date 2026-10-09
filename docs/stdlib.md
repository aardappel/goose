# The Goose standard library

The standard library has ten modules under `stdlib/`: `std`, `dictionary`,
`vec`, `math`, `os`, `binary`, `audio`, `gfx`, `physics`, and `ui`. Import each module by name,
for example `import std;`. The compiler locates the library in its source
tree; use `--stdlib <dir>` or `GOOSE_STDLIB` to select another location.
Everything is written in Goose except the C behind `os`
(`src/runtime/runtime_os.h`), libm behind `math`, the graphics layer behind
`gfx` (`src/gfx/`), the physics layer behind `physics` (`src/physics/`) and
the ui layer behind `ui` (`src/ui/`), and the PCM mixer behind `audio`
(`src/audio/`), all reached through `extern fn` (spec
§7.10). The design and its rationale are in `design/stdlib_design.md` (and
`design/gfx.md` for `gfx`, `design/physics.md` for `physics`, `design/ui.md`
for `ui`, and `design/audio.md` for `audio`); this is the reference.

For **text rendering, fonts and game HUDs**, start with
[`ui`](#text-rendering-and-game-huds), which renders over `gfx`. For external
binary file formats, use [`binary`](#binary); `from_bytes` reads Goose's own
serialization format.

The library uses these conventions:

* A function that reads or mutates elements in place takes `xs: T[:]`; every
  array kind and every slice coerces to it: `sort(arr)`, `sort(arr[1..])`.
* A function that changes the length takes `xs: A&`, the whole array by
  reference, and instantiates for whatever array kind it is given, provided
  that kind has the operations used. A grow-only array shrinks through the
  reference wherever nothing in the caller points into it (spec §5.1), so
  `remove_at(xs, i)` works on a `[>..]` local; the checker rejects the
  call if a live reference or slice could be invalidated.
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
  `heap_push`) cannot take one that contains self-relative references, because
  those values cannot be copied (spec §3.9). Construct them in place.
* The functions that move elements around (`swap_at`, `reverse`, `shuffle`,
  `sort`, `stable_sort`, `fill`, `copy_into`, `insert_at`, `remove_at`,
  `swap_remove`, `retain`, `dedup`, the heap functions) move the elements
  themselves, references included: `reverse(refs)` reorders an array of
  references and leaves what they point to alone, and an array of
  references to values that cannot be copied (spec §3.9) sorts too. They
  store whole elements with `.=`, as generic code may (spec §3.8). `swap(a,
  b)` exchanges the values `a` and `b` refer to. `remove_at`, `swap_remove`
  and `heap_pop` return a copy of the value removed, for a reference element
  its pointee's. A reference element passed by value (`insert_at`,
  `heap_push`) is written `&x`, which makes `T` the reference type. The
  checker does not yet let `stable_sort` sort an array of references or
  slices (`implementation.md` §10).
* The `std`, `dictionary`, `vec`, `math`, and `os` names are global; `binary`, `audio`,
  `gfx`, `physics`, and `ui` use their own namespaces. A local named `fill` or `count`
  shadows the corresponding global function, causing an error at a call; a
  global variable of such a name does not, since a call names the functions
  past it (spec §11.1).

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
fn stable_sort<T>(xs: T[:])                     // natural merge sort; one temporary of xs.len elements
fn to_lower(s: u8[:])                           // ASCII, in place
fn to_upper(s: u8[:])
```

`sort` is a pattern-defeating quicksort after pdqsort and Rust's ipnsort:
unstable (the order it leaves equal elements in is unspecified), in place,
no allocation. Its partition does not branch on the comparisons, a run of
equal keys costs a linear pass rather than n log n, and heapsort takes over
a range that partitioning keeps splitting badly, so the worst case is
O(n log n). `stable_sort` is a natural merge sort: it finds the runs
already in order (or in strictly descending order, which it reverses) and
merges them pass by pass between `xs` and a temporary of `xs.len` elements
on a fresh data stack. Both take a single pass over input that is in order
already or strictly descending, and neither allocates for it or for 20
elements or fewer. A comparator that is no strict weak ordering (`<=`, or
`<` over floats with NaNs) leaves an unspecified order, but always a
permutation of the input, and never makes either abort.

A `let` array can be sorted because `let` prevents rebinding, not element
writes. A `const` array or read-only slice cannot be sorted (spec §9.5).

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

The heap is binary, with the children of `i` at `2i + 1` and `2i + 2`. Its
sifts move a hole instead of swapping: the element being placed waits in a
local while each level moves one element into the hole, and is stored once
where the hole stops. `heap_pop` walks the hole from the root down to a leaf
along the child that goes first and then sifts the last element up from
there, as libstdc++ and Rust do, which takes about one comparison per level
where sifting it down from the root takes two. For a comparator that is a
strict weak order every operation leaves the same layout as the textbook
swapping sifts, ties included, so elements that tie come out in the same
order. A comparator that reads the heap while it runs sees one slot holding
a copy of its neighbour rather than the element waiting outside.

```goose
var q: i64[>..<] = [];
q.heap_push(5); q.heap_push(1);
let smallest = q.heap_pop();
```

### Strings

```goose
fn format_int(out: u8[>..]&, v: i64, base: i64, width: i64, padc: u8)   // base 2..36, right-aligned
fn format_uint(out: u8[>..]&, v: u64, base: i64, width: i64, padc: u8)
fn format_flt(out: u8[>..]&, v: f64, decimals: i64)        // fixed decimals, rounded half up; inf/-inf/nan as print writes them
fn parse_int(s: u8[:], base: i64 = 10) -> i64, bool        // optional sign, whole string
fn parse_flt(s: u8[:]) -> f64, bool                        // sign, fraction, exponent
fn format_uleb(out: u8[>..]&, v: i64)                     // LEB128 (3.6), 1-10 bytes
fn parse_uleb(s: u8[:]) -> i64, i64                       // value and byte count; 0, 0 if malformed
fn compare(a: u8[:], b: u8[:]) -> i64                      // bytewise: -1, 0, 1
fn trim(s: u8[:]) -> u8[:]                                 // ASCII whitespace; a sub-slice
fn trim_start(s: u8[:]) -> u8[:]                           fn trim_end(s: u8[:]) -> u8[:]
fn split(s: u8[:], sep: u8) -> const u8[:][>..]            // read-only slices into s; empty parts kept
fn split(s: u8[:], sep: u8[:]) -> const u8[:][>..]         // matched from the left; an empty sep: [s]
fn each_split<F>(s: u8[:], sep: u8)                        // F(part); no array built
fn each_split<F>(s: u8[:], sep: u8[:])
fn words(s: u8[:]) -> const u8[:][>..]                     // split at runs of ASCII whitespace; no empty words
fn each_word<F>(s: u8[:])                                  // F(word)
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
let cmd = words(input);                   // "take  the sword": ["take", "the", "sword"]
if cmd.len > 1 && cmd[0] == "take" { take(cmd[cmd.len - 1]); }
```

`format_uleb` and `parse_uleb` write and read the length prefix used by
serialized images (`design/serialization.md`). Writing the prefix separately
lets a program save a `bytes_of` view without first copying it into an image.
Reading the prefix tells a stream reader how many payload bytes to expect.

```goose
let payload = pool.bytes_of();            // a view, nothing copied
var head: u8[>..] = [];
format_uleb(head, payload.len);
write_file(path, head); append_file(path, payload);
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
Strings are keyed as `u8[:]` slices into text the caller keeps (`const u8[:]`
for literals or views of `const` data, §9.5), or as inline
`u8[..k]`. A set is `dictionary<K, bool>`.

The slot array is grow-shrink, so a reference into it — a `get` or
`get_or_insert` result, an `each` binder — must be done with before the next
`insert`, `update`, `remove` or `clear`; the checker reports it against the
mutation, naming the reference and where it was bound (spec §5.2). Finish
with the reference first, or use `update`, which does the lookup and the
change in one:

```goose
var counts = dictionary<const u8[:], i32> {};
each_split(text, ' ') { counts.update(it, 0) { it += 1; } };
counts.each() { w, n => if n > 100 { print(w, " ", n); } };
let n = counts.get("the");
if n { print(n); }               // the last use of n; counts is free again after it
```

It is liveness, not scope, that the checker asks about, so a reference whose
last use is before the mutation needs no block around it, and one still used
after the mutation is an error wherever it was declared. The key type is
`const u8[:]` because literals and the read-only views returned by `split`
require it (§9.5). A dictionary holding writable slices can use
`dictionary<u8[:], V>`, whether the underlying buffer is bound with `let`
or `var`.

A value can be a reference if it is optional, `Cell?` or a link into a
pool, `Cell&<u32 in cells>?`: the unused slots hold `default<V>()`, which a
non-optional reference does not have. `insert` over an existing key stores
the new reference rather than writing through the old one. `get` and
`get_or_insert` return the stored reference itself, and `update`'s and
`each`'s blocks receive it (`V?` and `V&` of a reference are that
reference, §3.8), loaded if relative: writing through it writes the
pointee, and replacing it takes an `insert`. `get` returns null for a null
value as for an absent key; `contains` tells them apart. Such a reference
points at its pointee rather than into the slot array, so it may stay live
across the next mutation:

```goose
var owner = dictionary<i64, Cell?> {};
owner.insert(7, cells[3]);
let c = owner.get(7);
owner.insert(7, cells[4]);       // cells[3] is unchanged, and c still refers to it
if c { c.v += 1; }
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

Elementwise `+ - * /` are the language's: `a + b`, `p - q`, and so is
unary minus on a float or signed integer vector: `-v`. `*` and `/`
also broadcast a scalar in either order: `v * 2.0`, `v / length(v)`,
`1.0 / v`; `v *= scale` and `v /= scale` update a vector in place. The
scalar converts to the component type, so an `f64` variable scaling a
`float3` needs an explicit `as f32`. Per-component
`min`/`max` is written out (`min(a.x, b.x)`).

## math

libm, both widths (`sqrt(x)` picks `sqrt` or `sqrtf` by the argument's
type): `sqrt sin cos tan asin acos atan atan2 exp log log2 log10 pow floor
ceil round trunc`, returning floats as C does (`as i64` converts). An
integer argument suits both widths, so it names one (`sqrt(n as f64)`);
`sqrt(n * 0.5)` is the `f64` one, as `sqrt(0.5)` is. Plus:

```goose
let PI = 3.141592653589793;   let TAU = 6.283185307179586;
fn radians(deg: f64) -> f64   fn degrees(rad: f64) -> f64
fn is_nan(x: f64) -> bool     fn is_inf(x: f64) -> bool
```

## os

A small interface to `src/runtime/runtime_os.h`:

```goose
fn read_file(path: const u8[:], out: u8[>..]&) -> bool     // appends the whole file
fn write_file(path: const u8[:], data: const u8[:]) -> bool
fn write_file_atomic(path: const u8[:], data: const u8[:]) -> bool   // the old file or all of the new
fn append_file(path: const u8[:], data: const u8[:]) -> bool
fn file_exists(path: const u8[:]) -> bool                  // a file, not a directory
fn delete_file(path: const u8[:]) -> bool
fn rename_file(from: const u8[:], to: const u8[:]) -> bool // replaces `to` atomically
fn make_dir(path: const u8[:]) -> bool                     // true if one is there afterwards
fn is_dir(path: const u8[:]) -> bool
fn delete_dir(path: const u8[:]) -> bool                   // an empty one
fn list_dir(path: const u8[:], out: u8[>..]&) -> bool      // appends each name and '\n', sorted
fn read_line(out: u8[>..]&) -> bool                  // stdin, newline stripped; false at end
fn read_stdin(out: u8[>..]&)                         // everything until end of input
fn write_stdout(s: const u8[:])   fn write_stderr(s: const u8[:])   fn flush_stdout()
fn arg_count() -> i64       fn arg(i: i64, out: u8[>..]&)
fn args() -> u8[:][>..]                              // indexable; argument 0 is the program
fn env(name: const u8[:], out: u8[>..]&) -> bool
fn resource_dir() -> u8[>..]                         // absolute directory, trailing separator
fn resource_dir(out: u8[>..]&) -> bool               // appends; unchanged on failure
fn time() -> f64                                     // seconds since the epoch
fn clock() -> f64                                    // monotonic, high resolution
fn time_ns() -> i64         fn clock_ns() -> i64
fn sleep(seconds: f64)      fn sleep_ms(ms: i64)
fn random_seed() -> u64                              // entropy, for rng
```

Paths are UTF-8 on every platform: on Windows the runtime converts them to
UTF-16 for the system's wide APIs. So are environment values, and the
arguments, which on Windows arrive through the ANSI code page first and so
keep only the characters it has. A path is refused, and the call returns
`false`, if it is longer than 4095 bytes, contains a NUL byte, or on Windows
is not valid UTF-8: cut short or read another way, it could name a
different file.

`write_file_atomic` is for files that must never be seen half-written, such
as saved games. It writes the data to a new file beside `path`, named after
it with a random tag and `.tmp` added, flushes that file to disk and renames
it over `path`. A crash or a failure at any point leaves `path` with either
its old contents or all of the new ones. On a failure the new file is
removed; after a crash it can remain, its name ending in `.tmp`.
`rename_file` is the same one-step replacement for a file the program wrote
itself, within one volume. On Windows no file can be replaced while another
program has it open, and virus scanners open files just written for a
moment, so both wait up to about a second for such a file before returning
`false`.

`list_dir` leaves out `.` and `..` and sorts the names bytewise, so a
listing does not depend on the file system. A name that cannot be one line
of UTF-8, a POSIX name with a newline in it or a Windows name that is not
valid UTF-16, is left out too. On a failure nothing is appended.

```goose
var names: u8[>..] = [];
if list_dir("saves", names) {
    each_split(names, '\n') { if it.len > 0 { load_save(str("saves/", it)); } };
}
if !write_file_atomic("saves/slot1.sav", image) { write_stderr("not saved\n"); }
```

### Application resources

Ordinary relative paths resolve against the process working directory, as
in C/C++. Goose does not change that directory when it starts a program.
For data distributed with the application, `resource_dir()` returns an
absolute UTF-8 directory with a trailing separator, ready to concatenate:

```goose
var wad: u8[>..] = [];
if !read_file(str(resource_dir(), "data/map.wad"), wad) {
    abort("cannot read map.wad");
}
```

In JIT runs the root is the entry source's directory, regardless of which
imported module calls the helper. In AOT programs it is the running
executable's directory, discovered at runtime rather than recorded at
compilation. In a conventional macOS `.app` bundle it is `Contents/Resources/`.
Deploy the data relative to that root; moving the executable and its data
together preserves resource lookup. An AOT program does not depend on its
original source tree or on the launcher's working directory.

The string-returning form aborts if the directory cannot be discovered;
the builder overload returns false and appends nothing. Native discovery
supports Windows, Linux and macOS. This directory is for shipped assets;
it is not promised writable. User-supplied paths, outputs and saves can
continue to use ordinary file operations and their chosen locations.

`exit(code)` and `abort(msg)` are builtins, since the checker knows they
diverge. Subprocesses and networking are not in v1; they arrive as
`extern fn`s when a program needs them.

## binary

Checked, little-endian reads of external formats, written entirely in Goose.
`import binary;` puts the API in namespace `binary`. Input is a `const u8[:]`;
returned byte slices and fixed-width names borrow that input without copying
or allocating. Their lifetimes and read-only access are checked normally.
This API does not interpret Goose serialization images (`from_bytes`).

### Reads at offsets

```goose
fn range_ok(data: const u8[:], at: i64, size: i64) -> bool
fn read_u16le(data: const u8[:], at: i64) -> u16, bool
fn read_i16le(data: const u8[:], at: i64) -> i16, bool
fn read_u32le(data: const u8[:], at: i64) -> u32, bool
fn read_bytes(data: const u8[:], at: i64, size: i64) -> const u8[:], bool
fn read_name(data: const u8[:], at: i64, width: i64) -> const u8[:], bool
```

Offsets and sizes are bytes. Negative values, truncated fields and offsets
past the end fail with zero or an empty slice and `false`; range checking
does not overflow on extreme offsets or sizes. An empty range at the end
is valid. Reads work at unaligned offsets on either host byte order.
`read_name` validates the entire fixed-width field and returns the bytes
before its first NUL, or all `width` bytes if there is no NUL. It does not
validate padding or interpret the bytes as a particular text encoding.

### Cursor and bounded subreaders

```goose
struct Reader { data: const u8[:], pos: i64 }
fn reader(data: const u8[:]) -> Reader
fn read_u16le(r: Reader&) -> u16, bool
fn read_i16le(r: Reader&) -> i16, bool
fn read_u32le(r: Reader&) -> u32, bool
fn read_bytes(r: Reader&, size: i64) -> const u8[:], bool
fn read_name(r: Reader&, width: i64) -> const u8[:], bool
fn skip(r: Reader&, size: i64) -> bool
fn subreader(r: Reader&, size: i64) -> Reader, bool
```

The cursor holds only its input and byte position. Every read returns its
own success flag. Success advances `pos` (by the full width for a name);
failure returns zero/empty and leaves `pos` unchanged, so a shorter read
can be tried next. Set `pos` to seek; a negative or out-of-range position
makes reads fail. Callers keep any file offsets or diagnostic context they
need. There is no sticky error state.

`subreader` consumes a bounded range from its parent and gives it a new
cursor starting at zero. Failure returns an empty reader and `false`,
leaving the parent in place. Keep the parent in scope while using its
child or returned slices; the current reference-result analysis can tie
their lifetime to the parent.

```goose
// data owns the file bytes. The first four bytes give a payload length.
var r = binary::reader(data);
let size, size_ok = binary::read_u32le(r);
if !size_ok { abort("missing payload length"); }
var payload, payload_ok = binary::subreader(r, size);
if !payload_ok { abort("truncated payload"); }
let version, version_ok = binary::read_u16le(payload);
let name, name_ok = binary::read_name(payload, 8);
if !version_ok || !name_ok { abort("truncated payload header"); }
// name borrows data. payload cannot read into the next record.
```

Use offset reads for random access and cursors for sequential records.
Applications still validate signatures, counts, indices and format-specific
limits after checking the read. The WAD loader in
[`31_mini_doom.goose`](../samples/31_mini_doom.goose) shows both styles.

## gfx

Graphics on SDL3's GPU API, which draws through Direct3D 12, Vulkan or Metal:
a window and its input, buffers, textures, samplers, pipelines, render and
compute passes, and reading results back. Optional: it needs a compiler built
with the `third_party/SDL` submodule, and a program using it links what `goose
--gfx-link msvc|cc` prints (a response file: `cl game.c @<it>`, `cc game.c -o
game @<it>`). A compiler without it still typechecks and generates C for such
a program; only running it in-process fails. Everything is in namespace `gfx`;
`samples/27_gfx_cube.goose` is a small complete program, `design/gfx.md` how
it works.

**Text rendering is provided by [`ui`](#text-rendering-and-game-huds)**,
including fonts, positioned text and noninteractive game HUDs. Use
`ui::label` or `ui::draw_text`, then `ui::render` over the gfx scene. Only
specialized artwork or rendering requirements need a separate text renderer.

### Shaders

```goose
// Code shaders share, in a global each names as a part.
let scene = """
    layout(set = 1, binding = 0) uniform Scene { mat4 view_proj; };
    """;

// A shader's source after its stage, "vert", "frag" or "comp", or a file,
// .vert, .frag or .comp, relative to this one.
let vs = embed_shader("vert", scene, """
    layout(location = 0) in vec3 a_pos;
    void main() { gl_Position = view_proj * vec4(a_pos, 1.0); }
    """);
let fs = embed_shader("lit.frag");
```

`embed_shader` is a builtin: the GLSL 450 shader is compiled when the program
is, into SPIR-V, MSL and HLSL at once, and the result is a `const u8[:]` of
static data to hand to `pipeline` or `compute_pipeline`. Its source is usually
a `"""` string (spec §2) written at the call, and may come in parts: each is a
string literal or a `let` or `const` global initialized with one, and they
join as lines, in order. A shader that does not compile is a compile error at
the call, pointing at the offending line of GLSL where there is one: in the
program, or in the shader's file. `#include "x.glsl"` resolves relative to the
shader's file, or to the program's for source written in it. The dialect is
cute_spirv's (`third_party/cute_spirv`): no doubles, no geometry or
tessellation stages, uniform blocks without instance names, and each storage
buffer block one runtime array (`buffer B { vec4 items[]; };`).

Resources go where SDL_GPU expects them, which the compiler checks:

| Stage | `set` | holds, each set's bindings numbered from 0 in this order |
|---|---|---|
| vertex | 0 | samplers, then storage textures, then storage buffers (read-only) |
| vertex | 1 | uniform blocks |
| fragment | 2 | samplers, then storage textures, then storage buffers (read-only) |
| fragment | 3 | uniform blocks |
| compute | 0 | samplers, then read-only storage textures, then read-only storage buffers |
| compute | 1 | read-write storage textures, then read-write storage buffers |
| compute | 2 | uniform blocks |

The `slot` of a `bind_*` call counts within one kind: the first storage buffer
is slot 0 however many samplers precede it. A compute shader's read-write
resources are given to `begin_compute` in binding order instead.

### Device, window and frames

```goose
fn open(title: const u8[:], width: i64, height: i64) -> bool        // and (..., flags)
fn open_headless(width: i64, height: i64) -> bool                   // no window; and (..., flags)
fn close()
fn frame() -> bool          // ends the frame drawn since the last call; false once asked to close
fn quit()                   // frame() returns false next
fn flush()                  // submit everything and wait for the GPU
fn error() -> u8[>..]       // why the last failing call failed
fn check()                  // abort if the program has misused gfx
fn driver() -> u8[>..]      // "direct3d12", "vulkan" or "metal"
fn set_title(title: const u8[:])
fn screen() -> Texture      fn screen_depth() -> Texture      fn screen_size() -> int2
fn time() -> f64            fn delta_time() -> f64            fn frame_count() -> i64
```

Flags for `open`: `WINDOW_RESIZABLE`, `WINDOW_HIDDEN`, `WINDOW_FULLSCREEN`,
`NO_VSYNC`, `DEBUG` (validation layers where installed; on in a debug build of
the layer). With `GOOSE_GFX_HEADLESS=1` in the environment `open` opens no
window, as the test runners use it. The screen is an RGBA8 texture of the
window's size in pixels, with a depth texture: a frame draws into it, and
`frame()` shows it. A window is as wide and high as `open` says in the
display's own coordinates, and has every pixel the display has there: on a
high-density display more than that, which `screen_size()` counts and the
mouse is reported in.

```goose
guard gfx::open("demo", 1280, 720) else { abort(str("gfx: ", gfx::error())); }
while gfx::frame() {
    gfx::begin_screen(float4 { 0.1, 0.1, 0.1, 1.0 });
    // bind, uniforms, draw ...
    gfx::end_pass();
}
gfx::close();
```

Errors: a call that can fail for reasons outside the program returns false or
a zero handle, and `error()` says why. A call the program should not have made
-- a draw with no pipeline bound, a texture the shader samples left unbound, a
released handle, a uniform struct of the wrong size -- is printed as it
happens, skipped, and aborts the program at the next `frame()`, `check()`,
read back or `close()`.

### Input

```goose
fn key_down(name: const u8[:]) -> bool        // SDL's key names: "A", "Space", "Left", "Escape"
fn key_pressed(name: const u8[:]) -> bool     // went down since the last frame()
fn key_released(name: const u8[:]) -> bool
fn mouse_down(button: i64) -> bool            // MOUSE_LEFT, MOUSE_MIDDLE, MOUSE_RIGHT
fn mouse_pressed(button: i64) -> bool         fn mouse_released(button: i64) -> bool
fn mouse_pos() -> float2    fn mouse_delta() -> float2      // in the screen's pixels
fn mouse_wheel() -> f32     fn mouse_in_window() -> bool    // over the window, or dragging from it
fn focused() -> bool                                      // keyboard focus at the last frame()
fn set_mouse_relative(on: bool) -> bool    fn mouse_relative() -> bool
fn set_cursor_visible(visible: bool) -> bool    fn cursor_visible() -> bool
fn inject_key(name: const u8[:], down: bool) -> bool           // as if typed, seen at the next frame()
fn inject_mouse(x: f32, y: f32, button: i64, down: bool)       // to pixel x, y; button 0 only moves
fn inject_text(text: const u8[:]) -> bool                      // an EVENT_TEXT per character
fn inject_focus(focused: bool) -> bool                        // simulate focus at the next frame()
fn events() -> Event[>..]           // the last frame's input, in the order it came
fn text_input(on: bool)             // typed text as EVENT_TEXT: off by default
fn scancode(name: const u8[:]) -> i64       fn key_name(scancode: i64) -> u8[>..]
fn clipboard() -> u8[>..]           fn set_clipboard(text: const u8[:]) -> bool
fn open_count() -> i64              // times gfx was opened: older handles are gone
```

The state functions say where things are at a frame; `events()` says what
happened between two, which text editing needs: key presses and releases
with their scancode, the character their key types (`keycode`) and the
`MOD_*` keys held, typed characters, mouse motion, buttons (`MOUSE_*`, with
X1 and X2) with their click count, and the wheel. Headless, the clipboard is
gfx's own rather than the system's.

`mouse_pos` is the mouse's place from the window's opening, before it
first moves. `mouse_in_window` is true while the mouse is over the window,
and while it holds a button pressed there, as SDL tells it: from the
mouse's first move over a window that opened under it, at the latest.
Once it leaves, `mouse_pos` stays where it left, off the screen: edge
scrolling asks both. Headless, the mouse is in the window when the last
position injected is on the screen.

`set_mouse_relative(true)` confines the mouse to the window and hides its
cursor. `mouse_delta()` continues to report movement past the window edges,
in screen pixels: use it for first-person camera turns, rather than
`mouse_pos()`. Setting a different mode discards queued motion and clears
the current delta so a previously accumulated move cannot turn the camera.
Calling it again with the same mode preserves motion. It returns false on
failure, including a window without keyboard focus; `error()` gives the reason.

`set_cursor_visible(false)` hides the cursor for programs drawing their own.
Its preference applies outside relative mode: relative mode always hides the
cursor, and `cursor_visible()` reports that effective state inside the window.
The preference is restored when relative mode is disabled. Hiding a cursor
without focus fails; showing one always works while gfx is open.

`focused()` reports keyboard focus, which differs from `mouse_in_window()`.
`events()` includes `EVENT_FOCUS_GAINED` and `EVENT_FOCUS_LOST` in order with
other input; these kinds have no payload. Losing focus clears held keys and
buttons and mouse/wheel deltas, disables relative mode, and resets the cursor
to visible. Input received while unfocused is ignored. Focus gain leaves
relative mode disabled so a game can pause on focus loss and wait for a click
before recapturing. Held keys/buttons also get released edges and synthetic
up events before `EVENT_FOCUS_LOST`, so event-driven UI cannot retain a held
key or a drag across an application switch. `close()` also releases the
mouse and restores the cursor.

Headless gfx starts focused, with a visible cursor and relative mode disabled.
The setters simulate their state without touching the system mouse.
`inject_focus` queues the same transitions for testing; it does not change
operating-system focus. Closing and reopening resets all these states.

### Buffers

```goose
struct Buffer { id: u32 }
fn buffer(usage: i64, data: const u8[:]) -> Buffer             // and (usage, size, data): zeros after
fn vertex_buffer(data: const u8[:]) -> Buffer                    // data from bytes_of(vertices)
fn index_buffer(data: const u8[:]) -> Buffer
fn update_buffer(b: Buffer, data: const u8[:]) -> bool           // and (b, offset, data)
fn read_buffer<T>(b: Buffer, out: T[>..]&, count: i64) -> bool   // appends count elements of T
fn read_buffer(b: Buffer, offset: i64, out: u8[:]) -> bool
fn buffer_size(b: Buffer) -> i64
fn release(b: Buffer)
```

Usage flags: `BUFFER_VERTEX`, `BUFFER_INDEX`, `BUFFER_INDIRECT`,
`BUFFER_STORAGE` (read by vertex and fragment shaders), `BUFFER_COMPUTE_READ`,
`BUFFER_COMPUTE_WRITE`. Updates and read backs happen between passes, in
order with the draws around them; a read back waits for the GPU.

### Textures and samplers

```goose
struct Texture { id: u32 }
struct TextureDesc { kind: i32 = 0, format: i32 = 1, usage: i32 = 1, width: i32, height: i32,
                     depth: i32 = 1, mips: i32 = 1, samples: i32 = 1 }   // TEXTURE_2D, RGBA8, SAMPLED
struct Region { mip: i32 = 0, layer: i32 = 0, x: i32 = 0, y: i32 = 0, z: i32 = 0,
                w: i32 = 0, h: i32 = 0, d: i32 = 0 }    // w == 0: the whole mip
fn texture(desc: TextureDesc) -> Texture                 // mips 0: the full chain
fn texture2d(width: i64, height: i64) -> Texture         // and (..., format, usage)
fn render_target(width: i64, height: i64, format: i32) -> Texture   // COLOR_TARGET | SAMPLED
fn depth_target(width: i64, height: i64) -> Texture                 // DEPTH, and SAMPLED
fn texture3d(width, height, depth, format, usage)        fn texture_cube(size, format, usage)
fn texture_array(width, height, layers, format, usage)
fn load_texture(path: const u8[:], flags: i64) -> Texture   // PNG or BMP; LOAD_MIPS, LOAD_SRGB
fn update_texture(t: Texture, data: const u8[:]) -> bool     // and (t, region, data)
fn read_texture<T>(t: Texture, out: T[>..]&) -> bool         // appends mip 0 as T: u8, float4, ...
fn read_texture(t: Texture, region: Region, out: u8[:]) -> bool
fn read_pixels(t: Texture) -> u8[>..]                        // mip 0's bytes, top row first
fn save_png(t: Texture, path: const u8[:]) -> bool           // RGBA8 or BGRA8
fn screenshot(path: const u8[:]) -> bool                     // the screen, as shown
fn generate_mips(t: Texture)
fn texture_info(t: Texture) -> TextureDesc
fn release(t: Texture)

struct Sampler { id: u32 }
struct SamplerDesc { min_filter: i32 = 1, mag_filter: i32 = 1, mip_filter: i32 = 1,
                     wrap_u: i32 = 0, wrap_v: i32 = 0, wrap_w: i32 = 0,
                     max_anisotropy: f32 = 1.0, compare: i32 = 0 }  // LINEAR, REPEAT
fn sampler(desc: SamplerDesc) -> Sampler      fn sampler(filter: i32, wrap: i32) -> Sampler
fn shadow_sampler() -> Sampler                // for sampler2DShadow
fn release(s: Sampler)
```

Kinds `TEXTURE_2D`, `TEXTURE_2D_ARRAY`, `TEXTURE_3D`, `TEXTURE_CUBE` (faces
+x, -x, +y, -y, +z, -z as layers 0 to 5). Usage `SAMPLED`, `COLOR_TARGET`,
`DEPTH_TARGET`, `STORAGE_READ`, `COMPUTE_READ`, `COMPUTE_WRITE`,
`COMPUTE_READ_WRITE`; a multisampled texture is a target only. Formats
`RGBA8`, `BGRA8`, `RGBA8_SRGB`, `R8`, `RG8`, `RGBA16F`, `RGBA32F`, `R16F`,
`RG16F`, `R32F`, `RG32F`, `R32UI`, `RGBA8UI`, `RGB10A2`, `RG11B10F`, and
`DEPTH16`, `DEPTH24`, `DEPTH32F`, `DEPTH24_STENCIL8`, `DEPTH` (32-bit float
where it can also be sampled). Filters `NEAREST`, `LINEAR`; wraps `REPEAT`,
`MIRROR`, `CLAMP`; compares `COMPARE_LESS` and the like.

### Pipelines and drawing

```goose
struct Pipeline { id: u32 }
struct PipelineDesc { primitive: i32 = 0, cull: i32 = 0, clockwise: bool = false,
                      wireframe: bool = false, depth_test: bool = false,
                      depth_write: bool = false, depth_compare: i32 = 2, blend: i32 = 0,
                      instance_location: i32 = -1, depth_bias: f32 = 0.0,
                      depth_bias_slope: f32 = 0.0, formats: u8[16] = [0; 16] }
fn pipeline(vs: const u8[:], fs: const u8[:]) -> Pipeline      // and (vs, fs, desc)
fn release(p: Pipeline)

struct PassDesc { color: Texture[4], resolve: Texture[4], depth: Texture, layer: i32 = 0,
                  mip: i32 = 0, clear_color: bool = true, clear_depth: bool = true,
                  color_value: float4 = float4 { 0.0, 0.0, 0.0, 1.0 }, depth_value: f32 = 1.0 }
fn begin_pass(desc: PassDesc) -> bool
fn begin_pass(target: Texture, depth: Texture, clear: float4) -> bool   // Texture { 0 }: no depth
fn begin_screen(clear: float4) -> bool
fn end_pass()
fn bind(p: Pipeline)
fn bind_vertex_buffer(b: Buffer)                 // and (slot, b, offset): slot 1 per instance
fn bind_index_buffer(b: Buffer, index_size: i64) // 2 or 4 bytes; and (b, size, offset)
fn bind_texture(stage: i64, slot: i64, t: Texture, s: Sampler)     // VERTEX, FRAGMENT, COMPUTE
fn bind_storage_texture(stage: i64, slot: i64, t: Texture)
fn bind_storage_buffer(stage: i64, slot: i64, b: Buffer)
fn uniforms<T>(stage: i64, slot: i64, u: T)     // a flat struct as a uniform block
fn push_uniforms(stage: i64, slot: i64, data: const u8[:])
fn viewport(x: f32, y: f32, w: f32, h: f32)     fn scissor(x: i64, y: i64, w: i64, h: i64)
fn draw(vertices: i64)                          fn draw_instanced(vertices: i64, instances: i64)
fn draw_indexed(indices: i64)                   fn draw_indexed_instanced(indices: i64, instances: i64)
fn draw(vertices, instances, first_vertex, first_instance)
fn draw_indexed(indices, instances, first_index, vertex_offset, first_instance)
```

A pipeline is not tied to the formats it draws into; it is made for each
pass's targets the first time it is bound in one. The vertex shader's inputs
are read, in location order, one after another from one packed element, as a
Goose struct lays out its fields: `struct Vertex { pos: float3, uv: float2 }`
feeds `layout(location = 0) in vec3` and `layout(location = 1) in vec2`.
From `instance_location` on they come per instance from vertex buffer 1, and
`formats[location]` overrides a location's format (`VERTEX_UBYTE4_NORM` for a
color as four bytes, `VERTEX_HALF2`, and the rest of `VERTEX_*`). Primitives
`TRIANGLES`, `TRIANGLE_STRIP`, `LINES`, `LINE_STRIP`, `POINTS`; culls
`CULL_NONE`, `CULL_FRONT`, `CULL_BACK` (front faces counterclockwise unless
`clockwise`); blends `BLEND_NONE`, `BLEND_ALPHA`, `BLEND_ADD`,
`BLEND_PREMULTIPLIED`, `BLEND_MULTIPLY`.

Uniform blocks are laid out by std140 and Goose structs are packed: a vec3 or
vec4 member starts on a multiple of 16 bytes and a block's size rounds up to
16, so the matching struct pads (`struct Place { rect: float4, depth: f32,
pad 12 }`); pushing one of another size than the shader's block is a misuse.
On Direct3D 12 a shader's `gl_InstanceIndex` does not count a draw's
`first_instance`, so keep that 0 if the shader reads it.

### Compute

```goose
struct ComputePipeline { id: u32 }
fn compute_pipeline(cs: const u8[:]) -> ComputePipeline
fn begin_compute(rw_buffers: const Buffer[:]) -> bool     // and (rw_buffers, rw_textures)
fn bind(p: ComputePipeline)
fn dispatch(x: i64, y: i64, z: i64)                        // workgroups of the shader's local size
fn end_compute()
fn release(p: ComputePipeline)
```

### Matrices

```goose
struct mat4 { c0: float4, c1: float4, c2: float4, c3: float4 }   // column-major, as GLSL's mat4
let mat4_identity
fn mul(a: mat4, b: mat4) -> mat4          fn mul(m: mat4, v: float4) -> float4
fn translate(t: float3) -> mat4           fn scale(s: float3) -> mat4
fn rotate(axis: float3, angle: f32) -> mat4
fn perspective(fovy: f32, aspect: f32, near: f32, far: f32) -> mat4   // right-handed, depth 0..1
fn ortho(left, right, bottom, top, near, far: f32) -> mat4
fn look_at(eye: float3, target: float3, up: float3) -> mat4
```

Clip space is SDL_GPU's on every backend: y up, depth from 0 to 1, texture
coordinates with (0, 0) at the top left.

## audio

Procedural PCM sound effects over SDL3, independent of a window or graphics
device. `import audio;` uses the `audio::` namespace. Built when SDL is present
and `GOOSE_AUDIO=ON` (the default), including with `GOOSE_GFX=OFF`. Programs
built from generated C link the response file named by `goose --audio-link
msvc|cc`. A compiler without the layer can still typecheck and emit the same C;
JIT use reports that audio was not built in. See `design/audio.md` for the mixer.

| Function | Behavior |
|---|---|
| `available() -> bool` | True when the native layer is linked; no device probe. |
| `open(sample_rate = 48000) -> bool` | Open the default output device, stereo output. |
| `open_offline(sample_rate = 48000) -> bool` | Open the same mixer without a device or background thread. |
| `close()` | Stop playback, free every sound, invalidate handles; safe to repeat. |
| `create_sound(samples: const f32[:], sample_rate = 48000, channels = 1) -> Sound` | Copy mono or interleaved stereo PCM into owned storage. |
| `release_sound(sound) -> bool` | Stop its voices and free the PCM. Zero is harmless. |
| `play(sound, volume = 1.0, pan = 0.0, pitch = 1.0, looping = false) -> Voice` | Start an independent voice, allowing overlapping repeats. |
| `playing(voice) -> bool` | Whether the voice is still mixing. Stale/zero handles return false. |
| `stop(voice)`, `stop_all()` | Stop one or every voice. Completed/stale/zero voices are harmless. |
| `set_voice(voice, volume, pan, pitch) -> bool` | Change a live voice without restarting. |
| `render(out: f32[:]) -> bool` | Offline only: write interleaved stereo output and advance playback. |
| `error() -> u8[>..]` | Most recent failure; successful calls do not clear it. |
| `error_into(out: u8[:]) -> i64` | Copy as much error text as fits, returning its full length. |

All calls run on the main thread; calls reachable from `thread_fn` are rejected.
`Sound { id: u64 }` and `Voice { id: u64 }` are explicit resources. Sound creation
copies the source immediately: Goose can then mutate or free its array. Releasing
a sound stops all its voices. Completed voices recycle automatically. A new
resource never aliases an old handle, even across `close()` and `open()`.

There is one mixer, at most `MAX_SOUNDS = 256` sounds and `MAX_VOICES = 64`
simultaneous voices. Exhaustion returns zero without stealing a voice. Rates
are 8000..192000 Hz. PCM must be nonempty, finite, in `[-1, 1]`, with complete
frames (two samples per stereo frame). Volume is `0..1`; pan is `-1..1`,
centered at zero, attenuating the opposite channel; pitch is `0.125..8` and
changes speed and duration together. Mixing uses linear interpolation and clips
the final sum to `[-1, 1]`. Leave headroom when many effects overlap.

Invalid arguments, stale sounds, unavailable devices and resource exhaustion
return false or a zero handle and set `error()`. They do not abort. Stopping or
querying a completed voice is normal; updating it returns false. Offline
`render` needs an even sample count, allows empty output, and advances exactly
`out.len / 2` frames. Device mode advances automatically; `playing()` describes
the mixer, so a little audio already buffered by the device can remain after
completion or stop. No file decoding, streaming input, recording, or effects.

```goose
import audio;
import math;
import os;

fn main() {
    if !audio::open() { print(audio::error()); return; }
    var pcm: f32[>..] = [];
    for i in 12000 {
        let t = i as f64 / 48000.0;
        let envelope = 1.0 - i as f64 / 12000.0;
        pcm.push((sin(TAU * 440.0 * t) * 0.2 * envelope) as f32);
    }
    let tone = audio::create_sound(pcm);
    let voice = audio::play(tone);
    while audio::playing(voice) { sleep_ms(5); }
    sleep_ms(100); // Let the device finish the already mixed tail.
    audio::close();
}
```

## physics

Rigid body physics on Box3D: worlds, bodies, shapes of every kind Box3D has,
nine kinds of joints, contact, sensor, hit, move and joint events, ray and
shape casts and overlap queries, a character mover, and recording and replay.
Optional like `gfx`: it needs a compiler built with the `third_party/box3d`
submodule, and a program using it links what `goose --physics-link msvc|cc`
prints (`cl game.c @<it>`, `cc game.c -o game @<it>`; a program using `gfx`
too adds that one's). A compiler without it still typechecks and generates C
for such a program; only running it in-process fails. Everything is in
namespace `physics`; `samples/28_physics_boxes.goose` is a complete program,
`design/physics.md` how it works.

The API is Box3D's under Goose names: `b3Body_GetPosition` is
`position(body)`, `b3CreateRevoluteJoint` is `create_revolute_joint`, a
getter uses the property name and a setter prefixes it with `set_`. Box3D works in meters, kilograms
and seconds and has no built-in up; the default gravity is -10 along y. What
Box3D takes as a callback comes back as an array: events and query results
are fresh arrays (each also has an `_into` form that copies into a slice of
the caller's and returns how many there are), and friction and restitution
mixing is one of the `MIX_*` rules. Custom contact filters, pre-solve
callbacks, debug drawing and the standalone dynamic tree are not part of it.

Physics runs on the main thread; a `thread_fn` reaching it is a compile
error. Box3D spreads a step over worker threads of its own, as many as
`WorldDef.workers` asks for, with the same results for any number of them.

### Errors

A call that can fail for reasons outside the program -- a file that is not
there, points that make no hull -- returns false or a zero handle, and
`error()` says why. A call the program should not have made -- a destroyed
body, a revolute function on a prismatic joint, a height field on a dynamic
body -- is reported as it happens, skipped, and aborts the program at the
next `step()`, `destroy(world)` or `check()`. Every handle carries a
generation, so a destroyed object is an error to use, not a crash.

```goose
fn error() -> u8[>..]           fn check()             fn misuse_count() -> i64
fn available() -> bool          fn version() -> Version          // Box3D's
fn byte_count() -> i64          // what Box3D has allocated
fn set_length_units_per_meter(units: f32)     // before anything else is made
fn length_units_per_meter() -> f32
fn world_count() -> i64         fn max_world_count() -> i64
```

### Values

```goose
struct Quat { x: f32, y: f32, z: f32, w: f32 }       // a rotation; w the scalar part
struct Transform { p: float3, q: Quat }              // rotate by q, then move by p
struct Mat3 { cx: float3, cy: float3, cz: float3 }   // by columns
struct AABB { lower: float3, upper: float3 }
struct Plane { normal: float3, offset: f32 }
struct MassData { mass: f32, center: float3, inertia: Mat3 }
struct Sphere { center: float3, radius: f32 }
struct Capsule { center1: float3, center2: float3, radius: f32 }
let quat_identity           let transform_identity
fn quat_axis_angle(axis: float3, angle: f32) -> Quat
fn mul(a: Quat, b: Quat) -> Quat            // b, then a
fn rotate(q: Quat, v: float3) -> float3     fn inv_rotate(q: Quat, v: float3) -> float3
fn conjugate(q: Quat) -> Quat               fn normalize(q: Quat) -> Quat
fn rotation_matrix(q: Quat) -> Mat3
fn transform_point(t: Transform, p: float3) -> float3      // and inv_transform_point
fn mul(a: Transform, b: Transform) -> Transform            fn inv_mul(a, b) -> Transform
```

### Worlds

```goose
struct World { id: u32 }
struct WorldDef { gravity: float3 = { 0, -10, 0 }, restitution_threshold: f32 = 1.0,
                  hit_event_threshold: f32 = 1.0, contact_hertz: f32 = 30.0,
                  contact_damping_ratio: f32 = 10.0, contact_speed: f32 = 3.0,
                  maximum_linear_speed: f32 = 400.0, enable_sleep: bool = true,
                  enable_continuous: bool = true, workers: i32 = 1, friction_mixing: i32 = 0,
                  restitution_mixing: i32 = 0, user_data: u64 = 0, capacity: Capacity }
fn create_world() -> World                   // and (def)
fn destroy(w: World)                         fn is_valid(w: World) -> bool
fn step(w: World, time_step: f32, sub_steps: i64)    // 1/60 and 4 are the usual
fn gravity(w) -> float3        fn set_gravity(w, gravity: float3)
fn enable_sleeping(w, flag)    fn sleeping_enabled(w) -> bool     // and continuous, warm_starting
fn restitution_threshold(w) -> f32   fn hit_event_threshold(w) -> f32   // and set_...
fn maximum_linear_speed(w) -> f32    fn contact_recycle_distance(w) -> f32  // and set_...
fn set_contact_tuning(w, hertz: f32, damping_ratio: f32, push_speed: f32)
fn set_friction_mixing(w, rule: i64)          fn set_restitution_mixing(w, rule: i64)
fn worker_count(w) -> i64      fn set_worker_count(w, count: i64)   // 1 to MAX_WORKERS
fn explode(w, def: ExplosionDef)       // position, radius, falloff, impulse_per_area, mask_bits
fn bounds(w) -> AABB           fn awake_body_count(w) -> i64
fn profile(w) -> Profile       fn counters(w) -> Counters      fn max_capacity(w) -> Capacity
fn user_data(w) -> u64         fn set_user_data(w, data: u64)
```

At most 128 worlds exist at once. Mixing rules: `MIX_DEFAULT` (Box3D's: the
geometric mean of the frictions, the larger restitution), `MIX_GEOMETRIC`,
`MIX_MIN`, `MIX_MAX`, `MIX_AVERAGE`, `MIX_MULTIPLY`.

### Bodies

```goose
struct Body { id: u64 }
struct BodyDef { body_type: i32 = 0, position: float3, rotation: Quat = quat_identity,
                 linear_velocity: float3, angular_velocity: float3, linear_damping: f32 = 0.0,
                 angular_damping: f32 = 0.0, gravity_scale: f32 = 1.0,
                 sleep_threshold: f32 = 0.05, safety_factor: f32 = 0.5,
                 motion_locks: MotionLocks, enable_sleep: bool = true, is_awake: bool = true,
                 is_bullet: bool = false, is_enabled: bool = true,
                 allow_fast_rotation: bool = false, enable_contact_recycling: bool = true,
                 user_data: u64 = 0 }
fn create_body(w: World, def: BodyDef) -> Body
fn destroy(b: Body)          // with its shapes and joints
fn is_valid(b) -> bool       fn world(b) -> World
fn body_type(b) -> i32       fn set_type(b, body_type: i64)   // STATIC, KINEMATIC, DYNAMIC
fn name(b) -> u8[>..]        fn set_name(b, name: const u8[:])
fn user_data(b) -> u64       fn set_user_data(b, data: u64)
fn position(b) -> float3     fn rotation(b) -> Quat     fn transform(b) -> Transform
fn transforms(bodies: const Body[:], out: Transform[:])   // many in one call
fn set_transform(b, position: float3, rotation: Quat)      // a teleport
fn local_point(b, p) -> float3    fn world_point(b, p) -> float3    // and local_/world_vector
fn linear_velocity(b) -> float3   fn angular_velocity(b) -> float3  // and set_...
fn local_point_velocity(b, p) -> float3        fn world_point_velocity(b, p) -> float3
fn set_target_transform(b, target: Transform, time_step: f32, wake: bool)   // kinematic
fn apply_force(b, force: float3, point: float3, wake: bool)   fn apply_force_to_center(b, force, wake)
fn apply_torque(b, torque: float3, wake: bool)
fn apply_linear_impulse(b, impulse, point, wake)   fn apply_linear_impulse_to_center(b, impulse, wake)
fn apply_angular_impulse(b, impulse: float3, wake: bool)
fn mass(b) -> f32     fn inverse_mass(b) -> f32     fn rotational_inertia(b) -> Mat3
fn world_inverse_inertia(b) -> Mat3    fn local_center(b) -> float3    fn world_center(b) -> float3
fn mass_data(b) -> MassData    fn set_mass_data(b, data: MassData)    fn apply_mass_from_shapes(b)
fn linear_damping(b) -> f32    fn angular_damping(b) -> f32    fn gravity_scale(b) -> f32  // and set_...
fn is_awake(b) -> bool         fn set_awake(b, awake: bool)     // the whole island
fn enable_sleep(b, flag)       fn sleep_enabled(b) -> bool
fn sleep_threshold(b) -> f32   fn safety_factor(b) -> f32       // and set_...
fn is_enabled(b) -> bool       fn disable(b)     fn enable(b)
fn motion_locks(b) -> MotionLocks      fn set_motion_locks(b, locks: MotionLocks)
fn is_bullet(b) -> bool        fn set_bullet(b, flag: bool)
fn fast_rotation_allowed(b) -> bool    fn allow_fast_rotation(b, flag: bool)
fn contact_recycling_enabled(b) -> bool    fn enable_contact_recycling(b, flag: bool)
fn enable_hit_events(b, flag: bool)          // on each of its shapes
fn shape_count(b) -> i64     fn shapes(b) -> Shape[>..]
fn joint_count(b) -> i64     fn joints(b) -> Joint[>..]
fn contacts(b) -> Manifold[>..]            // those touching
fn aabb(b) -> AABB           fn min_extent(b) -> f32    fn max_extent(b) -> float3
fn closest_point(b, target: float3) -> float3, f32
```

Queries against one body, placed where the query says: `cast_ray(b, origin,
translation, filter, max_fraction, body_transform) -> RayHit`, `cast_shape`,
`overlap_shape`, `collide_mover(b, origin, mover, filter, body_transform) ->
PlaneHit[>..]` and `time_of_impact_mover(b, origin, mover, translation,
filter, transform1, transform2) -> ToiHit`.

### Shapes

```goose
struct Shape { id: u64 }
struct ShapeDef { material: SurfaceMaterial, density: f32 = 1000.0, filter: Filter,
                  is_sensor: bool = false, enable_sensor_events: bool = false,
                  enable_contact_events: bool = false, enable_hit_events: bool = false,
                  invoke_contact_creation: bool = true, update_body_mass: bool = true,
                  enable_speculative_contact: bool = true, explosion_scale: f32 = 1.0,
                  user_data: u64 = 0 }
struct SurfaceMaterial { friction: f32 = 0.6, restitution: f32 = 0.0,
                         rolling_resistance: f32 = 0.0, tangent_velocity: float3,
                         user_material_id: u64 = 0, custom_color: u32 = 0 }
struct Filter { category_bits: u64 = ALL_BITS, mask_bits: u64 = ALL_BITS, group_index: i32 = 0 }
fn create_sphere_shape(b: Body, def: ShapeDef, sphere: Sphere) -> Shape
fn create_capsule_shape(b, def, capsule: Capsule) -> Shape
fn create_box_shape(b, def, half_extents: float3) -> Shape      // and (..., frame: Transform)
fn create_hull_shape(b, def, hull: Hull) -> Shape
fn create_transformed_hull_shape(b, def, hull, transform: Transform, scale: float3) -> Shape
fn create_mesh_shape(b, def, mesh: Mesh) -> Shape      // and (..., scale, materials)
fn create_height_field_shape(b, def, h: HeightField) -> Shape  // static bodies; and (..., materials)
fn create_compound_shape(b, def, c: Compound) -> Shape         // static bodies
fn destroy(s: Shape)         // and (s, update_body_mass: bool)
fn is_valid(s) -> bool       fn shape_type(s) -> i32    fn body(s) -> Body    fn world(s) -> World
fn is_sensor(s) -> bool      fn name(s) -> u8[>..]      fn user_data(s) -> u64   // and set_...
fn density(s) -> f32         fn set_density(s, density: f32, update_body_mass: bool)
fn friction(s) -> f32        fn restitution(s) -> f32   // and set_...
fn surface_material(s) -> SurfaceMaterial      fn set_surface_material(s, material)
fn mesh_material_count(s) -> i64    fn mesh_material(s, i) -> SurfaceMaterial   // and set_...
fn shape_filter(s) -> Filter        fn set_shape_filter(s, filter: Filter, invoke_contacts: bool)
fn enable_contact_events(s, flag)   fn contact_events_enabled(s) -> bool   // and sensor_, hit_
fn sphere(s) -> Sphere    fn capsule(s) -> Capsule    fn hull(s) -> Hull   // hull(): a copy
fn mesh(s) -> Mesh        fn mesh_scale(s) -> float3  fn height_field(s) -> HeightField
fn compound(s) -> Compound
fn set_sphere(s, sphere)  fn set_capsule(s, capsule)  fn set_hull(s, hull)  fn set_mesh(s, mesh, scale)
fn ray_cast(s, origin: float3, translation: float3) -> CastOutput     // in the world
fn contacts(s) -> Manifold[>..]     fn sensor_overlaps(s) -> Shape[>..]
fn aabb(s) -> AABB        fn mass_data(s) -> MassData     fn closest_point(s, target) -> float3
fn apply_wind(s, wind: float3, drag: f32, lift: f32, max_speed: f32, wake: bool)
```

Shape types `SHAPE_SPHERE`, `SHAPE_CAPSULE`, `SHAPE_HULL`, `SHAPE_MESH`,
`SHAPE_HEIGHT_FIELD`, `SHAPE_COMPOUND`. Two shapes collide when each one's
category is in the other's mask, unless they share a group index: a positive
group always collides, a negative one never does. A mesh collides only with
shapes that are not meshes. The default density is water's, 1000 kg/m³.

### Geometry

Hulls, meshes, height fields and baked compounds are made once and shared by
shapes. A shape keeps what it was made from: destroying a mesh, height field
or compound that shapes use lets go of the handle, and the data goes when its
last shape does; a hull is copied into each world that uses it.

```goose
struct Hull { id: u32 }      struct Mesh { id: u32 }
struct HeightField { id: u32 }      struct Compound { id: u32 }
fn create_hull(points: const float3[:]) -> Hull    // zero for points in a plane; and (..., max_vertices)
fn create_box_hull(half_extents: float3) -> Hull   // and (..., frame)
fn create_cylinder_hull(height, radius, y_offset: f32, sides: i64) -> Hull
fn create_cone_hull(height, radius1, radius2: f32, slices: i64) -> Hull
fn create_rock_hull(radius: f32) -> Hull
fn transformed(h: Hull, transform: Transform, scale: float3) -> Hull
fn create_mesh(vertices: const float3[:], indices: const i32[:]) -> Mesh
fn create_mesh(vertices, indices, material_indices: const u8[:], def: MeshDef) -> Mesh
fn create_grid_mesh(x_count, z_count: i64, cell_width: f32, material_count: i64, identify_edges: bool) -> Mesh
fn create_wave_mesh(...)    fn create_torus_mesh(...)    fn create_box_mesh(center, extent, identify_edges)
fn create_hollow_box_mesh(center, extent)    fn create_platform_mesh(center, height, top_width, bottom_width)
fn create_height_field(heights: const f32[:], material_indices: const u8[:], def: HeightFieldDef) -> HeightField
fn create_grid_height_field(rows, columns: i64, scale: float3, make_holes: bool) -> HeightField
fn create_wave_height_field(rows, columns, scale, row_frequency, column_frequency, make_holes)
fn create_compound(spheres: const CompoundSphere[:], capsules: const CompoundCapsule[:],
                   hulls: const CompoundHull[:], meshes: const CompoundMesh[:]) -> Compound
fn destroy(h: Hull)   fn is_valid(h: Hull) -> bool   fn info(h: Hull) -> HullInfo   // each kind
fn vertices(h: Hull) -> float3[>..]    fn triangles(h: Hull) -> int3[>..]    // and for Mesh
```

The same geometry answers queries on its own, in its own frame:
`compute_mass(g, density)` for spheres, capsules and hulls,
`compute_aabb(g, transform)`, `ray_cast(g, origin, translation,
max_fraction) -> CastOutput`, `overlap(g, transform, points, radius) -> bool`
and `shape_cast(g, points, radius, translation, max_fraction, can_encroach)
-> CastOutput` for each kind (a mesh takes its scale first). Between two
point clouds with radii: `shape_distance`, `shape_cast` and
`time_of_impact` over two `Sweep`s, with `sweep_transform(sweep, time)`.

### Joints

```goose
struct Joint { id: u64 }
struct JointDef { body_a: Body, body_b: Body, frame_a: Transform, frame_b: Transform,
                  collide_connected: bool = false, force_threshold: f32, torque_threshold: f32,
                  constraint_hertz: f32 = 60.0, constraint_damping_ratio: f32 = 2.0,
                  draw_scale: f32 = 1.0, user_data: u64 = 0 }
fn joint_def(a: Body, b: Body, anchor: float3) -> JointDef     // and (a, b, frame: Transform)
fn local_frame(b: Body, world_frame: Transform) -> Transform
fn create_revolute_joint(w: World, def: RevoluteJointDef) -> RevoluteJoint
// and create_distance_joint, _filter_, _motor_, _parallel_, _prismatic_, _spherical_,
// _weld_, _wheel_: each kind's definition starts with `base: JointDef`.
```

Each joint kind has its own handle type, such as `struct RevoluteJoint {
joint: Joint }`. Kind-specific functions take that handle. Functions common
to all joints take its `joint` field:

```goose
fn destroy(j: Joint)    // and (j, wake_bodies: bool)
fn is_valid(j) -> bool       fn joint_type(j) -> i32     fn body_a(j) -> Body    fn body_b(j) -> Body
fn world(j) -> World         fn local_frame_a(j) -> Transform     // and _b, and set_...
fn collide_connected(j) -> bool    fn user_data(j) -> u64       // and set_...
fn wake_bodies(j)            fn is_awake(j) -> bool
fn constraint_force(j) -> float3   fn constraint_torque(j) -> float3
fn linear_separation(j) -> f32     fn angular_separation(j) -> f32
fn constraint_tuning(j) -> f32, f32     fn set_constraint_tuning(j, hertz, damping_ratio)
fn force_threshold(j) -> f32        fn torque_threshold(j) -> f32     // and set_...
```

| Kind | What it does | Its own |
|---|---|---|
| `RevoluteJoint` | a hinge about the frames' z axis | `angle`, `enable_limit`/`set_limits`/`lower_limit`/`upper_limit`, `enable_motor`/`set_motor_speed`/`set_max_motor_torque`/`motor_torque`, `enable_spring`/`set_spring_hertz`/`set_spring_damping_ratio`/`set_target_angle` |
| `PrismaticJoint` | a slider along the frames' x axis | `translation`, `speed`, limits, a motor (`set_max_motor_force`, `motor_force`), a spring to `set_target_translation` |
| `DistanceJoint` | two anchors a distance apart | `rest_length`/`set_rest_length`, `current_length`, `set_length_range`/`min_length`/`max_length`, a spring with `set_spring_force_range`, a motor |
| `SphericalJoint` | a ball and socket | `enable_cone_limit`/`set_cone_limit`/`cone_angle`, `set_twist_limits`/`twist_angle`, a spring to `set_target_rotation`, a motor to `set_motor_velocity` |
| `WeldJoint` | two bodies as one | `set_linear_hertz`, `set_angular_hertz` and their damping ratios; 0 hertz is rigid |
| `WheelJoint` | a wheel on a suspension | `enable_suspension`/limits, `enable_spin_motor`/`set_spin_motor_speed`/`spin_speed`, `enable_steering`/`set_target_steering_angle`/`steering_angle` |
| `MotorJoint` | drives body b relative to body a | `set_linear_velocity`, `set_angular_velocity`, their limits, and springs |
| `ParallelJoint` | keeps the frames' z axes parallel | `set_spring_hertz`, `set_spring_damping_ratio`, `set_max_torque` |
| `FilterJoint` | only stops the two colliding | |

Every setter has its getter. Kinds are `JOINT_REVOLUTE` and so on; using a
kind's function on a joint of another kind is a misuse.

### Events

Read after the step they happened in, each an array of what the last step
produced:

```goose
fn contact_begin_events(w) -> ContactEvent[>..]      // shape_a, shape_b, contact
fn contact_end_events(w) -> ContactEvent[>..]
fn contact_hit_events(w) -> ContactHitEvent[>..]     // point, normal, approach_speed, materials
fn sensor_begin_events(w) -> SensorEvent[>..]        // sensor, visitor
fn sensor_end_events(w) -> SensorEvent[>..]
fn body_move_events(w) -> BodyMoveEvent[>..]         // transform, body, user_data, fell_asleep
fn joint_events(w) -> JointEvent[>..]                // joint, user_data: past its thresholds
struct Contact { ... }
fn is_valid(c: Contact) -> bool          fn manifolds(c: Contact) -> Manifold[>..]
```

A shape reports contacts with `enable_contact_events`, hits with
`enable_hit_events` (faster than the world's `hit_event_threshold`), and
sensor visits with `enable_sensor_events` on both the sensor and the visitor.
A `Manifold` has the contact and its shapes, the normal from a to b, and up
to four `ManifoldPoint`s with their anchors, separation and impulses.

### Queries

```goose
struct QueryFilter { category_bits: u64 = ALL_BITS, mask_bits: u64 = ALL_BITS }
fn overlap_aabb(w, box: AABB, filter: QueryFilter) -> Shape[>..]
fn overlap_shape(w, origin: float3, points: const float3[:], radius: f32, filter) -> Shape[>..]
fn cast_ray(w, origin: float3, translation: float3, filter) -> RayHit[>..]   // nearest first
fn cast_ray_closest(w, origin, translation, filter) -> RayHit                // hit false: none
fn cast_shape(w, origin, points, radius, translation, filter) -> RayHit[>..]
```

A query shape is the convex hull of up to `MAX_PROXY_POINTS` points around
`origin`, grown by `radius`: one point is a sphere, two a capsule, eight the
corners of a box.

### A character mover

```goose
fn cast_mover(w, origin: float3, mover: Capsule, translation: float3, filter) -> f32
fn collide_mover(w, origin, mover, filter) -> PlaneHit[>..]
fn solve_planes(target_delta: float3, planes: CollisionPlane[:]) -> float3, i64
fn clip_vector(vector: float3, planes: const CollisionPlane[:]) -> float3
```

`cast_mover` determines how far a capsule can move. `collide_mover` returns
the planes it touches. Pass these as `CollisionPlane`s to `solve_planes` to
find the allowed move closest to the target. `clip_vector` removes velocity
components that point into those planes.

### Recording and replay

```goose
struct Recording { id: u32 }      struct Player { id: u32 }
fn create_recording() -> Recording        fn load_recording(path: const u8[:]) -> Recording
fn start_recording(w: World, r: Recording)        fn stop_recording(w: World)
fn size(r) -> i64     fn bytes(r) -> u8[>..]      fn save(r, path: const u8[:]) -> bool
fn validate_replay(r, workers: i64) -> bool       // replays it; the same?
fn create_player(r, workers: i64) -> Player
fn step(p: Player) -> bool     fn sub_step(p)     fn at_pre_step(p) -> bool
fn restart(p)     fn seek(p, frame: i64)     fn frame(p) -> i64     fn frame_count(p) -> i64
fn at_end(p) -> bool     fn diverged(p) -> bool     fn diverge_frame(p) -> i64
fn world(p) -> World     fn body_count(p) -> i64    fn body(p, index: i64) -> Body
fn info(p) -> PlayerInfo     fn set_worker_count(p, count: i64)
fn destroy(r: Recording)     fn destroy(p: Player)
```

A recording holds everything a world does between `start_recording` and
`stop_recording`; a player replays it into a world of its own, step by step,
checking it comes out the same. The recorded bodies exist once the replay has
made them, `body(p, k)` being the k-th made.

```goose
let world = physics::create_world();
let ground = physics::create_body(world, physics::BodyDef { position: float3 { 0.0, -1.0, 0.0 } });
physics::create_box_shape(ground, physics::ShapeDef {}, float3 { 20.0, 1.0, 20.0 });
let crate = physics::create_body(world, physics::BodyDef { body_type: physics::DYNAMIC,
                                                           position: float3 { 0.0, 4.0, 0.0 } });
physics::create_box_shape(crate, physics::ShapeDef {}, float3 { 0.5, 0.5, 0.5 });
for i in 90 { physics::step(world, 1.0 / 60.0, 4); }
print(physics::position(crate).y);        // resting on the ground: about 0.5
physics::destroy(world);
```

## ui

Windows and widgets on Nuklear, the immediate-mode GUI library, drawn
through `gfx`: windows, rows and layout spaces, groups, trees, list views,
labels, buttons, check boxes, options, selectables, sliders, knobs, progress
bars, color pickers, properties, text fields and editors, charts, popups,
combo boxes, contextual menus, tooltips, menus, styles and fonts. Optional
like `gfx`: it needs a compiler built with the `third_party/nuklear`
submodule, and a program using it links what `goose --ui-link msvc|cc`
prints, with gfx's if it renders through gfx (`cl app.c @<ui> @<gfx>`). A
compiler without it still typechecks and generates C for such a program;
only running it in-process fails. Everything is in namespace `ui`;
`samples/29_ui_todo.goose` is a complete program, `design/ui.md` how it
works.

The API is Nuklear's under Goose names: `nk_button_label` is
`button_label`. Immediate mode means each frame the program says which
windows and widgets there are, in order, and learns what the user did from
the same calls: `button_label` returns true on the frame the button is
clicked, `checkbox_label` flips the `bool` it is given, `slider_float` moves
the `f32`. The program's data stays its own; a context keeps only where
windows are and what is open, active or scrolled.

```goose
guard gfx::open("sound", 400, 300) else { abort(str("gfx: ", gfx::error())); }
let ctx = ui::create(13.0);
var volume: f32 = 0.5;
var muted = false;
while gfx::frame() {
    ui::input_from_gfx(ctx);
    if ui::begin(ctx, "Sound", ui::rect(20, 20, 220, 130), ui::WINDOW_TITLE | ui::WINDOW_MOVABLE) {
        ui::layout_row_dynamic(ctx, 24.0, 1);
        ui::slider_float(ctx, 0.0, volume, 1.0, 0.05);
        ui::checkbox_label(ctx, "Muted", muted);
        if ui::button_label(ctx, "Reset") { volume = 0.5; }
    }
    ui::end(ctx);
    gfx::begin_screen(float4 { 0.1, 0.1, 0.1, 1.0 });
    gfx::end_pass();
    ui::render(ctx);
}
ui::destroy(ctx);
```

A frame is: input (here from gfx), then windows, each `begin` followed by
its `end` whether or not it returned true, then drawing what they built.
Inside a window every widget takes the next place in the current row, so a
row comes first (`layout_row_*`). The ui runs on the main thread; a
`thread_fn` reaching it is a compile error.

### Errors

A call that can fail for reasons outside the program -- a font file that is
not there, bytes that are not a font -- returns false or a zero handle, and
`error()` says why. A call the program should not have made -- a widget
outside a window, a group ended that was never begun, a window begun inside
another, a key that is not one, a destroyed handle -- is printed as it
happens, skipped, and aborts the program at the next `input_begin`,
`convert`, `render`, `destroy(context)` or `check()`. Contexts, font
atlases, fonts and text editors are handles with a generation, so a
destroyed one is an error to use, not a crash.

```goose
fn error() -> u8[>..]     fn check()     fn misuse_count() -> i64     fn available() -> bool
```

### Contexts and input

```goose
struct Context { id: u32 }
fn create(font_height: f32) -> Context        // Nuklear's own font; 13 is its native size
fn create(font_height: f32, scale: f32) -> Context      // drawn at a scale (set_scale)
fn create_from_file(path: const u8[:], font_height: f32) -> Context   // a TrueType font of its own
fn create_from_memory(ttf: const u8[:], font_height: f32) -> Context  // from the font's bytes
fn create_context(font: Font) -> Context      // a font from a baked atlas
fn destroy(c: Context)        fn is_valid(c: Context) -> bool
fn context_atlas(c) -> FontAtlas              fn font(c) -> Font      // drawn with now
fn clear(c)                   // ends a frame; input_begin does it for a frame that drew
fn set_scale(c, scale: f32) -> bool           fn scale(c) -> f32      // pixels per ui unit
fn set_delta_time(c, seconds: f32)            fn delta_time(c) -> f32
fn set_clipboard(c, text: const u8[:])        // what KEY_PASTE pastes
fn copied(c) -> u8[>..], bool                 // what a text field copied or cut, if any
fn input_begin(c)             fn input_end(c)
fn input_motion(c, x: i64, y: i64)
fn input_button(c, button: i64, x: i64, y: i64, down: bool)     // BUTTON_LEFT, _MIDDLE, _RIGHT, ...
fn input_scroll(c, amount: float2)
fn input_key(c, key: i64, down: bool)         // KEY_*: editing keys and shortcuts
fn input_text(c, text: const u8[:])           // and input_char, input_glyph, input_unicode
```

The whole ui can be drawn bigger or smaller with `set_scale`, which the
program picks as it likes -- from the window's size, the display, a
setting. Layout stays the same: windows, rows, fonts and the style keep
their sizes in the ui's own units, and the scale is the pixels to each
unit, which `convert` and `render` draw with and input positions come in.
A context made by `create` or `create_from_*` has its font baked again at
the scale, so its text stays sharp -- Nuklear's built-in font imitates a
bitmap one and scales well only by whole numbers, a TrueType one by any;
one made on a font of the program's keeps that atlas's, which `bake(atlas,
scale)` makes sharp at a scale, again at each new one. It changes between
frames, not during one.

Input goes between `input_begin` and `input_end`, before any window, one
batch a frame; `input_from_gfx` below does it all. Positions are pixels. A
key's press and
release in the same frame count as a press; Nuklear looks at a frame's keys
after it, in the order of their `KEY_*` values. A button's press or release
also zeroes the frame's mouse motion. What widgets see can be asked
directly:

```goose
fn input_mouse(c) -> Mouse           // pos, prev, delta, scroll_delta, grab state
fn input_is_mouse_down(c, button) -> bool      // and _pressed, _released
fn input_is_key_down(c, key) -> bool           // and _pressed, _released
fn input_is_mouse_hovering_rect(c, r: Rect) -> bool     // and _prev_, _still_
fn input_has_mouse_click_in_rect(c, button, r) -> bool  // where the last click was
fn input_is_mouse_click_in_rect(c, button, r) -> bool   // a release there this frame
fn input_is_mouse_hovering_delay_rect(c, r, timer: f32&, delay: f32) -> bool
```

A click is a button going down or up: the `has_` queries look at where the
button's last one was, the `is_` ones at one this frame. The `_delay`
queries count hovering time in a timer the program keeps between frames
(also `_still_delay` and `_still_delay_clicked`).

A program that shares its input with the ui -- a game with keys of its own
-- asks whose it is:

```goose
fn wants_keyboard(c) -> bool     // a text field or a property being typed into has the keys
fn wants_mouse(c) -> bool        // over a window or popup taking input, or dragging from one
```

A text field has the keyboard from the click that focuses it until a click
outside it, `edit_unfocus`, or its window closing or being left out of a
frame, wherever the mouse goes meanwhile; its window keeps it focused
behind another window, under a popup of its own or hidden, but takes no
keys until it is in front, the popup closed or the window shown again. The
mouse is the ui's over a window or a popup, and while it holds a button
that went down over one; a button that went down elsewhere keeps it the
program's until released. A window or popup begun with `WINDOW_NO_INPUT`
-- a message log, a box of hover information -- lets the mouse through to
the program: it cannot be moved, scaled, scrolled or brought to the front,
and its widgets take no clicks. Nuklear's `item_is_any_active` is true whenever
the mouse is over a window, so it cannot say whose the keys are.

### Text rendering and game HUDs

`ui` provides text rendering for gfx applications as well as widgets:
`ui::label` uses row/space layout, and `ui::draw_text` draws at an explicit
rectangle on the current window's canvas. `ui::create(13.0)` supplies a
built-in font without an asset file; `create_from_file` and
`create_from_memory` supply TrueType fonts. Font measurement and atlas
access are listed under [Fonts](#fonts).

For a HUD, create a dedicated context once, hide its window background, and
use `WINDOW_NO_INPUT | WINDOW_NO_SCROLLBAR` with no title or border. The
window supplies a canvas and clipping; it does not need interactive widgets.
The following fits inside a gfx program (after opening gfx):

```goose
let hud = ui::create(13.0);
if hud.id == 0 { abort(ui::error()); }
var style = ui::style(hud);
style.window.fixed_background = ui::style_item_hide();
ui::set_style(hud, style);
while gfx::frame() {
    ui::input_from_gfx(hud);
    if ui::begin(hud, "HUD", ui::rect(0, 0, 320, 80),
                 ui::WINDOW_NO_INPUT | ui::WINDOW_NO_SCROLLBAR) {
        ui::draw_text(hud, ui::rect(12, 12, 280, 24), "Health: 100",
                      ui::font(hud), ui::Color { 0, 0, 0, 0 },
                      ui::Color { 255, 255, 255, 255 });
    }
    ui::end(hud); // Always paired with begin, even when begin returns false.
    // Draw the game scene here and end its render pass.
    ui::render(hud);
}
ui::destroy(hud); // Before gfx::close().
```

Draw the HUD after the game scene. `ui::render` uses gfx internally and
retains the scene beneath it. For a resizable full-screen overlay, update
the window's bounds to the viewport; for a custom renderer, ui can also
export its text and geometry commands or converted triangles. World-space
sprites, billboard orientation and sprite animation belong to the game's
sprite renderer; ordinary HUD text can use this existing font path.

### Fonts

```goose
struct FontAtlas { id: u32 }         struct Font { id: u32 }
fn create_font_atlas() -> FontAtlas              fn destroy(a: FontAtlas)
fn add_default_font(a, height: f32) -> Font      // ProggyClean
fn add_font_from_file(a, path: const u8[:], height: f32) -> Font
fn add_font_from_memory(a, ttf: const u8[:], height: f32) -> Font
    // each also (..., config: FontConfig) and (..., config, ranges: const u32[:])
fn bake(a) -> bool                   // then the fonts can be used
fn bake(a, scale: f32) -> bool       // glyphs scale times the size, for a ui at that scale;
                                     // again at another, fonts kept
fn atlas_size(a) -> int2             fn atlas_pixels(a) -> u8[>..]     // RGBA
fn set_texture(a, texture: u32)      fn texture(a) -> u32
fn fonts(a) -> Font[>..]             fn atlas(f: Font) -> FontAtlas
fn info(f) -> FontInfo               // height, ascent, descent, glyph count, fallback
fn text_width(f, text: const u8[:]) -> f32
fn find_glyph(f, codepoint: u32) -> FontGlyph, bool
fn glyph_ranges(which: i64) -> u32[>..]          // RANGE_DEFAULT, _CHINESE, _CYRILLIC, _KOREAN
```

An atlas bakes its fonts into one image; a context draws with one of them
and uses the atlas's white texel for its shapes. `FontConfig` has
Nuklear's defaults: oversampling, pixel snapping, spacing, the characters
to bake (`ranges`, a `RANGE_*` set, or pairs of first and last character
given as an array), the fallback glyph, and `merge_mode`, which adds the
glyphs to the atlas's first font. Fonts are added before the atlas is
first baked; it keeps their data, and baking it again at another scale
replaces their glyphs and image in place, so contexts drawing with its
fonts keep them and draw sharp at the new scale. A context draws its
shapes with the atlas of the font it was made with or last given by
`style_set_font`; destroying an atlas a context still uses is an error.

### Windows and layout

```goose
fn begin(c, title: const u8[:], bounds: Rect, flags: i64) -> bool   // WINDOW_* flags
fn begin_titled(c, name, title, bounds, flags) -> bool
fn end(c)
fn window_get_bounds(c) -> Rect      // and _position, _size, _content_region, _scroll, ...
fn window_has_focus(c) -> bool       fn window_is_hovered(c) -> bool
fn window_is_collapsed(c, name) -> bool       // and _closed, _hidden, _active
fn window_set_bounds(c, name, bounds: Rect)   // and _position, _size, _focus, set_scroll
fn window_close(c, name)       fn window_collapse(c, name, state: i64)   // MINIMIZED, MAXIMIZED
fn window_show(c, name, state: i64)          // SHOWN, HIDDEN; and the _if forms
fn layout_row_dynamic(c, height: f32, cols: i64)            // equal parts of the width
fn layout_row_static(c, height: f32, item_width: i64, cols: i64)
fn layout_row(c, format: i64, height: f32, ratios: const f32[:])    // DYNAMIC, STATIC
fn layout_row_begin(c, format, height, cols)   fn layout_row_push(c, value: f32)   fn layout_row_end(c)
fn layout_row_template_begin(c, height)   // push_dynamic, push_variable(min), push_static(w), _end
fn layout_space_begin(c, format, height, widget_count)   fn layout_space_push(c, bounds: Rect)
fn layout_space_end(c)       // and _bounds, _to_screen, _to_local
fn layout_set_min_row_height(c, height)   fn spacer(c)   fn spacing(c, cols)
```

A row lays out the widgets after it, and repeats until the next row call.
`begin` is false for a window collapsed or hidden, whose `end` still
follows. Windows are found by name: two in one frame cannot share one.

### Groups, trees and list views

```goose
fn group_begin(c, title, flags) -> bool          fn group_end(c)
fn group_scrolled_begin(c, offset: Scroll, title, flags) -> bool
fn group_scrolled_end(c) -> Scroll               fn group_get_scroll(c, id) -> Scroll
fn tree_push(c, type: i64, title, initial: i64) -> bool    // TREE_NODE or _TAB, MINIMIZED..
fn tree_pop(c)
fn tree_push_id(c, type, title, initial, id: i64) -> bool     // same titles apart
fn tree_image_push(c, type, img: Image, title, initial) -> bool
fn tree_state_push(c, type, title, state: i64&) -> bool       fn tree_state_pop(c)
fn tree_element_push(c, type, title, initial, selected: bool&) -> bool   fn tree_element_pop(c)
fn list_view_begin(c, id, flags, row_height: i64, row_count: i64) -> ListView, bool
fn list_view_end(c)          // build rows view.begin until view.end only
```

A group is a scrolled panel inside a window, with its own rows; its end
follows only when it began. A tree node's contents go between a true push
and its pop.

### Widgets

```goose
fn label(c, text: const u8[:], align: i64)       // TEXT_LEFT, TEXT_CENTERED, TEXT_RIGHT
fn label_colored(c, text, align, color: Color)   fn label_wrap(c, text)
fn value_int(c, prefix, v: i64)                  // and _bool, _uint, _float, _color_*
fn link_label(c, text, align) -> bool            // and _colored, _underline, _styled, ...
fn image(c, img: Image)                          fn image_color(c, img, color)
fn button_label(c, text) -> bool                 // clicked this frame
fn button_symbol(c, symbol: i64) -> bool         // SYMBOL_*; and _color, _image, _*_label
fn button_label_styled(c, style: const StyleButton&, text) -> bool    // and for each kind
fn button_push_behavior(c, behavior: i64) -> bool    // BUTTON_DEFAULT, BUTTON_REPEATER
fn button_pop_behavior(c) -> bool
fn checkbox_label(c, text, active: bool&) -> bool          // and _align, _flags_label
fn check_label(c, text, active: bool) -> bool              // the new state
fn radio_label(c, text, active: bool&) -> bool   fn option_label(c, text, active: bool) -> bool
fn selectable_label(c, text, align, value: bool&) -> bool  // and _image_, _symbol_; select_*
fn slider_float(c, min: f32, value: f32&, max: f32, step: f32) -> bool    // and slider_int
fn slide_float(c, min, value, max, step) -> f32           // and slide_int
fn knob_float(c, min, value: f32&, max, step, zero_direction: i64, dead_zone_degrees: f32) -> bool
fn progress(c, cur: i64&, max: i64, modifiable: bool) -> bool    fn prog(c, cur, max, modifiable) -> i64
fn color_picker(c, color: Colorf, format: i64) -> Colorf    // RGB, RGBA
fn color_pick(c, color: Colorf&, format) -> bool
fn property_float(c, name, min, value: f32&, max, step, inc_per_pixel: f32) -> bool
    // and _int, _double; propertyi, propertyf, propertyd return the value
fn widget(c) -> i64, Rect            // a place for a widget of the program's own: WIDGET_*
fn widget_bounds(c) -> Rect          // the next widget's place, and _position, _size, ...
fn widget_is_hovered(c) -> bool      fn widget_is_mouse_clicked(c, button) -> bool
fn widget_disable_begin(c)           fn widget_disable_end(c)     // shown dimmed, inert
```

The `_label` names take any Goose string: a Nuklear `_text` function is the
same call. A property is dragged, stepped with its arrows or typed into; a
name starting with `#` is not shown. Sliders and knobs need a non-empty
range.

### Text editing

```goose
fn edit_string(c, flags: i64, text: T&, filter: i64) -> i64     // u8[..k], u8[..]
fn edit_string(c, flags, buffer: u8[:], len: i64&, filter) -> i64
fn edit_buffer(c, flags, edit: TextEdit, filter) -> i64         // keeps undo
fn edit_focus(c, flags)        fn edit_unfocus(c)                // the next edit
fn filter_accepts(filter: i64, rune: u32) -> bool
fn create_text_edit() -> TextEdit     fn destroy(e: TextEdit)
fn text(e) -> u8[>..]          fn set_text(e, text)              fn state(e) -> TextEditState
fn set_cursor(e, cursor, select_start, select_end)               fn set_mode(e, mode: i64)
fn textedit_text(e, text)      fn textedit_delete(e, at, len)    fn textedit_delete_selection(e)
fn textedit_select_all(e)      fn textedit_cut(e) -> bool        fn textedit_paste(e, text) -> bool
fn textedit_undo(e)            fn textedit_redo(e)
```

Flags are `EDIT_*`, with the usual sets `EDIT_SIMPLE`, `EDIT_FIELD` (one
line, selectable, clipboard) and `EDIT_BOX` (several lines); the result
says `EDIT_ACTIVE` or `_INACTIVE`, `_ACTIVATED` and `_DEACTIVATED` this
frame, and `_COMMITTED` for Enter with `EDIT_SIG_ENTER`. A field takes what
fits in its text's capacity, through a `FILTER_*` (`DEFAULT`, `ASCII`,
`FLOAT`, `DECIMAL`, `HEX`, `OCT`, `BINARY`); a paste takes what typing
would. A text editor (`TextEdit`) holds its own text, cursor, selection,
mode and undo history; a field over a buffer keeps no undo between frames.
Editing keys are `KEY_*`; `KEY_COPY` and `KEY_CUT` hand the selection to
`copied`, `KEY_PASTE` pastes `set_clipboard`'s text.

### Popups, combo boxes, menus and tooltips

```goose
fn popup_begin(c, type: i64, title, flags, rect: Rect) -> bool     // POPUP_STATIC, _DYNAMIC
fn popup_close(c)     fn popup_end(c)     // and popup_get_scroll, popup_set_scroll
fn combo(c, items: const u8[:][:], selected: i64, item_height: i64, size: float2) -> i64
fn combobox(c, items, selected: i64&, item_height, size) -> bool
    // and combo_separator, combo_string over one string of items
fn combo_begin_label(c, selected: const u8[:], size) -> bool   // and _color, _symbol, _image
fn combo_item_label(c, text, align) -> bool     fn combo_close(c)     fn combo_end(c)
fn contextual_begin(c, flags, size: float2, trigger_bounds: Rect) -> bool
fn contextual_item_label(c, text, align) -> bool   fn contextual_close(c)   fn contextual_end(c)
fn tooltip(c, text)       fn tooltip_offset(c, text, position: i64, offset: float2)   // TOP_LEFT..
fn tooltip_begin(c, width: f32) -> bool           fn tooltip_end(c)
fn do_tooltip(c, text, bounds: Rect)    // when hovering bounds; and _delay(.., timer: f32&)
fn menubar_begin(c)       fn menubar_end(c)       // first in its window
fn menu_begin_label(c, text, align, size: float2) -> bool    // and _image, _symbol forms
fn menu_item_label(c, text, align) -> bool        fn menu_close(c)      fn menu_end(c)
```

A popup, combo box, contextual menu, menu or tooltip is a small window of
its own, drawn above the one it opens from, with rows inside; each end
follows only a true begin, and none opens inside another. A tooltip is
asked for before the widget it is about, when `widget_is_hovered`.

### Charts

```goose
fn chart_begin(c, type: i64, count: i64, min: f32, max: f32) -> bool   // CHART_LINES, _COLUMN
fn chart_begin_colored(c, type, color: Color, active: Color, count, min, max) -> bool
fn chart_add_slot(c, type, count, min, max)      // and _colored: another line or columns
fn chart_push(c, value: f32) -> i64              // CHART_HOVERING, CHART_CLICKED
fn chart_push_slot(c, value, slot: i64) -> i64
fn chart_end(c)
fn plot(c, type, values: const f32[:])           // a whole chart, scaled to the values
```

### Style and colors

```goose
fn style(c) -> Style              fn set_style(c, s: const Style&)
fn push_style(c, s) -> bool       fn pop_style(c) -> bool
fn style_default(c)               fn style_from_table(c, table: const Color[:])   // one per COLOR_*
fn default_color_table() -> Color[>..]           fn style_get_color_by_name(color: i64) -> u8[>..]
fn style_set_font(c, f: Font)     fn style_push_font(c, f) -> bool     fn style_pop_font(c) -> bool
fn style_load_cursor(c, which: i64, cursor: Cursor)     // CURSOR_*
fn style_load_all_cursors(c, cursors: const Cursor[:])  // one per CURSOR_*
fn style_set_cursor(c, which: i64) -> bool     fn style_show_cursor(c)     fn style_hide_cursor(c)
fn style_item_color(color) -> StyleItem          // and _image, _nine_slice, _hide
fn rgb(r, g, b: i64) -> Color     // and rgba, rgba_u32, rgb_f, rgba_f, rgb_cf, rgb_hex, hsv, hsva, ...
fn color_cf(c: Color) -> Colorf   // and color_u32, color_hex_rgba, color_hsv_f, color_hsva_f
fn rgb_factor(c: Color, factor: f32) -> Color
```

`Style` mirrors Nuklear's `nk_style` field for field (text, link, buttons,
toggles, selectables, sliders, knobs, progress bars, properties, edits,
charts, scroll bars, tabs, combo boxes, windows), less its font and
cursors: read it, change fields, set it back. A `StyleItem` is a color, an
image or a nine-slice image, by `kind`. Software cursors are images Nuklear
draws at the mouse.

### Images and rectangles

```goose
struct Rect { x: f32, y: f32, w: f32, h: f32 }
struct Image { texture: u32, w: u16, h: u16, region: u16[4] }    // a renderer's texture
fn rect(x, y, w, h) -> Rect       // f32 or i64; and recta, rect_pos, rect_size, get_null_rect
fn image_id(texture: u32) -> Image               fn texture_image(t: gfx::Texture) -> Image
fn subimage_id(texture, w: u16, h: u16, region: Rect) -> Image     // part of a w x h texture
fn nine_slice_id(texture, l, t, r, b: u16) -> NineSlice    // and sub9slice_id, with the size
```

### Drawing and what was drawn

```goose
fn stroke_line(c, x0, y0, x1, y1: f32, thickness: f32, color: Color)
    // and stroke_curve, _rect, _circle, _arc, _triangle, _polyline, _polygon
fn fill_rect(c, r: Rect, rounding: f32, color)
    // and fill_rect_multi_color, _circle, _arc, _triangle, _polygon
fn draw_image(c, r: Rect, img: Image, color)     fn draw_nine_slice(c, r, slice: NineSlice, color)
fn draw_text(c, r: Rect, text, font: Font, background: Color, foreground: Color)
fn push_scissor(c, r: Rect)
fn commands(c) -> Command[>..]    fn command_points(c) -> int2[>..]    fn command_text(c) -> u8[>..]
fn convert(c, config: ConvertConfig) -> i64      // CONVERT_SUCCESS or what went wrong
fn vertices(c) -> Vertex[>..]     fn indices(c) -> u32[>..]    fn draw_commands(c) -> DrawCommand[>..]
fn null_texture(c) -> u32
```

The canvas functions draw onto the window being built, in screen
coordinates, clipped to the scissor: with `widget`, the way to draw a
widget of the program's own. Once a frame's windows have ended, its drawing
is available two ways, for any renderer: Nuklear's commands (one `Command`
per shape, image or text, polygons' points and texts' bytes beside them),
or triangles from `convert` -- vertices with a position, texture
coordinate and color, 32-bit indices, and draw calls each with a scissor
and a texture, shapes using the null texture. Both stay until the next
frame's input.

### Through gfx

```goose
fn render(c: Context)                            // over the screen
fn render(c, target: gfx::Texture)               // over a COLOR_TARGET texture
fn render(c, target, config: ConvertConfig)
fn input_from_gfx(c: Context)                    // after gfx::frame(), before any window
```

`render` converts the frame and draws it with alpha blending, not inside a
pass, uploading each font atlas to a gfx texture the first time and
releasing it with the atlas (`released_textures()` lists those destroyed,
for a renderer of the program's own). What it keeps -- a pipeline, a
sampler, buffers -- is made once and again after gfx is closed and
opened. `input_from_gfx` hands the context gfx's last frame
of input in order: the mouse, editing keys (Delete, Backspace, Enter, Tab,
Home, End, the arrows, Page Up and Down, Escape), shortcuts with Ctrl or
Command (A, C, X, V, Z, Y), typed text (it turns gfx's text input on), the
frame's duration, and the clipboard both ways, pasting the system
clipboard's text and putting there what a field copied. Both work in the
screen's pixels, so on a high-density display the ui is as many pixels as
elsewhere, and smaller, until the program sets a scale.
