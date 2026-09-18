![The Goose programming language](docs/logo/logo_black_fg_white_bg_mini.png)

# The Goose Programming Language

A memory-safe systems language that is faster than C++ and Rust, on less
memory, with no allocator/GC and no lifetime annotations.

[Tutorial](docs/tutorial.md) · [Specification](docs/goose_spec.md) ·
[Samples](samples/README.md) · [Benchmarks](bench/summary.md) ·
[Standard library](docs/stdlib.md)

Goose looks familiar like C or Rust, and
is built on one idea: **there is no heap**. Every dynamic value lives inline on
a data stack the compiler manages, growth is a pointer bump, and scope exit is
the only free. The rest of the language is what it takes to make that work for
real programs, and what it buys is measurable.

## Why Goose

* **Faster than C++ and than safe Rust, while memory safe.** Over sixteen
  benchmarks Goose runs at 3.3x the speed of idiomatic C++, 1.16x hand-optimized
  C++ and 1.12x the best safe Rust, on 1.9x, 1.3x and 1.2x less memory
  ([summary](bench/summary.md), [full results](bench/results.md)). The wins are
  structural: they come from things the other languages cannot express.
* **No allocator, no GC, no reference counting, no destructors.** Memory is a
  handful of data stacks that the compiler assigns statically. Freeing a
  million-element structure is one store, however deeply it nests.
* **Nothing ever moves.** A reference into a growing array stays valid for as
  long as the array does. You keep typed references where C++ must `reserve`
  and safe Rust retreats to `u32` indices.
* **Memory safe with zero annotations.** No lifetime syntax, no aliasing or
  exclusivity rules, no `unsafe`. The compiler infers what every reference is
  rooted in and objects to exactly one thing: outliving the owner.
* **Flat all the way down.** A string, an array of strings, a record with
  variable-size fields and an array of those records are each one contiguous
  block with no pointer in it. A record that is 160 bytes and an allocation in
  C++ is 29 bytes and none in Goose.
* **Enums that cost what they hold.** Variable-mode ADTs give each value its own
  variant's size rather than the largest one's: 4x less memory and 2x the speed
  of a Rust `enum` on the benchmark that exercises it.
* **Links narrower than pointers.** A relative reference stores a typed, checked
  link as a 1, 2 or 4-byte offset. Structures built from them are position
  independent, so your data structure is already its file format: saving is a
  write, loading is a read plus a verification pass that rejects hostile bytes.
* **Everything is built in place, guaranteed.** A value is constructed at its
  final destination through any depth of calls. `items.push(parse(line))`
  writes the parsed record straight into the array, and returning a growable
  array by value costs nothing.
* **Errors without plumbing.** `return err from load` returns from a function
  any number of frames up, statically checked, with no unwinder, no `Result`
  type and no `?` on every call.
* **Threads that share nothing.** A worker is compiled as a separate program
  with its own memory, and flat values cross typed queues as a `memcpy`. Data
  races, locks, atomics and memory orderings do not exist in the language.
* **Generics and higher-order functions with no overhead.** An untyped
  parameter is generic. Function values are compile-time entities, so
  `xs.filter() { it > 0 }` compiles to the loop it looks like and builds its
  result straight into its destination.
* **Plain C in, plain C out.** Goose compiles to one C file, so it runs wherever
  a C compiler does and calls C directly through `extern fn`. The bundled
  TinyCC backend compiles and runs a program in-process, with no build step.

The [tutorial](docs/tutorial.md) walks through all of this by example, the
[specification](docs/goose_spec.md) has the exact rules, and the
[benchmarks](bench/summary.md) have the numbers, losses included.

## Features

Only what is different about Goose is shown here. The [tutorial](docs/tutorial.md)
covers the same ground properly, and the [samples](samples/README.md) are
twenty-six complete programs doing it for real.

### One memory model: stacks, and scope exit is the free

A program has the native call stack, static data and N data stacks, where the
compiler works out N. A data stack is a large address-space reservation with a
bump pointer, and there is no other memory. At most one resizable value is
live per stack and it is always on top, so growth never moves anything and
never checks a capacity. All of this is proved at compile time; the runtime
keeps nothing but the bump pointers.

```goose
for round in 3 {
    var scratch: u8[>..] = [];                        // grow-only: growth is a pointer bump
    for i in 100000 { scratch.push((i % 256) as! u8); }
    print("round ", round, ": ", scratch.len, " bytes");
}                                                     // the free: one store to the stack top
```

