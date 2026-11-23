# Goose for C++ and Rust Programmers: Translating Common Patterns

Oct 22, 2026 · @Ben

Most C++ and Rust code translates to Goose once you stop asking "where do I allocate this?" and start asking "which scope owns this, and what array does it live in?" This guide takes patterns you already write daily and shows the Goose shape of each, with the reason it differs.

It is drawn from the official [tutorial](https://aardappel.github.io/goose/docs/tutorial.html), [language reference](https://aardappel.github.io/goose/docs/reference.html), [standard library](https://aardappel.github.io/goose/docs/stdlib.html) and design notes. The examples use syntax shown in those documents; check the reference when something does not compile. Most examples assume `import std;` at the top of the file (for `trim`, `split`, `sort`, `filter`, `remove_at` and friends), and the `dictionary` examples also need `import dictionary;`.

## The one shift: memory is a few stacks

Goose has no `malloc`, no `free`, no GC, no reference counting and no destructors. Memory is the native stack plus a handful of **data stacks** the compiler sizes for your program. Four rules follow, and almost every pattern below is a consequence of them:

1. **Every dynamic value is owned by a variable.** A local, a global, a by-value parameter, or a field or element of one. Nothing floats free on a heap.
2. **Scope exit is the free.** When the owning scope ends, the stack pointer moves back. One store, no walk, no destructors.
3. **Nothing moves.** A growable array is always the top of its own stack, so `push` never reallocates. A reference into it stays valid after a million pushes, which a `Vec` or `std::vector` cannot promise.
4. **Values nest inline.** A string inside a struct inside an array is bytes inside bytes inside bytes, with no pointers between them.

The compiler proves all of it statically. There are no lifetime annotations; references carry an inferred *root* (the variable that owns their target) and may not outlive it.

On top of that, the type system is closed-world: no inheritance, no traits, no vtables, no function pointers, no escaping closures. Dynamic polymorphism is algebraic data types and nothing else.

**How to read this guide.** Each section names the C++/Rust habit, shows it, then shows the Goose version. The quick reference table right below is the index.

## Quick reference

| You write in C++ / Rust | You write in Goose | Section |
| --- | --- | --- |
| `new T`, `make_unique`, `Box::new` | A local, or `push` into an array that outlives you | Heap allocation |
| `vector<T>`, `Vec<T>` | `T[>..]` (grow-only); `T[>..<]` if you pop | Containers |
| `string`, `String` | `u8[>..]` builder; `u8[]` finished; `u8[..16]` inline | Containers |
| `string_view`, `&str`, `span`, `&[T]` | `T[:]` slice (checked, can't dangle) | Containers |
| `x.clone()`, copy constructor of a vector | `copy(x)`, always explicit | Heap allocation |
| Virtual method, `dyn Trait` | `enum` + `match`, or case functions | Virtual functions |
| `vector<unique_ptr<Base>>` | `Shape[>..]` (fixed mode) or `Shape..[>..]` (packed) | Virtual functions |
| Trait bound, C++ concept | Untyped / `<T>` generic, checked per instantiation | Traits |
| `impl Hash for K`, `operator<<` | A free `hash(k: K)` or `format(out, v: K)` overload | Traits |
| Methods, `impl` blocks | Free functions; `x.f(a)` is `f(x, a)` | Traits |
| `std::function`, stored `Box<dyn Fn>` | Compile-time function value; stored behaviour becomes an enum tag | Callbacks |
| `Box<Node>` tree, `Vec<Node>` + `u32` indices | `Node[>..]` pool + `Node&<u32>?` links | Linked structures |
| `shared_ptr`, `Rc<RefCell<T>>`, object pool | `slot_pool<T>` from the standard library + `alloc_ref` / `free` | Non-nested lifetimes |
| Struct owning three `Vec`s | A `root struct` whose three fields grow independently | Structs that own several vectors |
| `Result<T, E>`, `?`, `throw` | Trailing `bool`, `T?`, `return E from f`, `exception::try` | Errors |
| RAII destructor | Scope exit (memory); explicit `release` (external handles) | RAII |
| `Arc<Mutex<T>>`, `std::thread` + shared data | `thread_fn` + typed queues of flat values | Threads |
| serde, protobuf | `to_bytes` / `from_bytes<T>` / `bytes_of` | Serialization |
| `std::optional<T&>`, `Option<&T>`, `nullptr` | `T?`, narrowed by `if` / `guard` | Smaller habits |
| `std::tuple`, `(a, b)` returns | `-> A, B` multiple results (no tuple type) | Smaller habits |

## Heap allocation: there is no heap

In C++ and Rust you allocate when a value's size is dynamic or when it must outlive the current call. In Goose the first case is just a local, and the second is a value returned by value or pushed into an array owned further up.

### "I need a buffer of runtime size"

```
// C++
auto buf = std::make_unique<uint8_t[]>(n);
// Rust
let mut buf = vec![0u8; n];
```

```
// Goose: a local. It lives on a data stack and is freed at the closing brace.
var buf: u8[>..] = [];
buf.resize(n, 0);
```

A scratch buffer in a loop costs one pointer store per iteration to free, so refilling a fresh local each time is idiomatic, not wasteful.

### "I build something and hand it back"

C++ programmers often pass an out-parameter to avoid a copy; Rust returns a `Vec` and relies on a move. Goose returns by value and the result is **built directly in the caller's destination**, through any number of calls. There is no temporary and no move.

```
fn squares(n: i64) -> i64[>..] {
    var out: i64[>..] = [];
    for i in n { out.push(i * i); }
    return out;           // `out` *is* the caller's variable from its declaration on
}

let sq = squares(10);     // no copy

// Also works into a field of an element being pushed (from the tutorial):
book.push(Order { id: 1001, customer: "alice",
                  items: parse_items("SKU-441:2:1999;SKU-7:1:500") });
```

So prefer return values over out-parameters; both cost the same, and the return value reads better. `str(...)` follows the same rule: `names.push(str("item", i))` formats directly into the new element.

### "I need my own copy"

Fixed-size values (scalars, structs of fixed fields, `T[k]`, references, slices) copy silently on `=`, like C. Anything whose size isn't known at compile time **never copies implicitly**: `let b = a;` binds `b` to `a` by reference. You write `copy(a)` to get an O(n) copy, so every expensive copy is visible in the source.

```
var a: i64[>..] = [1, 2, 3];
let alias = a;        // same array
var mine = copy(a);   // independent
```

### "The object must outlive this function"

The question becomes *which scope should own it*. Usually the answer is a caller: the caller declares the array and passes it down as `T[>..]&`; the callee pushes into it and gets back a reference from `push`. When no scope fits because lifetimes truly interleave, use a `reusable` pool (see Non-nested lifetimes).

## Containers and strings: pick the array by what its size does

There is no single `vector`. The array family differs only in what happens to the size, and every member stores `[metadata][elements]` inline with no pointer to a separate block.

| You want | C++ / Rust | Goose |
| --- | --- | --- |
| A growable list you keep references into | `vector<T>` / `Vec<T>` (references invalidated on growth) | `T[>..]` grow-only: the default |
| A stack, queue or heap | `vector` + `pop_back`, `BinaryHeap` | `T[>..<]` grow-shrink, with `heap_push` / `heap_pop` from `std` |
| A small bounded list inside a struct | `std::array` + count, `ArrayVec`, `SmallVec` | `T[..k]` (fixed-size, inline) |
| Built once, then read-only | `vector` you stop touching, `Box<[T]>` | `T[]` or `T[varint]` |
| Compile-time size | `std::array<T, N>`, `[T; N]` | `T[k]` |
| A view of someone else's elements | `span<T>`, `&[T]` | `T[:]` |

Two rules shape how you use them. At most one *resizable* value (`T[>..]` or `T[>..<]`) may sit in a struct, and only as its last field. And an array's elements may not themselves be resizable.

### `vector<string>` and `Vec<String>`

```
var words: u8[][>..] = [];
for i in 5 { words.push(str("word", i)); }
```

This is one contiguous block: a count, then five `(length, bytes)` runs. No per-string allocation, no pointer per element. The cost is that variable-size elements make the array **sequential**: you can iterate it but not index it (`words[2]` is a compile error).

If you need to index or sort, make each element fixed-size with an inline capacity:

```
var names: u8[..16][>..] = [];   // each name up to 16 bytes, indexable, sortable
```

Or keep the compact array and an index of references beside it (Linked structures shows this).

### String building (`ostringstream`, `format!`, `String::push_str`)

There is no string type: a string is a `u8` array. `print`, `format` and `str` take any number of arguments of any type.

```
var s: u8[>..] = [];
s.append("hello");
s.push(',');
format(s, " world! ", 6 * 7);       // appends the text of anything
let t = str("x=", x, " y=", y);    // a fresh string, built at its destination
print("answer=", 42, " ratio=", 0.75);   // no format string
```

A `u8[..16]` field is a builder too, up to capacity: `format(p.name, "linus")`.

### `string_view`, `&str`, `split`

Slices are free and checked, so library functions return views rather than new strings. `split` returns an array of slices into its input; no text is copied.

```
let body = trim(line);
let eq   = find(body, "=");
let key  = trim(body[..eq]);
let parts = split("alice,30,paris", ',');
each_split(text, '\n') { handle(trim(it)); };   // no array built at all
```

Slices make good hash keys: `dictionary<const u8[:], i32>` stores addresses and lengths into text you already hold. The `const` is there because string literals and `split` results are read-only views.

### Erasing from the middle, retaining, dedup

These are library functions taking the array by reference: `remove_at`, `swap_remove`, `insert_at`, `retain`, `dedup`. A grow-only array may shrink only where the compiler can see nothing still points into it; if you hold a slice into it that is used later, the call is rejected and the error names the slice.

## Virtual functions and trait objects become enums

Goose has no inheritance, no traits and no vtables. Its only runtime polymorphism is the `enum` (an algebraic data type). That covers the overwhelmingly common case: a known, closed set of kinds that you store together and act on uniformly.

### The classic shape hierarchy

```
// C++
struct Shape { virtual double area() const = 0; virtual ~Shape() = default; };
struct Circle : Shape { double r; double area() const override { return 3.14159*r*r; } };
struct Rect   : Shape { double w, h; double area() const override { return w*h; } };
std::vector<std::unique_ptr<Shape>> shapes;

// Rust
trait Shape { fn area(&self) -> f64; }
let shapes: Vec<Box<dyn Shape>> = ...;
```

```
// Goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

// Option 1: case functions, one overload per variant. This is the virtual call.
fn area(s: Shape.Circle) -> f64 { 3.14159 * s.r * s.r }
fn area(s: Shape.Rect) -> f64   { s.w * s.h }
fn area(s: Shape.Dot) -> f64    { 0.0 }

// Option 2: one function with a match. Same dispatch, written inline.
fn name(s: Shape) -> u8[:] {
    match s {
        Circle c => "circle",
        Rect r   => if r.w == r.h { "square" } else { "rectangle" },
        _        => "other",
    }
}

var shapes: Shape[>..] = [];
shapes.push(Shape.Circle { r: 1.0 });
shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
for s in shapes { print(s, " area ", area(s)); }
```

Calling `area(s)` with a whole `Shape` dispatches through a jump table on the tag. Exhaustiveness is checked: add a `Triangle` variant and every case-function set and every `match` without `_` stops compiling until it handles it. That is the same guarantee a pure virtual gives you, enforced at the call sites instead of the class.

The trade-off is the usual one for sum types. Adding an *operation* is easy (write new case functions anywhere). Adding a *kind* means touching every operation. If you need an open set that third-party code extends at runtime (plugins), Goose cannot express it; see the last section.

### Base-class fields shared by every kind

Put the common data in a struct and the varying part in an enum field:

```
enum Kind { Player { lives: i32 }, Enemy { aggro: f32 }, Pickup { value: i32 } }
struct Entity { x: f32, y: f32, hp: i32, kind: Kind }

fn tick(e: Entity&) {
    match e.kind {
        Enemy en => { e.x += en.aggro; },
        _ => {},
    }
}
```

### Pick the storage mode at the use site

Every enum can be stored two ways, and you choose by the type you write, not the declaration:

|  | Fixed mode `Shape` | Variable mode `Shape..` |
| --- | --- | --- |
| Size | Tag + largest payload, every element (17 bytes here) | Exactly its own variant (Dot 1, Circle 9, Rect 17) |
| Array of them | Indexable | Sequential: iterate only, or keep an index beside it |
| Change an element's variant in place | Yes: `shapes[2] = Shape.Rect { .. }` | Never |
| Reference into a payload (`Rect &r =>`) | Never; `match` binds a copy | Yes, edit in place |
| Payloads of variable size (a string) | Not allowed | Allowed |

This is what replaces "a vector of pointers to heap objects of different sizes". Use fixed mode for things you index and mutate; use variable mode for compact trees, records and logs you walk.

```
var packed: Shape..[>..] = [];
packed.push(Shape.Circle { r: 1.0 });
packed.push(Shape.Rect { w: 2.0, h: 3.0 });
for s in packed {
    match s {
        Rect &r => { r.w += 1.0; },   // edits the payload inside the array
        _ => {},
    }
}
```

In fixed mode a `match` binder is a read-only copy. To mutate one variant's fields, build the new variant and assign the whole element.

### Visitor pattern, double dispatch

A visitor exists to fake pattern matching, so it disappears: write the `match`. For double dispatch (collide A with B), nest two matches, or write a case-function set whose body matches on the second argument. Case-function dispatch happens on one parameter position.

### Generic code over every variant

If you would reach for a derive macro or reflection ("print every variant's fields"), use `const for <N, V> in variants<T>` and `const for <F, A> in fields<V>`, which expand at compile time into ordinary code.

## Traits, concepts and methods become overloads and generics

Static polymorphism in Goose works like C++ templates without concepts: a generic body is checked only at each concrete instantiation, and everything is monomorphized. There are no trait bounds to declare. What a trait or a concept would *name*, you provide as an overload of a free function.

### Generic functions

```
fn twice(x) { x + x }                         // an untyped parameter is generic
fn span<T>(a: T, b: T) -> T { if a > b { a - b } else { b - a } }   // <T> says a and b agree
fn zero<T>() -> T { default<T>() }           // explicit type arg when nothing infers it
let half = zero<f64>() + 0.5;
```

A type parameter is always a type, never a size: `fn f<N>(a: i64[N])` is an error. Take a slice `T[:]` instead, which every array kind converts to.

Errors appear at the instantiation, with the call chain that produced it, much like a C++ template error but with the chain spelled out.

### `impl Hash for MyKey`, `std::hash<MyKey>`

Write a `hash` overload. `dictionary` finds it by overload resolution.

```
struct key { a: i64, b: i64 }
fn hash(k: key) -> u64 { hash_combine(hash(k.a), hash(k.b)) }

var seen = dictionary<key, bool> {};    // a set is dictionary<K, bool>
```

Equality is structural and built in (`==` compares structs memberwise), so there is no `PartialEq` to derive.

### `impl Display`, `operator<<`

Write a `format` overload. `print`, `str` and `format` then render your type that way everywhere, including inside arrays and other structs.

```
struct Person { name: u8[..16], age: i32 }
fn format(out: u8[>..]&, p: Person) { format(out, p.name, " (", p.age, ")"); }

print(people);    // [ada (36), grace (45)]
```

Without an overload, everything still prints structurally (`Point { 1, 2 }`), which is often enough for debugging.

### Ordering (`Ord`, `operator<`, comparators)

Ordering comparisons (`<`, `>`, `<=`, `>=`) exist on numbers only. Sorting takes a block that says "a goes before b":

```
sort(people) { a, b => a.age < b.age };
let oldest = max_by(people) { it.age };   // index of the largest
```

### Operator overloading on math types

There is no operator overloading. Instead, `+ - * / %` and unary `-` apply memberwise to any struct or fixed array whose leaves are all integers or all floats, and `*` and `/` broadcast a scalar. That is how the library's `vec3<T>` works.

```
struct V3 { x: f32, y: f32, z: f32 }
var c = V3 { 1, 2, 3 } + V3 { 0.5, 0.5, 0.5 };
c *= 2;
```

Anything beyond memberwise arithmetic (a matrix multiply, a dot product) is a named function: `mul(a, b)`, `dot(a, b)`.

### Methods and `impl` blocks

There are none, but `x.f(a)` is exactly `f(x, a)` for every function, so method-call syntax works everywhere: `xs.sort()`, `d.insert(k, v)`, `2.5.describe()`. Group a type's functions in the same file or namespace as the type. There is also no namespace privacy in v1, so encapsulation is by convention.

### Constructors

Struct literals do the job. Fields can declare defaults (evaluated per construction, may call functions), and ending a literal with `..` fills every unspecified field with its type's default. For invariants, write a plain `make_thing(...)` function that returns the struct; it is built in place at the caller.

## Callbacks and closures: passed down, never stored

Function values in Goose are compile-time entities. You can pass a function or a block to another function, and every call through it is direct and inlinable. You cannot store one in a variable, a field or an array, and you cannot return one. There is no `std::function`, no `Box<dyn Fn>`, no function pointer.

### Higher-order functions and lambdas

The trailing-block syntax replaces lambdas. `it` is the implicit parameter, or name them with `a, b =>`. The block sees and can write the enclosing locals.

```
// Rust: let evens: Vec<_> = xs.iter().filter(|x| *x % 2 == 0).collect();
let evens   = xs.filter() { it % 2 == 0 };
let squares = xs.map()    { it * it };
let total   = fold(xs, 0) { acc, x => acc + x };

var gaps: i64[>..] = [];
each_pair(xs) { a, b => gaps.push(b - a); };   // writes an enclosing local
```

`filter` and `map` build their result straight into the receiving variable; there is no iterator chain and no intermediate collection.

### Writing your own HOF

Take the function as a type parameter and call it:

```
fn each_pair<T, F>(xs: T[:]) {
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}
```

A `return` inside a block returns from the **enclosing function you wrote it in**, not from the HOF. So a custom iterator has the same control power as a built-in `for`:

```
fn each_step<F>(lo: i64, hi: i64, step: i64) {
    var i = lo;
    while i < hi { F(i); i += step; }
}

fn first_multiple_of_7(lo: i64, hi: i64) -> i64 {
    each_step(lo, hi, 3) { if it % 7 == 0 { return it; } };   // leaves first_multiple_of_7
    return -1;
}
```

This replaces Rust's `try_for_each`/`ControlFlow` gymnastics and C++'s "return a bool from the lambda to stop".

### Nested functions instead of closure objects

A nested `fn` reads and writes the enclosing function's locals as free variables. Recursive-descent parsers in the samples are written this way: the input, cursor and output pools are locals of `parse`, and the grammar rules are nested functions.

```
var budget = 0;
fn spend(e: Employee&) { budget += e.salary; }
for &e in staff { if e.dept == "eng" { spend(e); } }
```

### Stored callbacks: event handlers, command tables, strategy objects

This is the pattern that changes most. When C++ keeps a `vector<std::function<void()>>` of handlers or a `map<string, Command*>`, the set of behaviours is almost always known at compile time. Store *which* behaviour as data, and dispatch with an enum.

```
// C++: std::vector<std::function<void(World&)>> on_tick;
enum Action { Spawn { kind: i32 }, Heal { amount: i32 }, Log }

fn run(a: Action.Spawn, w: World&) { /* ... */ }
fn run(a: Action.Heal,  w: World&) { /* ... */ }
fn run(a: Action.Log,   w: World&) { /* ... */ }

var on_tick: Action[>..] = [];
on_tick.push(Action.Heal { amount: 5 });
for a in on_tick { run(a, world); }   // dispatch on the tag; other args pass through
```

The payload carries what a closure would have captured. Because it holds data rather than code, the list can also be saved to disk or sent to a worker thread.

A command table keyed by name becomes an `if`/`else` chain that turns the name into an `Action` (`match` takes enums and integers, not strings; strings compare with `==`), or a `dictionary<const u8[:], i32>` from name to an id that you then `match` on.

For GUI callbacks, the `ui` library is immediate-mode: `if ui::button_label(ctx, "Reset") { volume = 0.5; }`. The "callback" is the `if` body, run on the frame the button is clicked.

## Linked structures: nodes live in an array, links are references

Where C++ uses `unique_ptr<Node>` children and safe Rust gives up and uses `Vec<Node>` plus `u32` indices, Goose puts nodes in one grow-only array and links them with **typed references**. Because pushing never moves existing nodes, a reference you got from `push` stays valid forever, and the compiler checks it.

### Binary search tree

```
// Rust, the usual arena workaround
struct Node { key: i32, left: Option<u32>, right: Option<u32> }
let mut pool: Vec<Node> = Vec::new();
```

```
// Goose
struct Node { key: i32, count: i32 = 1, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];                     // .= binds a reference; = would copy
    loop {
        if key == cur.key { cur.count += 1; return; }
        let next = if key < cur.key { cur.left } else { cur.right };
        if next { cur .= next; continue; }  // `if` narrows the optional
        if key < cur.key { cur.left .= pool.push(Node { key: key }); }
        else             { cur.right .= pool.push(Node { key: key }); }
        return;                             // the push did not invalidate cur
    }
}
```

Three reference features do the work:

- **References are transparent.** `cur.key` reads through `cur`; there is no `*` or `->`. To mean the reference itself, use `.=` to bind or rebind and `.==` to compare addresses.
- **`T?` is a nullable reference** with no space cost. You must narrow it with `if`, `while`, `guard` or `assert` before use; inside the narrowed region it simply is a `T&`.
- **`Node&<u32>` is a relative reference**: a 4-byte offset instead of an 8-byte pointer. Loading it gives an ordinary `Node&`. This node is 16 bytes, versus 24 plus an allocation header for a `Box`/`unique_ptr` tree.

Relative widths are `u8`, `u16`, `u32`, `u64` or `varint`. Use plain `T&` (8 bytes) when you don't care about size.

### Self-relative vs pool-relative links

| Form | Measured from | Use when |
| --- | --- | --- |
| `T&<u32>` | The field itself; target must be in the same array | The pool is a local or parameter, or you want to save/move the whole structure as bytes |
| `T&<u32 in pool>` | The base of the named global array `pool`, or of a global root struct's field such as `pool.items` | Other arrays need links into the pool, or you relink a lot |

A value holding self-relative links cannot be copied (the offsets would point elsewhere). Build it in place with `push` and bind it with `.=`.

### Doubly linked list with a sentinel

`self` initializes a non-optional link to the value being constructed, so a circular list can have a sentinel and no null links at all. The nodes live in a `slot_pool` (see [Non-nested lifetimes](#non-nested-lifetimes-shared_ptr-and-rc-become-pools)), which constructs the sentinel in its slot with `alloc_index_with`:

```
import slot_pool;

struct Node { val: i64, prev: Node&<u32 in pool.items>, next: Node&<u32 in pool.items> }

var pool = slot_pool<Node> {};
let head = pool.alloc_index_with() { Node { val: 0, prev: self, next: self } };

fn insert_after(p: Node&, v: i64) -> Node& {
    let n .= pool.alloc_ref(Node { val: v, prev: p, next: p.next });
    n.next.prev .= n;
    p.next .= n;
    return n;
}

fn remove(n: Node&) {
    n.prev.next .= n.next;
    n.next.prev .= n.prev;
    pool.free_ref(n);
}
```

### Recursion over a tree

Recursion is opt-in: mark the cycle's entry `recursive fn` and write out every signature in the cycle. Accumulators are passed in by reference (see "Structs that own several vectors" for why):

```
recursive fn in_order(n: Node&, out: i32[>..]&) {
    let l = n.left;
    if l { in_order(l, out); }
    out.push(n.key);
    let r = n.right;
    if r { in_order(r, out); }
}
```

### Expression trees and ASTs

Combine a variable-mode enum with narrow links. Each node is exactly as big as its variant:

```
enum Expr { Num { v: f64 }, Add { l: Expr..&<u16>, r: Expr..&<u16> } }

fn eval(e: Expr.Num&) -> f64 { e.v }
recursive fn eval(e: Expr.Add&) -> f64 { eval(e.l) + eval(e.r) }

var pool: Expr..[>..] = [];
let a = pool.push(Expr.Num { v: 1.5 });
let b = pool.push(Expr.Num { v: 2.0 });
let sum = pool.push(Expr.Add { l: a, r: b });   // 23 bytes for the whole tree
```

### Graphs and indexing a packed array

For an adjacency list, the `index types` feature turns `u32` node ids into a type the compiler knows is in range: `type NodeId = u32 in nodes;`. Converting with `as NodeId` checks once; every later `nodes[id]` is check-free.

An array of variable-size elements (a `Shape..[>..]`, a `u8[][>..]`) can't be indexed. Keep a second array of links beside it and index that:

```
var marks: Mark..[>..] = [];
var mark_at: (Mark..&<u32 in marks>)[>..] = [];
mark_at.push(marks.push(Mark.Circle { r: 1.5 }));
match mark_at[i] { /* ... */ }
```

The `in` form needs a global array; for a local one use `(Mark..&)[>..]` at 8 bytes per entry.

## Non-nested lifetimes: `shared_ptr` and `Rc` become pools

Some data really does not die in stack order: cache entries, nodes of a graph being edited, open documents closed in any order, entities in a game. C++ reaches for `new`/`delete` or `shared_ptr`; Rust for `Rc<RefCell<T>>` or a slab crate. Goose's standard library gives you a **`slot_pool`**: a grow-only array, `items`, paired with a freelist. It is a root struct (see [Structs that own several vectors](#structs-that-own-several-vectors-a-root-struct)), so each pool is a local or global variable.

```
import slot_pool;

struct Item { id: i32, weight: f32 }
var pool = slot_pool<Item> {};

let s0 = pool.alloc_index(Item { 10, 1.0 });   // a slot index
let r  .= pool.alloc_ref(Item { 11, 1.0 });    // or a reference
pool.free(s0);                                 // slot 0 goes on the freelist
let s2 = pool.alloc_index(Item { 12, 2.0 });   // reuses slot 0
pool.free_ref(r);                              // a reference back to its slot
```

Read and iterate the elements through `pool.items`: `pool.items[s2]`, `for item in pool.items`. The language's older `reusable` and `reusable[]` pools are deprecated in favour of `slot_pool` and `slice_pool`.

**`free` releases nothing.** It puts an index on a freelist. Every slot stays a live, correctly typed value, so a stale reference to a reused slot reads a *different* `Item`, never garbage. That is a logic bug of the same class as a stale index into a `Vec`, not a use-after-free. Memory and type safety hold.

The practical rules that follow:

- Treat a freed slot like a stale index: stop using references to it after `free`. If you need to detect staleness, keep a generation counter in the element and compare it, as you would with a Rust slotmap key.
- The pool never shrinks; its high-water mark stays committed until the owning scope ends (or the program does, for a global). `pool.items.len` counts every slot ever appended, free ones included, and iterating `items` visits those too. `trim_memory()` in `os` returns pages above stack tops, not freed slots.
- Elements must be fixed-size. For variable-size data per slot, use the slice pool below.
- Ref-counting has no direct equivalent. Decide who owns the object (usually one place) and free it there; other holders keep references and accept the stale-slot semantics, or check a generation.

### Independently growing buffers: `slice_pool`

When each object owns a growable buffer (a document's text, a node's child list), use a `slice_pool`. It hands out contiguous runs of its `items` and merges freed neighbours.

```
// C++: std::unordered_map<DocId, std::unique_ptr<Document>> open;
import slot_pool;
import slice_pool;

struct Document { id: u64, text: u8[:] }      // the document's current run of bytes

var docs = slot_pool<Document> {};
var bytes = slice_pool<u8> {};

fn open_doc(id: u64, size: i64) -> i64 {      // a slot in docs, like a DocId
    return docs.alloc_index(Document { id: id, text: bytes.alloc_slice(size) });
}
fn grow(slot: i64, extra: i64) {
    let d .= docs.items[slot];
    d.text = bytes.realloc_slice(d.text, d.text.len + extra);
}
fn close(slot: i64) {
    bytes.free_slice(docs.items[slot].text);
    docs.free(slot);
}
```

Handing out slot indices rather than references keeps the API simple: `free` takes an index, and `free_ref` only accepts a reference the compiler can trace to that exact pool, which a `Document&` parameter of a function using the global `docs` currently is not. `bytes.alloc_copy(text)` allocates a run and fills it from an existing slice in one step.

This is a first-fit allocator inside one array, with three consequences you would not see with `malloc`: `realloc_slice` may move a run, so keep cursors as offsets, not slices; all documents share one stack reservation (2 GB by default); and the array's high-water mark is never returned to the OS. The [design note](https://aardappel.github.io/goose/docs/design/owning_dynamic_data.html) covers alternatives, such as fixed `u8[..MAX]` slots or chunked ropes.

### When a pool is the wrong answer

Many "I need `shared_ptr`" cases are really "I need this to outlive the function that made it". Check first whether a caller's scope can own the data, with the callee pushing into an array passed down. That costs nothing and needs no freeing at all.

## Structs that own several vectors: a root struct

A C++ `class Compiler { vector<Node> nodes; vector<Symbol> syms; string out; }` or a Rust `struct World { entities: Vec<E>, events: Vec<Ev>, ... }` becomes a **`root struct`**. Each of its fields gets storage of its own, so all three can grow independently:

```
root struct Compiler {
    nodes: Node[>..] = [],
    syms: Symbol[>..] = [],
    out: u8[>..] = [],
}

fn emit(c: Compiler&, s: u8[:]) { c.out.append(s); }

fn compile(source: const u8[:]) -> i64 {
    var c = Compiler { .. };
    c.nodes.push(Node { kind: 1, first: -1, next: -1 });
    emit(c, source);
    return c.out.len;
}   // all three tables freed here at once; nothing global to reset
```

The tradeoff is placement. The owner must be a local or global variable: it cannot be a field, an array element, a copy or a return value. Functions take it as `Compiler&`, and that reference can be stored, captured or made optional like any other. `slot_pool` and `slice_pool` above are root structs from the standard library.

An ordinary `struct`, the kind you can put in arrays, holds **at most one** resizable array, and only as its **last** field (because a resizable value must be the top of its own data stack).

### What fits in an ordinary struct

- Any number of fixed-size fields, including inline bounded arrays (`u8[..32]`, `Item[..8]`) and fixed-mode enums.
- Variable-size fields (`u8[]`, `T[varint]`) built once at construction.
- One growable tail: `struct Stack { name: u8[..8], items: i64[>..<] }`.

If your "vectors" have a real small bound, `T[..k]` keeps them inline and the struct stays fixed-size, so it can go in arrays too.

### The context pattern

When the tables already exist as separate variables, or the working code also needs data it does not own, make each table a **local of a driver function** and hand the working code one ordinary struct of references. A struct of references and slices is fixed-size and cheap to pass.

```
struct Ctx {
    source: const u8[:],
    nodes: Node[>..]&,
    text: u8[>..]&,
    scratch: i64[>..<]&,
}

fn compile(source: const u8[:]) -> i64 {
    var nodes: Node[>..] = [];
    var text: u8[>..] = [];
    var scratch: i64[>..<] = [];
    var c = Ctx { source: source, nodes: nodes, text: text, scratch: scratch };
    return walk(c, 0, 1);
}   // all three tables freed here at once; nothing global to reset
```

The struct is for code spread across many functions or files. When the code is small enough to live inside the driver, nested functions see the tables as free variables with no struct at all.

Globals also work for program-lifetime state (`var log: u8[][>..];` is legal at top level), but the context pattern gives you a fresh state per call for free.

### Recursion can't keep growable locals across the recursive call

Every activation would need its own data stack, so the compiler forbids a function in a recursive cycle from holding growable data while calling back into the cycle. Three ways around it:

1. **Pass the accumulator in**: `recursive fn in_order(n: Node&, out: i32[>..]&)` pushes into the caller's array.
2. **Scope the scratch so it ends before the call**: build a line in a `{ }` block, print it, then recurse.
3. **Share one grow-shrink stack across the recursion**, truncating back to a mark before returning:

```
recursive fn walk(c: Ctx&, n: i64, depth: i64) -> i64 {
    let mark = c.scratch.len;
    c.scratch.push(depth);            // this activation's entries
    var total = c.scratch[mark];
    var e = c.nodes[n].first;
    while e >= 0 { total += walk(c, e, depth + 1); e = c.nodes[e].next; }
    c.scratch.resize(mark);           // gone before it returns
    return total;
}
```

A recursive-descent parser therefore keeps its state in a root struct or locals owned by the non-recursive entry function, passed down by reference, rather than in a parser object the recursion creates.

### Updating a variable-size field

Fields like `u8[]` or `T[varint]`, and variable-mode enum values, are **frozen** once built. If a record's string needs to change, either give it a fixed-capacity field (`u8[..32]`, assignable) or rebuild the container. Fixed-size fields of the same record can be assigned freely.

## Errors: three lightweight tools instead of `Result` or exceptions

There is no `Result` type, no `?` operator and no unwinding exceptions. Goose gives you three tools, chosen by how far the error travels.

| Situation | C++ / Rust | Goose |
| --- | --- | --- |
| Two outcomes, caller handles it right away | `optional<T>`, `Result<T, E>` | A trailing `bool`: `-> T, bool` |
| Found or not | `T*` / `Option<&T>` | `T?`, a nullable reference into the input |
| Fails deep inside, handled far up | exceptions, `?` through every frame | `return E from f`, or the `exception` module |
| Bug or invariant | `assert`, `panic!` | `assert(c)`, `abort(msg)`, `exit(code)` |

### Trailing `bool`

```
fn divide(a: i64, b: i64) -> i64, bool {
    if b == 0 { return 0, false; }
    return a / b, true;
}
let q, ok = divide(7, 2);
guard ok else { return; }
```

Multiple results are a calling convention, not a tuple. Library functions follow it: `let n, ok = parse_int(s);`.

### Optional references

```
let big = xs.find() { it > 100 };   // T? into xs, null if none
if big { print(big); }               // narrowed: big is a T& inside
guard big else { return 0; }         // or: narrowed for the rest of the block
```

### `return … from`: the long-distance return

`return E from f` returns `E` as the result of the innermost active call to `f`, skipping every frame in between. The functions in the middle say nothing about it: no `Result` in their signatures, no `?` on their calls.

```
fn load(text: u8[:]) -> User[>..], u8[] {          // users, error message
    var users: User[>..] = [];
    var lineno = 0;
    each_split(text, '\n') {
        lineno++;
        if it.len > 0 { users.push(parse_record(it, lineno)); }
    };
    return users, "";
}

fn parse_record(line: u8[:], lineno: i64) -> User {   // plain User, no Result
    let comma = find(line, ",");
    guard comma >= 0 else {
        return [], str("line ", lineno, ": expected name,age") from load;
    }
    let age, ok = parse_age(line[comma + 1..]);
    guard ok else { return [], str("line ", lineno, ": bad age") from load; }
    return User { name: trim(line[..comma]), age: age };
}
```

It is checked statically: every call path to the `return … from load` must be inside a call to `load`, so there is no "uncaught" case at runtime. The cost is a flag check per frame on the way out, no unwind tables. That makes it cheap enough to use for every syntax error in a parser, not just catastrophes.

### Typed `try`/`throw`

When you don't want to name the target function at each throw site, the `exception` module wraps the same mechanism:

```
import exception;

fn load() { exception::throw(str("cannot load item ", 42)); }

exception::try<u8[]>(load) { error => print(error); };
```

The error type is fixed at the `try` (`u8[]` for owned text, an int, or your own struct/enum). A thrown error may only carry references to globals or static data, so copy text into an owned `u8[]`. There is no `finally`, and `abort`/`exit` are not catchable.

### Don't port `errno`-style or sentinel-heavy code

Indices use `-1` for "none" by library convention (`find`, `position`), which is fine. For anything richer, prefer the trailing `bool` or `T?` over magic values, because `T?` forces the caller to narrow before use.

## RAII and destructors: memory frees itself, everything else is explicit

Goose has no destructors and no `Drop`. For memory you don't need them: scope exit resets the data stack, and since nothing inside Goose memory points to an external allocation, there is nothing to walk. Teardown of a million-element structure is one store.

What RAII also does in C++ and Rust is release *non-memory* resources: files, GPU buffers, sockets, locks. In Goose those are handles you release explicitly.

| Resource | C++ / Rust | Goose |
| --- | --- | --- |
| File contents | `ifstream` closed by its destructor | `read_file(path, out)` / `write_file(path, data)`: whole-file calls, no handle to close |
| GPU buffer, texture, pipeline | Wrapper class with destructor | `gfx::Buffer { id }` handle; call `gfx::release(b)` |
| Physics world, body | Owning object | `physics::destroy(w)` |
| Mutex guard | `lock_guard`, `MutexGuard` | Not needed: threads share no memory |
| Scoped timer, "undo on exit" | Guard object | Do the work at the end of the block yourself |

Library handles carry a generation, so using one after release is reported as a misuse rather than a crash. A pattern that keeps release calls from drifting: create the handle in the function that owns its lifetime and release it at the end of that same function, as you would with a C API.

The standard library has **no `defer` or `finally`**. A cleanup callback can't be guaranteed to run on every exit, because a block's `return` or a `return … from` skips it. A design note proposes scope-exit cleanup; it is not implemented. Until then, put cleanup on each exit path, or structure the function so there is one exit.

## Threads: no shared memory, only typed queues

There is no shared mutable memory in Goose, so there are no `Mutex`, `Arc`, atomics or memory orderings either. A worker is a `thread_fn`; the compiler compiles it and everything it calls as a separate program with its own data stacks and **its own copy of every global it uses**. Values cross between threads through queues, one per type.

```
// Rust: let results = Arc<Mutex<Vec<R>>>; spawn workers that lock and push.
struct Job    { n: i32 }
struct Result { n: i32, digits: i32 }

thread_fn worker() {
    loop {
        let job = qget<Job>();             // blocks
        guard job.n >= 0 else { break; }   // -1 means stop
        qput(Result { n: job.n, digits: count_digits(job.n) });
    }
}

fn main() {
    let n = max(1, hardware_threads());
    for i in 8 { qput(Job { n: (1 << (i * 3)) as i32 }); }
    for i in n { qput(Job { n: -1 }); }    // one stop job per worker
    var ids: i64[>..] = [];
    for i in n { ids.push(thread_spawn(worker)); }
    var total = 0;
    for i in 8 { let r = qget<Result>(); total += r.digits; }
    for id in ids { thread_wait(id); }
}
```

What changes in practice:

- **Only flat values cross**: no references or slices at any depth. Because a flat value can include variable-size parts, a job can carry real data, `struct Row { y: i32, cells: u8[] }`, and the crossing is a single memcpy.
- **Results come back as messages**, not writes into a shared vector. Collect them in `main` and reassemble by an id field.
- **A global incremented in a worker changes the worker's copy only.** Shared counters become messages.
- **Parallel-for over a big array** means splitting it into chunks, sending each chunk (or its index range plus the data it needs) as a job, and merging results.
- **Libraries with a main-thread rule** (`gfx`, `ui`, `audio`, `physics`) are rejected at compile time if reachable from a `thread_fn`.
- Threads need a real C compiler build (`goose build` or `-o`); the in-process TinyCC runner can't place thread-local storage.

## Serialization: your data is already a file format

In C++ and Rust, saving a pointer-linked structure needs a serializer (serde, protobuf, hand-written) because addresses mean nothing outside the process. In Goose, data that uses self-relative links (`T&<u32>`) and inline nested values is position-independent, so saving it is writing its bytes.

```
struct Node { word: u8[..24], count: i32, left: Node&<u32>?, right: Node&<u32>? }
var index: Node[>..] = [];
// ... build the tree ...

let image: u8[>..] = index.to_bytes();              // framed image
write_file("index.bin", image);

var loaded, ok = from_bytes<Node[>..]>(image);      // verified load
```

`from_bytes` verifies the framing, every tag, every length and every link before handing back an ordinary array, so a corrupt or hostile file returns `false`, never a wild reference. It checks safety, not integrity: add a checksum if you need to know the file is the one you wrote.

`bytes_of(a)` is a read-only view of the element bytes with no copy, ready for `write_file` or a GPU upload (`gfx::vertex_buffer(bytes_of(vertices))`).

Design implication: if a structure will be saved, prefer self-relative links over `T&` and `in pool` links when you lay it out. For external formats, use `binary` (checked little-endian reads), `json` (writer, tree parser, or a streaming reader into your own types), `csv` and `base64`.

## Smaller habits that change

These trip people up in the first afternoon.

| Habit | In Goose |
| --- | --- |
| `cond ? a : b` | `if` is an expression: `let k = if hp > 50 { "ok" } else { "hurt" };` |
| Declare a variable, assign it in each branch | Use `if`/`match`/`block` as an expression, or `let n: i64;` assigned exactly once (checked) |
| Early-exit chains of `if (!x) return` | `guard x else { return; }`; inside `block { … }`, `break value` exits with a value |
| `let` / `const` meaning in Rust / C++ | `let` = can't reassign the name; contents still writable. `const T` = read-only contents. `var` = reassignable |
| `*p`, `p->x`, `&*p` | References are transparent: `p.x`. `.=` binds or rebinds, `.==` compares addresses |
| `std::tuple`, returning `(a, b)` | `-> A, B` and `let a, b = f();`. There is no tuple type, and an `if` branch can't yield `a, b` |
| `x++` in an expression | `++` and assignment are statements only |
| `if (n)` on an integer | Error: write `if n != 0`. `if` on an optional narrows it |
| `-7 % 3 == -1` | `%` is Euclidean: `-7 % 3 == 2`, always in `[0, abs(b))`. `rem` from `std` gives the C behaviour |
| Implicit promotion to `int` | Arithmetic runs at the operands' width; `u8 + u8` is 8-bit. `u32 + i32` is an error. Narrowing needs `as` (checked in debug) or `as!` (unchecked) |
| Signed overflow is UB / panics in debug | Aborts in debug, wraps in release. Unsigned always wraps |
| `for (auto& x : v) x *= 2;` | `for &x in xs { x *= 2; }`. Plain `for x in xs` gives a read-only copy for fixed-size elements |
| `printf("%d %s", …)` | `print("n=", n, " s=", s)`; `format_int` / `format_flt` for padding and precision |
| Labelled `break 'outer` | No labels in v1. Use a `block`, a nested function with `return`, or a flag |
| Recursion "just works" | Mark the cycle `recursive fn`, write full signatures, keep growable locals out of the recursive path |
| Separate compilation, `.so` plugins | Whole-program compilation to C. Goose libraries come as source packages (`goose build`) |

### Bounds checks and speed

Every index is checked and the compiler removes the checks it can prove. Help it the way you'd help a vectorizer: one `assert` of the lengths at the top of a kernel lets it drop every check in the loop. `goose --bce-lines` shows which checks survived per line, and `checkfree fn` makes a surviving check a compile error. For `dist[adj[k]]`-style indirect indexing, index types (`u32 in nodes`) move the check to where the id is created.

## What doesn't translate, and the nearest workaround

Goose's speed and safety come from a closed world and static ownership. A few designs depend on exactly what it gives up.

| You want | Why it's not there | Nearest Goose shape |
| --- | --- | --- |
| Runtime plugins, an open class hierarchy extended by other modules | No function pointers, no vtables, whole-program compilation | An enum of known kinds; for truly external code, C via `extern fn` / `extern "lib" { … }` |
| Closures stored in a struct or returned from a function | Function values must stay compile-time | Store an enum tag plus captured data; dispatch with case functions |
| `Vec<Vec<T>>` where each inner vector grows independently | An array element can't be resizable | Runs from a `slice_pool<T>`, a flat array plus offsets, or `T[..k]` rows if bounded |
| Random access into an array of variable-size records | Offsets depend on every earlier element | Fixed-size elements with inline capacity, or an index array of references beside it |
| Taking a reference into a fixed-mode enum payload | It could be overwritten with another variant | Store it in variable mode (`T..`), or copy, modify and reassign |
| Changing the variant of a packed (`T..`) element | Its neighbours are packed right behind it | Fixed mode, or rebuild the container |
| `shared_ptr` with automatic last-owner cleanup | No refcounts, no destructors | Pick an owner; `slot_pool` + explicit `free`; generation field to detect staleness |
| Shared mutable state between threads | No shared memory by design | Messages through typed queues |
| An array of objects that each own several growable members | A root struct must be a local or global variable | One root struct holding a field per member, with slot indices or a `slice_pool` for the per-object parts |
| Subprocesses, networking | Not in the v1 library | `extern fn` to C |

As a rule of thumb, when something won't compile, read the whole error. It names both ends, such as the reference and where it was bound, or the holder and the line that stored into it, which usually tells you which scope or binding to move.

## Sources

- [Goose documentation index](https://aardappel.github.io/goose/docs/)
- [Goose, by example (tutorial)](https://aardappel.github.io/goose/docs/tutorial.html)
- [The Goose Language Reference](https://aardappel.github.io/goose/docs/reference.html)
- [The Goose standard library](https://aardappel.github.io/goose/docs/stdlib.html)
- [Design note: independently owned dynamic data](https://aardappel.github.io/goose/docs/design/owning_dynamic_data.html)
