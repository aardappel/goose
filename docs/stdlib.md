# The Goose standard library

Six modules under `stdlib/`, found by `import std;` (and `dictionary`,
`vec`, `math`, `os`, `gfx`) wherever the compiler was built from a source
tree, or through `--stdlib <dir>` / `GOOSE_STDLIB`. Everything is written in
Goose except the C behind `os` (`src/runtime/runtime_os.h`), libm behind
`math` and the graphics layer behind `gfx` (`src/gfx/`), all reached through
`extern fn` (spec §7.10). The design and its rationale are in
`design/stdlib_design.md` (and `design/gfx.md` for `gfx`); this is the
reference.

Conventions that hold throughout:

* A function that reads or mutates elements in place takes `xs: T[:]`; every
  array kind and every slice coerces to it: `sort(arr)`, `sort(arr[1..])`.
* A function that changes the length takes `xs: A&`, the whole array by
  reference, and instantiates for whatever array kind it is given, provided
  that kind has the operations used. A grow-only array shrinks through the
  reference wherever nothing in the caller points into it (spec §5.1), so
  `remove_at(xs, i)` works on a `[>..]` local; the checker reports the
  call while a slice of `xs` is in scope.
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
fn format_uleb(out: u8[>..]&, v: i64)                     // LEB128 (3.6), 1-10 bytes
fn parse_uleb(s: u8[:]) -> i64, i64                       // value and byte count; 0, 0 if malformed
fn compare(a: u8[:], b: u8[:]) -> i64                      // bytewise: -1, 0, 1
fn trim(s: u8[:]) -> u8[:]                                 // ASCII whitespace; a sub-slice
fn trim_start(s: u8[:]) -> u8[:]                           fn trim_end(s: u8[:]) -> u8[:]
fn split(s: u8[:], sep: u8) -> const u8[:][>..]            // read-only slices into s; empty parts kept
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

`format_uleb`/`parse_uleb` are the framing prefix an image carries
(`design/serialization.md`): writing a save's header by hand is what lets its
payload be a `bytes_of` view rather than a copy, and reading the prefix is
how a stream reader knows how many bytes an image still needs.

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
where the keys are literals or views of a `let`, §9.5), or as inline
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
last use is behind the mutation needs no block around it, and one still used
after the mutation is an error wherever it was declared. The key type is
`const u8[:]` because these keys are literals and views of a `let` (§9.5); a
dictionary whose keys are slices of a `var` buffer is `dictionary<u8[:], V>`.

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
fn read_file(path: const u8[:], out: u8[>..]&) -> bool     // appends the whole file
fn write_file(path: const u8[:], data: const u8[:]) -> bool
fn append_file(path: const u8[:], data: const u8[:]) -> bool
fn file_exists(path: const u8[:]) -> bool
fn delete_file(path: const u8[:]) -> bool
fn read_line(out: u8[>..]&) -> bool                  // stdin, newline stripped; false at end
fn read_stdin(out: u8[>..]&)                         // everything until end of input
fn write_stdout(s: const u8[:])   fn write_stderr(s: const u8[:])   fn flush_stdout()
fn arg_count() -> i64       fn arg(i: i64, out: u8[>..]&)
fn args() -> u8[:][>..]                              // indexable; argument 0 is the program
fn env(name: const u8[:], out: u8[>..]&) -> bool
fn time() -> f64                                     // seconds since the epoch
fn clock() -> f64                                    // monotonic, high resolution
fn time_ns() -> i64         fn clock_ns() -> i64
fn sleep(seconds: f64)      fn sleep_ms(ms: i64)
fn random_seed() -> u64                              // entropy, for rng
```

`exit(code)` and `abort(msg)` are builtins, since the checker knows they
diverge. Directory listing, subprocesses and networking are not in v1; they
arrive as `extern fn`s when a program needs them.

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
`WINDOW_HIGH_DPI`, `NO_VSYNC`, `DEBUG` (validation layers where installed; on
in a debug build of the layer). With `GOOSE_GFX_HEADLESS=1` in the environment
`open` opens no window, as the test runners use it. The screen is an RGBA8
texture of the window's size in pixels, with a depth texture: a frame draws
into it, and `frame()` shows it.

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
fn mouse_pos() -> float2    fn mouse_delta() -> float2    fn mouse_wheel() -> f32
fn inject_key(name: const u8[:], down: bool) -> bool           // as if typed, seen at the next frame()
fn inject_mouse(x: f32, y: f32, button: i64, down: bool)       // button 0 only moves
```

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