### Nothing moves, so references survive growth

```goose
struct Item { id: i32, weight: f32 }

var items: Item[>..] = [];
let first .= items.push(Item { 1, 0.5 });             // a reference to element 0
for i in 2..1000001 { items.push(Item { i as i32, 0.0 }); }
first.weight = 99.5;                                  // still valid, a million pushes later
```

`push` returns a reference to the element it just made, which is how data gets
linked up while it is being built. A `vector<T>` or `Vec<T>` reallocates, so
neither can promise this, and it is where a good share of the benchmark wins
come from.

### Safe references, no annotations

Every reference and slice carries a static *root*, the variable that bounds its
target's lifetime, and the whole lifetime system is one rule: a reference must
not outlive the variable that owns its target, and must never see it at a wrong
type. Roots are inferred and functions are specialized per root, so there is no
syntax for any of it, and no aliasing or exclusivity rules either. When the
checker objects, it names both ends:

```goose
fn longest(a: u8[:], b: u8[:]) -> u8[:] { if a.len >= b.len { a } else { b } }

var outer: u8[>..] = [];
var w = outer[..];
{
    var inner: u8[>..] = [];
    format(inner, "inner text");
    w = longest(outer, inner);     // error: storing a reference rooted at inner,
}                                  //        which does not outlive the destination (§9.2)
```

A slice `T[:]` is the universal "process a range" parameter: every array kind
coerces to it for free, and it never copies, so `split` returns slices into its
input and a dictionary keyed by `u8[:]` stores no strings at all.

### Flat all the way down

There is no single array type. There is a family that differs only in what
happens to the size, and every member is `[metadata][elements...]`, inline and
packed, never a pointer to an element block:

| Spelling | What it is |
|---|---|
| `T[k]` | fixed size, known at compile time |
| `T[]`, `T[varint]` | sized at construction, frozen after; a fixed or variable-width length |
| `T[..k]`, `T[..]` | capacity inline; grows and shrinks within it |
| `T[>..]` | **grow-only**: the workhorse; references into it stay valid |
| `T[>..<]` | **grow-shrink**: stacks, queues, heaps |

Strings are just `u8` arrays: `u8[>..]` is a builder, `u8[]` a finished string
stored inline in whatever holds it, `u8[..16]` a small string inside a struct,
`u8[:]` a view. A struct may contain variable-size parts, and they sit inline
in declaration order, so a record is a run of bytes with nothing indirect in it:

```goose
struct Item  { sku: u8[varint], qty: varint, cents: varint }
struct Order { id: varint, customer: u8[varint], items: Item[varint] }

var book: Order[>..] = [];
book.push(Order { id: 1001, customer: "alice",
                  items: [Item { sku: "SKU-441", qty: 2, cents: 1999 },
                          Item { sku: "SKU-7", qty: 1, cents: 500 }] });
```

That order is **29 bytes and 0 allocations**. As C++ `std::string` +
`std::vector<Item>` it is 160 bytes and 1 allocation, and as Rust `String` +
`Vec<Item>` 153 bytes and 4. A whole order book is one array that streams
through the cache. The price is that an array of variable-size elements is
sequential: you can iterate it but not index it.

### Enums in two sizes

Algebraic data types are the only dynamic polymorphism: no inheritance, no
vtables. Every ADT can be stored two ways, chosen at the point of use. Fixed
mode (`Shape`) is a tag plus room for the largest payload, indexable and
overwritable. Variable mode (`Shape..`) gives each value exactly its variant's
size; the array becomes sequential and a value never changes variant, and in
exchange you may take references *into* a payload:

```goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

var packed: Shape..[>..] = [];                 // 9, 17 and 1 bytes, not 17 each
packed.push(Shape.Circle { r: 1.0 });
packed.push(Shape.Rect { w: 2.0, h: 3.0 });
packed.push(Shape.Dot);
for s in packed {
    match s { Rect &r => { r.w += 1.0; }, _ => {} }   // edits the payload in place, inside the array
}
```

`match` has a second spelling, *case functions*: one overload per variant,
dispatched on the tag through a jump table and checked for exhaustiveness. It
is the virtual call without the vtable:

```goose
fn area(s: Shape.Circle) -> f64 { 3.14159 * s.r * s.r }
fn area(s: Shape.Rect) -> f64   { s.w * s.h }
fn area(s: Shape.Dot) -> f64    { 0.0 }

for s in packed { print(s, " area ", area(s)); }
```

### Links narrower than pointers

A `T&` is a machine address: eight bytes, never dangling. A *relative reference*
is the same link stored as a narrow offset: `T&<u32>` is measured from the field
itself to a target in the same array, so the structure is position independent,
and `T&<u32 in pool>` from a named pool's base, so other arrays can link into
the pool. `T&<u32>?` uses offset 0 as null. Here is a binary search tree in one
grow-only array, with 12-byte nodes:

```goose
struct Node { key: i32, left: Node&<u32>?, right: Node&<u32>? }   // 12 bytes, links included

fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];                             // .= binds a reference; = would copy the node
    loop {
        if key == cur.key { return; }
        let next = if key < cur.key { cur.left } else { cur.right };
        if next { cur .= next; continue; }          // narrowed by `if`: next is a Node&
        if key < cur.key { cur.left .= pool.push(Node { key: key }); }
        else             { cur.right .= pool.push(Node { key: key }); }
        return;
    }
}
```

References are transparent (no `*`, no `->`), `.=` binds or retargets one, and
`Node?` is a nullable reference that `if`, `guard` and `assert` narrow. The
insert is one push and one store, and the push cannot invalidate `cur`. When
lifetimes are not nested, a `reusable` pool pairs a grow-only array with a
hidden freelist: `alloc_ref` reuses a slot or pushes, `free` hands one back,
and since nothing is ever actually freed, a stale slot reads a *different value
of the correct type* rather than corrupting memory.

### Your data is already a file format

```goose
var image: u8[>..] = tree.to_bytes();           // a framed byte image of the whole array
var loaded, ok = from_bytes<Node[>..]>(image);  // verified before it becomes a value
image[image.len - 3] = 200;                     // tamper with a link ...
var bad, bok = from_bytes<Node[>..]>(image);    // ... and it is false and an empty array
```

The tree's links are self-relative, so it means the same thing wherever it
sits: saving is writing its bytes and loading is reading them back, with no
serializer, no schema and no pointer fixups. `from_bytes` checks the framing,
every tag, every length and every link before the bytes become a value, so a
corrupt or hostile file is a `false`, never a wild reference.

### Built in place, always

The copy-free construction guarantee ([spec §4.3](docs/goose_spec.md)) says a
constructed value is always built in its final home, propagated top-down
through calls. A function returning a grow-only array by value writes its
elements straight into the caller's variable, or into a field of the record
being built inside another array, so out-parameters mostly do not appear.

```goose
words.push(str("word", i));                    // formatted straight into the new element
let evens = xs.filter() { it % 2 == 0 };       // built straight into `evens`: no temporary
book.push(Order { id: 1001, customer: "alice", items: parse_items("SKU-441:2:1999;SKU-7:1:500") });
```

### Errors without plumbing

```goose
struct User { name: u8[..16], age: i32 }

fn load(text: u8[:]) -> User[>..], u8[] {
    var users: User[>..] = [];
    each_split(text, '\n') { users.push(parse_user(it)); };
    return users, "";
}

fn parse_user(line: u8[:]) -> User {              // returns a User: no Result, no error parameter
    let comma = find(line, ",");
    guard comma >= 0 else { return [], str("expected name,age: ", line) from load; }
    let age, ok = parse_int(line[comma + 1..]);
    guard ok else { return [], str("bad age: ", line) from load; }
    let name = trim(line[..comma]);
    return User { name: name, age: age as i32 };
}
```

`return E from f` returns `E` as the result of the innermost active call of
`f`, however many frames up, and every function in between keeps its plain
signature. It is checked statically, so every call of `parse_user` must lie
inside a `load` call and nothing is uncaught at runtime. It is a hidden
discriminant checked per frame rather than an unwinder, and the message is
built directly where `load`'s caller wants it. The parsers in the samples use
it for every syntax error.

### Generics without ceremony, blocks that disappear

```goose
fn twice(x) { x + x }                              // an untyped parameter is a generic one

fn each_pair<T, F>(xs: T[:]) {                     // F is a function value: a compile-time entity
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}

fn first_gap(xs: i64[:]) -> i64 {
    each_pair(xs) { a, b => if b - a > 1 { return a; } };   // returns from first_gap, not each_pair
    return -1;
}

let evens = xs.filter() { it % 2 == 0 };
let total = fold(xs, 0) { acc, x => acc + x };
sort(xs) { a, b => a > b };
```

Everything is monomorphized and type arguments are inferred, never written at a
call. Function values are passed as generic parameters: every call is direct
and inlinable, they cannot escape, and there are no closure objects or function
pointers, so a higher-order function compiles to exactly the loop it looks
like. A block may `return` from its lexically enclosing function, and nested
functions see the enclosing function's locals.

### Threads that share nothing

```goose
thread_fn worker() {                                // a separate program with its own memory
    loop {
        let job = qget<Job>();                      // blocks on the typed queue for Job
        guard job.n >= 0;                           // -1 means stop
        qput(Result { n: job.n, digits: count_digits(job.n) });
    }
}

for i in n { ids.push(thread_spawn(worker)); }
```

There is no shared mutable memory. A `thread_fn` and everything it calls is
compiled as its own program with its own data stacks and its own copies of the
globals. Values cross through typed queues, one per type, and must be *flat*,
with no references at any depth. That is cheap because a flat Goose value is
contiguous: a job or a result carries real data, pixels included, and crossing
is a `memcpy`.

### Plain C in, plain C out

```goose
extern fn hypot(x: f64, y: f64) -> f64;             // straight from libm
extern fn crc32_bytes(s: const u8[:]) -> u32;       // a slice crosses as { data, len }
extern fn stats_of(xs: i32[:], out: Stats&);        // a struct filled through a pointer
```

An `extern fn` binds a Goose signature to a C symbol, and that is the whole FFI;
the `math` and `os` modules are built on it. Exactly what has a plain C shape
may cross, and anything else is rejected at the declaration. The compiler emits
one C file for the whole program, which any C compiler builds, and with the
bundled TinyCC it compiles and runs the program inside its own process instead.

### Where the speed comes from

Not a clever optimizer. No allocator on any path, no teardown, contiguous data
so the cache does less work, narrow links, and enums that do not pay for their
largest variant everywhere. Every index is bounds-checked and the compiler
proves most checks away; one `assert` on a slice length is usually what a
kernel needs to lose all of them and vectorize, and `--bce-lines` reports what
survived. Every measurement is whole-process wall clock, teardown included.

| Geometric mean over 16 benchmarks | vs idiomatic C++ | vs hand-optimized C++ | vs best safe Rust |
|---|---:|---:|---:|
| speed, MSVC backend | 3.30x | 1.16x | 1.04x |
| speed, clang backend | 3.35x | 1.19x | 1.12x |
| peak memory | 1.93x less | 1.30x less | 1.24x less |

The [summary](bench/summary.md) says where each win comes from and owns up to
the losses; [results.md](bench/results.md) has every row and
[design.md](bench/design.md) says what the suite was built to find out.

### What it costs you

* You think about where data lives: who owns this, and how long does its scope
  last. Usually the answer is "the function that builds it", and that is free;
  when it is not, it is a `reusable` pool.
* Recursive functions cannot own growable data; they take the pool as a
  parameter.
* Arrays of variable-size elements iterate but do not index, a fixed-mode enum
  cannot be pointed into, and a variable-mode one cannot be overwritten. You
  choose per container.
* No escaping closures, no function pointers, no dynamic dispatch beyond ADT
  tags, and whole-program compilation only.
* A slot handed back to a `reusable` pool and still named reads whatever its
  next owner put there: a logic bug, never memory corruption.

[Tutorial §18](docs/tutorial.md#18-what-it-costs-you) is the full list.

## Build and run

You need CMake 3.20 or later, a C++20 compiler (MSVC, clang or gcc) and Python
3 for the test and sample runners. Two submodules are optional: TinyCC, which
the in-process backend is built from, and SDL3, which the `gfx` graphics module
is built from. Without either the compiler builds and behaves the same, minus
JIT mode or minus running `gfx` programs.

```bash
git clone --recursive https://github.com/aardappel/goose
cd goose
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

To leave SDL out, clone without `--recursive` and then run `git submodule
update --init third_party/tinycc`, or configure with `-DGOOSE_GFX=OFF`. On
Linux, SDL needs the X11 or Wayland development packages
([`third_party/SDL/docs/README-linux.md`](https://github.com/libsdl-org/SDL/blob/main/docs/README-linux.md));
without them the build leaves the `gfx` module out and says what to install.

The compiler is `build/goose` (`build/Release/goose.exe` with the Visual Studio
generator), and it finds the standard library in the source tree it was built
from. Run a program straight from source, in-process:

```bash
build/goose samples/01_tour.goose
```

Or generate C and build it with whatever compiler is around. Programs that use
threads need this route, since TinyCC cannot place thread-local storage:

```bash
build/goose -o tour.c samples/01_tour.goose && cc tour.c -o tour -lm -pthread && ./tour
```

On Windows that is `cl tour.c`. Useful flags: `--check` typechecks without
emitting C, `-O0`/`-O1`/`-O2` set the inlining level, `--bce-lines` reports the
bounds checks kept per line, and `-DGS_DEBUG=1` turns on the overflow, range
and tag checks in the generated C.

A program using `gfx` also links the graphics layer and SDL, which the compiler
names in a response file:

```bash
build/goose samples/27_gfx_cube.goose
build/goose -o cube.c samples/27_gfx_cube.goose && cc cube.c -o cube @$(build/goose --gfx-link cc)
```

With MSVC that is `cl cube.c @<the path goose --gfx-link msvc prints>`.

The test suite and the samples run on Windows, macOS and Linux:

```bash
python test/run_tests.py
python samples/run_samples.py
```

For editing, the [VS Code extension](vscode/README.md) gives syntax
highlighting, compiler checks on save and one-key runs through the JIT:

```bash
code --install-extension vscode/goose-language.vsix
```

## Documentation

* [Tutorial](docs/tutorial.md): the friendly introduction, by example. Read
  this first.
* [Language specification](docs/goose_spec.md): the exact rules, when you want
  to know why something did not compile.
* [Samples](samples/README.md): twenty-seven complete programs in reading
  order, from a tour of the language to a JSON parser, a threaded Mandelbrot, a
  file tree built from two pools and a spinning cube on the GPU.
* [Standard library](docs/stdlib.md): six modules, all readable Goose under
  `stdlib/`, including `gfx`, graphics on SDL3's GPU API
  ([how it is built](docs/design/gfx.md)).
* [Benchmarks](bench/summary.md): the numbers, with the
  [full results](bench/results.md) and the [design](bench/design.md) behind them.
* [Implementation notes](docs/implementation.md): how the compiler works, pass
  by pass, and [how it is tested](docs/testing.md).

## Status

Goose is new. What exists today is a whole-program compiler of about thirty
thousand lines of C++, the specification, the standard library, the samples,
and a test suite that CI runs on Windows, macOS and Linux with an extra
sanitizer job. Deliberately out of scope for now: moves for resizable values,
more than one resizable per struct, labeled `break`, namespace privacy, and
more OS/library access. Open items are tracked in the
[specification's Appendix B](docs/goose_spec.md#appendix-b-todo--open-items).

## License

Goose is licensed under the [Apache License, Version 2.0](LICENSE).

## Authors

**Wouter van Oortmerssen**: Language Design, Compiler Design, Coding standards.
[home](https://strlen.com/) page, [twitter](https://x.com/wvo).

**Claude Fable**: Compiler implementation, Benchmarking, Sample & Doc writing.

## History & Use of AI

Yes, this repo is almost entirely AI produced, though from a human design.
I had designed Goose several years ago, and had started to implement it,
but running a game startup (which is built on another programming language of mine,
[Lobster](https://strlen.com/lobster/)) there was no time to finish it.
Which was sad, because I knew Goose could do things other languages can't,
and it should exist.

I had not considered AI being able to help with this, until Fable came out and
I figured it might have reached a level to be able to do good job of it.
I made it essentially clone the style and structure of my other recent compiler
(Lobster), which is why if you look at the code, it looks rather similar to that.
My initial design had left lots of things unspecified, and lots of back and forth
with Fable made me decide on all of those, and it is now a better language for it.

It is also an experiment: though certainly not the first ever compiler implemented
with AI, possibly one of the more novel/extensive from scratch ones. This project is very different from cloning an existing language.

You may wonder why I had it work in C++, if clearly I could have used any
language, like Rust, or my own Lobster, or.. Goose itself (that may still happen).
With Fable, it was my observation that it is almost equally capable in any
language, and the strong guardrails of Rust or other languages are not as pressing
as they once were.
Since the compiler emits C, you likely already need to have a C/C++ compiler around,
so sticking inside one ecosystem would seem to simplify deployment and adoption.
I've also integrated libtcc and who knows what other C libraries in the future.
And, like I said, to have the compiler code mimic my existing Lobster compiler seemed
fun, at least I can read it like it is my own.
