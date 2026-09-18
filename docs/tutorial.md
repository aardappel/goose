# Goose, by example

This is the friendly introduction. It assumes you have written C, C++, Rust,
Go or something in that family, and it tries to show you what is *different*
about Goose rather than re-teach you what an `if` is. The precise rules live
in [the spec](goose_spec.md); the working programs live in
[`samples/`](../samples/README.md). This is the thing you read first.

A warning up front. Most of Goose looks ordinary — braces, `let`, `fn`,
`struct`, `match`. If you skim it you will conclude it is one more C-family
language with a slightly odd syntax for arrays, and you will have missed the
point entirely, because the odd syntax for arrays *is* the point. So the
first real section is about where values live. Everything else follows from
it.

---

## 1. The one idea

Here is an array of strings.

```goose
var words: u8[][>..] = [];
for i in 5 { words.push(str("word", i)); }
```

In C++ that is a `vector<string>`: one heap allocation for the vector, one
per string, a pointer and a capacity per element, and a walk over all of them
when it goes out of scope. In Rust it is a `Vec<String>` with the same shape.

In Goose it is **one contiguous block of bytes** — a length, then five
back-to-back `(length, characters)` runs — that came into existence by
bumping a pointer, and that disappears when the enclosing scope ends by
moving that pointer back. There is no allocator involved. There is nothing to
free. There is no pointer anywhere in it.

```
words:  05 00 00 00 | 05 w o r d 0 | 05 w o r d 1 | 05 w o r d 2 | ...
```

That is the whole idea, and the rest of the language is what it takes to make
it work for real programs:

* Memory is a handful of big **data stacks**. Growth is a pointer bump.
* Every dynamic value is **owned by a variable**. The only deallocation is a
  scope ending, which resets a pointer.
* Values nest **inline**. A string inside a struct inside an array is bytes
  inside bytes inside bytes.
* You may still take **references into a growing container**, and the
  compiler proves they never dangle — no annotations, no `'a`, no `Rc`.

It is a trade, and it costs you things — §18 is the honest list. What it
buys is measurable: over sixteen benchmarks Goose runs at about **3.3x the
speed of idiomatic C++**, roughly level with hand-tuned C++ and with the best
safe Rust, on **1.9x less memory** than the C++ and 1.2x less than the Rust
([`bench/summary.md`](../bench/summary.md)). Most of the wins are structural
rather than tuned: they are things the other languages cannot spell.

---

## 2. Running something

Goose compiles the whole program to C, which you then build with whatever
compiler is around:

```bash
goose -o hello.c hello.goose && cc hello.c -o hello -lm -pthread && ./hello
```

A compiler built with the bundled TinyCC backend skips both steps — with no
`-o` it compiles the program into its own process and runs it there, which is
what you want while you are reading this:

```bash
goose hello.goose
```

And the obligatory program:

```goose
fn main() {
    print("hello, goose");
}
```

`print` takes any number of arguments of any type, renders each as text and
writes the lot as a single line (a single `write`, so two threads never
interleave). There is no format string and no `<<`.

```goose
print("answer=", 42, " ratio=", 0.75, " yes=", 3 > 2);
```

Everything renders, not just scalars: arrays print as `[1, 2, 3]`, structs as
`Point { 1, 2 }`, a `u8` array as its bytes. This is why the samples print so
much — it is genuinely the easiest way to see what a value is.

---

## 3. The half hour of stuff you already know

Skim this. `let` binds a constant, `var` a variable, types come after the
name and are usually inferred:

```goose
let answer = 42;
var count: i32 = 0;
let ratio = 0.75;
count += 1;
count++;              // a statement, not an expression
```

Functions are free functions. There are no methods and no `impl` blocks, but
`x.f(a)` is *exactly* `f(x, a)`, so you can write the method-ish form
wherever it reads better:

```goose
fn total(xs: i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    return t;
}
```

`if`, `match`, `block` and `{ }` are **expressions** — a block's value is its
trailing expression — so there is no ternary and no "declare it then assign
in every branch":

```goose
let kind  = if hp > 50 { "healthy" } else { "hurt" };
let level = match hp { 0 => "dead", 1..50 => "low", _ => "fine" };
```

`guard` states what the code after it depends on; its `else` must leave
(`return`, `break`, `continue`, `abort`, `exit`). `block` gives `break` a
target without a loop, so a chain of checks can bail out with a value:

```goose
let verdict = block {
    guard p.alive else { break "gone"; }
    guard p.hp > 0 else { break "dead"; }
    "standing"
};
```

Loops are `for x in range_or_array`, `while`, and `loop`:

```goose
for i in 3 { }              // 0, 1, 2
for i in 10..13 { }         // 10, 11, 12
for x in xs { }             // copies, for fixed-size elements
for &x in xs { x *= 2; }    // references: the mutation form
for x, i in xs { }          // with the index
```

Several return values are a calling convention, not a tuple type — there is
no tuple type — and by custom a trailing `bool` says whether the rest means
anything:

```goose
fn divide(a: i64, b: i64) -> i64, bool {
    if b == 0 { return 0, false; }
    return a / b, true;
}

let q, ok = divide(7, 2);
```

Three small differences that will bite you if you skip them:

**Arithmetic happens at the operands' own width.** `u8 + u8` is an 8-bit add.
A narrower type widens into a wider one implicitly; nothing narrows without
`as` (range-checked in debug) or `as!` (never checked). `u32 + i32` is a
compile error asking which one you meant — the language will not silently
promote both to 64 bits behind your back, because avoiding exactly that is
the reason the sized types exist.

**`%` is Euclidean.** The result is in `[0, |b|)` and never negative, at
every integer type. So `x % n` is a valid index into a length-`n` array for
*any* `x`, and the compiler can drop the bounds check instead of demanding a
guard you know is redundant.

```goose
print(-7 % 3, " ", 7 % -3);       // 2 1
```

**Unsigned arithmetic wraps, in every build**, by definition — that is what
hashes and PRNGs mean by unsigned math. Signed overflow aborts in debug and
wraps in release.

---

## 4. Where values live

Now the part that matters.

A Goose program has the native call stack, some static data, and **N data
stacks**, where N is a small number the compiler works out for your program.
A data stack is a multi-gigabyte address-space reservation with a bump
pointer. That is the entire memory system. There is no `malloc`, no free
list, no GC, no reference counting, no destructors.

So every dynamic value is owned by a variable, and the rules that make this
work are:

1. Values are pushed in lifetime order and popped **en masse** when the
   owning scope exits.
2. At most one *resizable* value is live per stack, and it is the topmost
   thing on that stack for its entire lifetime. So growth is a pointer bump
   with no capacity check and **no move, ever**.

The compiler proves all of this statically. There is no runtime bookkeeping
beyond the bump pointers.

"One resizable per stack" sounds alarming until you notice the plural in
"N data stacks": three growable arrays alive at once simply get three stacks,
and stacks are recycled the moment a scope frees one. N is whatever the
deepest point of your program needs, computed once at compile time, and a
stack costs nothing but reserved address space. You never write any of this
down — there is no syntax for a stack anywhere in the language.

### Scope exit is the free

```goose
for round in 3 {
    var scratch: u8[>..] = [];
    for i in 100000 { scratch.push((i % 256) as! u8); }
    print("round ", round, ": ", scratch.len, " bytes of scratch");
}
```

Each iteration gets a fresh 100 KB buffer, and at the closing brace the
stack's top goes back to where it was. No destructor runs. No list is walked.
The cost of "freeing" that buffer is one store.

### Nothing moves, so references survive growth

This is the big one:

```goose
struct Item { id: i32, weight: f32 }

var items: Item[>..] = [];
let first .= items.push(Item { 1, 0.5 });          // a reference to element 0
for i in 2..1000001 { items.push(Item { i as i32, 0.0 }); }
first.weight = 99.5;                               // still valid, a million pushes later
print(items.len, " items, first is ", items[0]);
```

```
1000000 items, first is Item { 1, 99.5 }
```

A `vector<T>` cannot promise that and a `Vec<T>` cannot either: both
reallocate, so both invalidate. This single property is why so much Goose
code holds typed references where the equivalent Rust holds `u32` indices
into an arena — and it is where a good share of the benchmark wins come from.

`push` returns a reference to the element it just made, which is the
idiomatic way to link up data you are building.

### Copies are visible

Fixed-size values (scalars, structs, fixed arrays, references, slices) copy
silently on assignment, like C. **Non-fixed values never copy implicitly**,
because that copy is O(size) and you should see it:

```goose
var a: i64[>..] = [1, 2, 3];
var b = copy(a);      // an explicit, visible O(n) copy
b.push(4);
```

Without the `copy`, `var b = a;` binds `b` to `a` by reference. Same at a
call: `f(xs)` hands a growable array to `fn f(xs: i64[:])`, to
`fn f(xs: i64[>..]&)` and to an untyped `fn f(xs)` alike, all by reference,
all free. A function that wants its own copy says so.

---

## 5. The array family

There is no one "array type". There are several, and they differ in exactly
one respect: what happens to the size.

| Spelling | What it is | Metadata |
|---|---|---|
| `T[k]` | fixed size, known at compile time | none |
| `T[]` | sized at construction, frozen after | a length |
| `T[varint]` | the same, with a variable-length length | 1–2 bytes usually |
| `T[..k]` | capacity `k` inline, grows and shrinks within it | a small length |
| `T[..]` | capacity chosen at construction | length + capacity |
| `T[>..]` | **grow-only**: the workhorse | a length |
| `T[>..<]` | **grow-shrink**: stacks, queues, heaps | a length |

In every one of them the representation is `[metadata][elements...]`, inline
and packed. There is never a pointer to an element block.

```goose
var fixed: i32[4] = [1, 2, 3, 4];
let zeros: f64[8] = [0.0; 8];      // fill form
let frozen: i64[] = [7, 8, 9];     // sized once, never changes

var grow: i64[>..] = [];           // arenas, builders, pools, tree storage
for i in 5 { grow.push(i * i); }

var stack: i64[>..<] = [];         // the one that pops
stack.push(1); stack.push(2);
let top = stack.pop();

var small: u8[..8] = [];           // lives inline anywhere a fixed value can
small.push('h'); small.push('i');
```

Which to reach for:

* **`[>..]` is the default.** Arenas, pools, string builders, tree storage,
  scratch that gets refilled between phases. "Grow-only" names the
  *guarantee* — references into it stay valid — not the operation set; it can
  still `pop` and `clear` at points where the compiler can see nothing is
  pointing into it.
* **`[>..<]`** when you genuinely need a stack or a heap. The price is that
  references into it may live in variables only, never in storage, since
  popped memory gets reused at other types.
* **`[..k]` / `[..]`** when the thing must sit inline inside something else:
  a 16-byte name inside a struct, a small list inside an array element.
* **`[]` / `[varint]`** for finished data: a string in a record, a node in a
  pool, anything built once and then only read.

Only `[>..]` and `[>..<]` are "resizable", and it is those that the
one-per-stack rule applies to. Everything else can go anywhere.

---

## 6. Strings are just byte arrays

There is no string type. A string is any array-family type with `u8`
elements, and which one you pick says what you want:

* `u8[>..]` — a builder. Appending is a pointer bump.
* `u8[]` / `u8[varint]` — a finished string, stored inline in whatever holds
  it.
* `u8[..16]` — a small string that lives inside a struct with no indirection.
* `u8[:]` — a *view* of somebody else's bytes. A string literal is one of
  these, specifically a `const u8[:]`, since literals live in read-only
  static data.

```goose
var s: u8[>..] = [];
s.append("hello");
s.push(',');
format(s, " world! ", 6 * 7);       // format appends the text of anything
print(s, " [", s.len, " bytes]");
```

```
hello, world! 42 [16 bytes]
```

`str(...)` is `format` into a fresh string, and — this matters — it is built
*directly at its destination*:

```goose
words.push(str("word", i));         // written straight into the new element
```

There is no temporary string that is then copied into the array. The
copy-free construction guarantee (spec §4.3) says a constructed value is
always built in its final home, propagated top-down through calls, and the
compiler is required not to need a fallback.

Slicing text costs nothing, because a slice is a pointer and a count into
bytes that already exist:

```goose
let line = "  key = some value  ";
let body = trim(line);
let eq   = find(body, "=");
print("'", trim(body[..eq]), "' -> '", trim(body[eq + 1..]), "'");
```

```
'key' -> 'some value'
```

`split` gives you an **array of slices into the input** — a few bytes per
part, with no text copied, where most languages hand back a list of freshly
allocated strings:

```goose
let parts = split("alice,30,paris", ',');
print(parts.len, " parts: ", parts);      // 3 parts: ["alice", "30", "paris"]
```

And a struct can hold its strings inline, so an array of them is one block
you can sort and copy as plain data:

```goose
struct Person { name: u8[..16], age: i32 }

// A `format` overload makes print/str/format render a Person your way.
fn format(out: u8[>..]&, p: Person) { format(out, p.name, " (", p.age, ")"); }

var people: Person[>..] = [];
people.push(Person { name: "ada", age: 36 });
people.push(Person { name: "grace", age: 45 });
var p = Person { name: "", age: 20 };
format(p.name, "linus");        // a limited array is a builder too, up to capacity
people.push(p);
print(people);
```

```
[ada (36), grace (45), linus (20)]
```

---

## 7. Slices that cannot dangle

`T[:]` is a reference plus a count: C++'s `span`/`string_view`, or Rust's
`&[T]`, and it is the universal "process a range" parameter — every array
kind coerces to it at a call site, for free.

```goose
fn total(xs: i64[:]) -> i64 { var t = 0; for x in xs { t += x; } return t; }

total(fixed);         // a T[k]
total(grow);          // a T[>..]
total(grow[1..4]);    // a sub-range
total(grow[^2..]);    // ^k counts from the end
```

The difference from `string_view` is that this one is checked. Every
reference and slice carries a static **root** — the variable that bounds the
lifetime of what it points at — and the entire lifetime system is one rule:

> A reference must not outlive the variable that owns its target, and must
> never observe its target at a wrong type.

There are **no lifetime annotations anywhere in the language**. Roots are
inferred, and functions are specialized per root, which gets you
Rust-lifetime precision through monomorphization with zero syntax. There are
also no aliasing or exclusivity rules — two references to the same thing are
fine, because without shared-memory concurrency aliasing alone cannot break
type safety here. If you have bounced off Rust's borrow checker, the thing to
know is that Goose's version only ever objects to *outliving*, never to *two
of them at once*.

When it does object, it tells you where both ends are, so you can move
either:

```goose
var line: u8[>..] = [];
// ...
let w = line[..5];
print(w);
line.clear();        // fine: w's last use is behind the clear
```

A scratch buffer that is refilled per iteration hands out slices of itself
freely. "Reusable scratch" and "structure I can point into" are the same
type.

Because a slice never copies, it is also the natural *key*. The library's
dictionary keyed by `u8[:]` stores no strings at all — just addresses and
lengths into text you already have:

```goose
let text = "the quick brown fox jumps over the lazy dog the fox";
var counts = dictionary<const u8[:], i32> {};
each_split(text, ' ') { counts.update(it, 0) { it += 1; }; };
print(counts.count, " distinct words");
```

That is the whole of word-frequency counting, with nothing copied: the file
is read once into one buffer, and every key, every table row and every line
of the final report points into it
([`15_word_freq`](../samples/15_word_freq.goose) does exactly this over a
book).

The `const` there is the other half of the story. Writability is inferred
rather than annotated: a string literal is read-only, `&x` of a `var` is
writable, and a *slot* — a field, an element, a global — has to say which
kind it holds. `dictionary<const u8[:], i32>` says "these keys may be
read-only views", which is what lets literals and views of a `let` be keys.
Parameters are generic over constness, so you almost never write `const`
except on a slot that must accept read-only data, and on a parameter you
want to document as read-only.

---

## 8. Flat all the way down

A struct may contain variable-size parts, and they sit **inline**, in
declaration order. Combine that with `varint` fields — a LEB128 integer that
takes as many bytes as its value needs — and a record becomes a run of bytes
with nothing indirect in it at all:

```goose
struct Item  { sku: u8[varint], qty: varint, cents: varint }
struct Order { id: varint, customer: u8[varint], items: Item[varint] }
```

In most languages `Order` is a struct owning a `String` and a `Vec<Item>`,
each `Item` owning another `String`. Here an order is its header followed by
its items, laid end to end, and the whole order book is one array of those.

```goose
// "SKU:qty:cents;..." -> items, built straight into the field that gets them
fn parse_items(field: u8[:]) -> Item[>..] {
    var items: Item[>..] = [];
    each_split(field, ';') {
        let p = split(it, ':');
        let qty, ok1 = parse_int(p[1]);
        let cents, ok2 = parse_int(p[2]);
        assert(ok1 && ok2);
        items.push(Item { sku: p[0], qty: qty, cents: cents });
    };
    return items;
}

var book: Order[>..] = [];
book.push(Order { id: 1001, customer: "alice",
                  items: parse_items("SKU-441:2:1999;SKU-7:1:500") });

for o in book {
    var t = 0;
    for it in o.items { t += it.qty * it.cents; }
    print(o.customer, " owes ", t, " cents over ", o.items.len, " items");
}
```

```
alice owes 4498 cents over 2 items
```

It is worth counting the bytes, because this is the sort of record a real
program has millions of. That order — id 1001, customer `alice`, two items
with 5 and 7-character SKUs — is:

| | bytes | allocations |
|---|---:|---:|
| Goose, as declared above | **29** | **0** |
| C++ `std::string` + `std::vector<Item>` | 160 | 1 |
| Rust `String` + `Vec<Item>` | 153 | 4 |

The C++ and Rust figures are `sizeof` plus the blocks actually allocated,
measured with MSVC and rustc; they do not include the allocator's own
per-block header and rounding, which would add more. C++ gets off lightly
here only because all three strings are short enough for the small-string
optimization — one character more in a SKU and it is four allocations too.

Five times smaller is a cache story, not a bookkeeping one: the whole book
streams. And `id`, `qty` and `cents` cost one byte each rather than eight,
because a `varint` is as wide as its value needs.

Note also `parse_items` returning a growable array by value and that costing
nothing: the callee is compiled knowing its destination and writes the items
directly into the order's field, inside the book. Returning a built-up value
and out-parameter style are the same cost in Goose, which is why the
out-parameter style mostly does not appear.

The price of variable-size elements is that the array becomes **sequential**:
offsets are data-dependent, so you can iterate it but not index it.

```goose
for o in book { print(o); }     // fine
print(book[0]);                 // error: arrays of variable-size elements
                                // cannot be indexed, only iterated
```

That is a real restriction, and it is why the array family has both kinds.
Use variable elements for things you walk (records, parsed nodes, log lines);
use fixed elements, with a `u8[..k]` for the string part, for things you
index and sort.

---

## 9. Enums, in two sizes

Algebraic data types are the only dynamic polymorphism in the language. No
inheritance, no traits, no vtables.

```goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }
```

What is unusual is that every ADT type can be stored **two ways**, chosen at
the point of use:

**Fixed mode** (`Shape`) is what Rust's `enum` and C++'s `variant` do: a tag
plus room for the largest payload, here 1 + 16 = 17 bytes for every value,
`Dot` included. In exchange it is indexable and you may overwrite an element
with a different variant.

```goose
var shapes: Shape[>..] = [];
shapes.push(Shape.Circle { r: 1.0 });
shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
shapes.push(Shape.Dot);
shapes[2] = Shape.Rect { w: 1.0, h: 1.0 };      // a different variant, in place
```

**Variable mode** (`Shape..` — note the trailing dots) gives each value
exactly its own variant's size: a `Dot` is 1 byte, a `Circle` 9, a `Rect` 17.
The price is that the elements are packed against each other, so the array is
sequential and a value can never change variant. In exchange you may take
references *into* a payload:

```goose
var packed: Shape..[>..] = [];
packed.push(Shape.Circle { r: 1.0 });
packed.push(Shape.Dot);
packed.push(Shape.Rect { w: 2.0, h: 3.0 });

for s in packed {
    match s {
        Rect &r => { r.w += 1.0; },     // edits the payload in place, inside the array
        _ => {},
    }
}
```

The dichotomy is exact and deliberate: **replaceable XOR
interior-referenceable**. A fixed-mode value can be overwritten with another
variant, so nothing may point inside it; a variable-mode value can be pointed
into, so it can never be overwritten. That is what keeps it sound, and it is
why `&`-binders in a `match` are legal only on variable-mode payloads.

For a tree of mostly-small nodes the memory difference is large. It is the
single biggest source of the memory column in the benchmarks: the `records`
benchmark uses 4.0x less memory than a Rust `enum` whose every element is
sized for its `String` arm, and runs 2.0–2.3x faster for the same reason —
the cache does less work.

### `match`, and match written as functions

The inline form is what you expect:

```goose
fn name(s: Shape) -> u8[:] {
    match s {
        Circle c => "circle",
        Rect r => if r.w == r.h { "square" } else { "rectangle" },
        _ => "other",
    }
}
```

The other form is **case functions**: one overload per variant, called with
the enum. This is the virtual-call idiom, without the vtable:

```goose
fn area(s: Shape.Circle) -> f64 { 3.14159 * s.r * s.r }
fn area(s: Shape.Rect) -> f64   { s.w * s.h }
fn area(s: Shape.Dot) -> f64    { 0.0 }

for s in shapes { print(s, " area ", area(s)); }
```

The call dispatches on the tag through a jump table, and exhaustiveness is
checked exactly as for a `match` — add a variant and this stops compiling
until it gets its case. Variant types (`Shape.Circle`) are ordinary types you
can name and pass around.

---

## 10. Linking things without pointers

A `T&` is a machine address: eight bytes, no arithmetic, no casts, never
dangling. For a data structure with a lot of links, eight bytes per link is
often more than you want, and an address is not something you can save to a
file.

So Goose also has **relative references**, which are a link stored as a
narrow offset:

* `T&<u32>` — *self-relative*: an offset from the field itself to the target,
  which must live in the same enclosing array. Position-independent, so a
  structure built out of these means the same thing wherever it sits — hold
  that thought, because §15 cashes it in.
* `T&<u32 in pool>` — *pool-relative*: an offset from a named global pool's
  base. A store is a subtraction from a base already in a register, and — the
  thing self-relative cannot do — other arrays can hold links *into* the
  pool.

Widths are `u8`, `u16`, `u32`, `u64` or `varint`. Loading either form gives
you an ordinary `T&`; storing one re-encodes. `T&<u32>?` uses offset 0 as
null, so a link costs four bytes including its null.

Here is a binary search tree living in one grow-only array, with 4-byte
self-relative children:

```goose
struct Node { key: i32, count: i32 = 1, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];                  // .= binds the element; = would copy it
    loop {
        if key == cur.key { cur.count += 1; return; }
        if key < cur.key {
            let l = cur.left;
            if l { cur .= l; continue; }             // narrowed by `if`: l is a Node&
            cur.left .= pool.push(Node { key: key });   // the push cannot move cur
            return;
        }
        let r = cur.right;
        if r { cur .= r; continue; }
        cur.right .= pool.push(Node { key: key });
        return;
    }
}
```

A few things are happening here that are worth naming.

**`.=` binds and rebinds a reference.** Since references are *transparent* —
an expression that denotes a reference behaves as its target everywhere, with
no `*` and no `->` — there has to be some way to say "I mean the reference
itself", and `.=` is it. `var cur .= pool[0];` names the element;
`cur .= l;` retargets. (`.==` is the matching identity test: `==` compares
the two nodes, `.==` asks whether they are the same node.)

**Optionals are references that can be null, narrowed by flow.** `Node?` is a
nullable `Node&` — address 0, no space cost. You cannot touch it until an
`if`, `while`, `guard` or `assert` has narrowed it, and inside that region it
simply *is* a `Node&`.

**The insert is one store.** `cur.left .= pool.push(...)` pushes the node and
writes the offset, and the push cannot invalidate `cur` because nothing
moves. In safe Rust this is where you switch to `Vec<Node>` plus `u32`
indices.

The node is 16 bytes: two `i32`s and two 4-byte links, where a tree of
`Box<Node>` or `unique_ptr<Node>` would be 24 bytes of node plus an
allocation header per node. A thousand random inserts:

```
442 nodes, 7072 bytes of tree, no allocator involved
```

### `self`, and sentinels

A non-optional relative reference has no null, so a structure whose links
point back at itself could never be constructed — there is nothing yet to
point at. `self`, legal only as the whole initializer of such a field, means
"the value this literal is constructing":

```goose
struct Node { val: i64, prev: Node&<u32 in pool>, next: Node&<u32 in pool> }

reusable var pool: Node[>..] = [];
let head = pool.alloc_index(Node { val: 0, prev: self, next: self });
```

That is a circular doubly-linked list's sentinel, allocated before `main`
runs. Because the sentinel exists, no link in the whole list is ever null,
every insert and remove is the same four stores with no special cases, and
every link load is a plain add rather than an add plus a null test.

```goose
fn insert_after(p: Node&, v: i64) -> Node& {
    let n .= pool.alloc_ref(Node { val: v, prev: p, next: p.next });
    n.next.prev .= n;
    p.next .= n;
    return n;
}

fn remove(n: Node&) {
    n.prev.next .= n.next;
    n.next.prev .= n.prev;
    pool.free(pool.index_of(n));
}

fn is_end(n: Node&) -> bool { n .== pool[head] }
```

A node is 8 + 4 + 4 = 16 bytes, against a `std::list` node plus a map node in
the C++ version of the same thing — which is most of why the `lru` benchmark
uses 3.2x less memory.

---

## 11. When lifetimes really are not nested

Stacks are wonderful until your lifetimes are not nested — a cache, a mutable
graph, a file tree where things get deleted in any order. Goose's answer is
the `reusable` pool: a grow-only array the compiler pairs with a hidden
freelist.

```goose
reusable var pool: Item[>..] = [];

let s0 = pool.alloc_index(Item { 10, 1.0 });
let s1 = pool.alloc_index(Item { 11, 1.0 });
pool.free(s0);
let s2 = pool.alloc_index(Item { 12, 2.0 });   // takes slot s0 back
```

`alloc_index` / `alloc_ref` take a free slot if there is one and push
otherwise; `free(i)` records a slot for reuse. That looks like an allocator,
so it is worth being precise about what `free` does and does not do:

> **All elements remain valid at all times.** `free` does not release any
> memory and does not end any lifetime — it adds an index to a freelist.
> The slot is still a live, well-typed `Item` afterwards, and it still
> belongs to the pool, which still belongs to its owning scope.

So there is nothing here to be unsafe. A reference to a freed-and-reused slot
reads a *different `Item`* — a perfectly good one, just not the one you were
thinking of. That is the same class of mistake as keeping an index into an
array you have since overwritten: a logic bug, and one you can reason about
locally. Memory is never accessed at a type it was not written with, and
nothing goes out of bounds, because nothing was freed.

What it costs, then, is not safety but tidiness: within a pool, elements are
managed loosely rather than by scope, and it is on you to stop naming a slot
you have handed back. That is the price of expressing lifetimes a stack
cannot.

Running the linked list above through it:

```
built: 10 20 30 40 50   (6 slots)
evens removed: 10 30 50   (6 slots)
two more, both from the freelist: 10 30 50 7 8   (6 slots)
```

There is a second flavour, `reusable[]`, that hands out *runs* of elements
rather than single slots — `alloc_slice(n)`, `realloc_slice(s, n)`,
`free_slice(s)` — with a freelist of spans that merges neighbours. That is
what you use for the child lists of an n-ary tree, where each node's entries
want to be one contiguous run you can walk as a plain slice
([`26_file_tree`](../samples/26_file_tree.goose) builds a whole `mkdir`/`mv`/
`rm -r` file tree out of the two pools together).

---

## 12. Errors, and `return … from`

There is no exception mechanism and no `Result` type, and no specified error
convention either — it is the application's choice. For a call with two
outcomes the trailing `bool` you have already seen is enough
(`let age, ok = parse_age(s);`), and for "found or not" it is a `T?`, which
costs nothing because it is a reference into the input. Neither needs
explaining.

The one that is not obvious is **`return E from f`**: it returns `E` as the
result of the innermost active call of `f`, unwinding every frame in between.
It is the language's lightweight exception, and it exists because the two
idioms above scale badly — a failure eight frames deep inside a parser would
otherwise mean eight signatures carrying an error they have nothing to do
with.

```goose
fn load(text: u8[:]) -> User[>..], u8[] {
    var users: User[>..] = [];
    var lineno = 0;
    each_split(text, '\n') {
        lineno++;
        if it.len > 0 { users.push(parse_record(it, lineno)); }
    };
    return users, "";
}

fn parse_record(line: u8[:], lineno: i64) -> User {
    let comma = find(line, ",");
    guard comma >= 0 else { return [], str("line ", lineno, ": expected name,age") from load; }
    let age, ok = parse_age(line[comma + 1..]);
    guard ok else { return [], str("line ", lineno, ": bad age") from load; }
    return User { name: trim(line[..comma]), age: age };
}
```

```
loaded 2 users
error: line 2: expected name,age
error: line 1: bad age
```

Look at what `parse_record` does *not* have. Its return type is `User` — not
`Result<User, E>`, not `(User, bool)`. It has no error parameter, no `?` on
the calls it makes, and nothing to propagate. The same is true of every
function between it and `load`, however many there are. All the error
handling in the program is the two `guard`s that produce a message and the
one `if err.len > 0` that reports it.

Three things make this cheap rather than clever:

* **It is checked statically.** Validity is a compile-time property: every
  call site of a function containing `return … from load` must lie inside the
  dynamic extent of a `load` call, and the whole-program compiler verifies
  that in call-graph order. There is no "uncaught" case at runtime.
* **The implementation is a hidden discriminant**, not an unwinder. Each
  frame on the path gains a "this result is mine" versus "propagate to
  `load`" flag; a caller checks it and returns immediately in the second
  case. Normal epilogues run, so every data-stack watermark restores by
  itself. No tables, no `setjmp`, no destructors — there are none to run.
* **The result lands where `load`'s caller wanted it.** `str("line ", …)`
  builds its message directly at `load`'s return destination, straight past
  the frames being unwound.

So the happy path pays for a flag check per frame on the way out and nothing
else, which is why the parsers in the samples use this for *every* syntax
error rather than reserving it for catastrophes
([`17_calc`](../samples/17_calc.goose),
[`18_json`](../samples/18_json.goose) — a bad token, a missing bracket and a
bad escape are all one line each).

The same machinery is what lets a block `return` from its enclosing function
rather than from the HOF calling it (§13), which is the other place you will
meet it.

And the ones that stop the program rather than reporting: `assert(c)` for
invariants, `abort(msg)` with a message, `exit(code)`. The checker knows the
last two never return, so either can be the whole of a `guard`'s else.

---

## 13. Generics, and blocks that disappear

An untyped parameter is a generic one. That is the whole feature:

```goose
fn twice(x) { x + x }
fn span<T>(a: T, b: T) -> T { if a > b { a - b } else { b - a } }
```

`<T>` says that two parameters must agree, or names a type no parameter
carries. Type arguments are inferred from the arguments — `foo(1)`, never
`foo<i64>(1)` — and written out only for a type variable no argument
mentions:

```goose
fn zero<T>() -> T { default<T>() }   // T's default value

let half = zero<f64>() + 0.5;
```

Everything is monomorphized, so there is nothing abstract to typecheck: a
generic body is only ever checked at an instantiation where every type is
concrete, and errors come with the compile-time call chain that produced
them.

Overloads resolve on argument types, and a slice parameter takes any array
kind:

```goose
fn describe(x: i64) -> u8[>..]     { str("int ", x) }
fn describe(x: f64) -> u8[>..]     { str("float ", x) }
fn describe(xs: i64[:]) -> u8[>..] { str(xs.len, " ints, first ", xs[0]) }

print(describe(7), "; ", 2.5.describe(), "; ", xs.describe(), "; ", describe(xs[2..]));
```

Function values are **compile-time entities**. They are passed as generic
parameters, every call is direct and inlinable, and they cannot escape —
storing one, returning one or putting one in data is a compile error. There
are no closures-as-objects and no runtime function pointers. The payoff is
that a higher-order function compiles to exactly the loop it looks like:

```goose
fn each_pair<T, F>(xs: T[:]) {
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}

var gaps: i64[>..] = [];
each_pair(xs) { a, b => gaps.push(b - a); };     // writes an enclosing local
```

`{ ... }` after a call is the trailing-block sugar, with `it` as the implicit
parameter or `a, b =>` for named ones. The standard library is written this
way throughout, and `filter`/`map` build their results straight into the
variable receiving them — no intermediate array, no allocation:

```goose
let evens   = xs.filter() { it % 2 == 0 };
let squares = xs.map()    { it * it };
let total   = fold(xs, 0) { acc, x => acc + x };
sort(xs) { a, b => a > b };
```

A block may also `return` from the **lexically enclosing function**, not from
the HOF calling it — implemented with the same `return from` machinery — so a
hand-written iterator has the same structuring power as a `for` loop:

```goose
fn each_step<F>(lo: i64, hi: i64, step: i64) {
    var i = lo;
    while i < hi { F(i); i += step; }
}

fn first_multiple_of_7(lo: i64, hi: i64) -> i64 {
    each_step(lo, hi, 3) { if it % 7 == 0 { return it; } };   // returns from first_multiple_of_7,
                                                              // not from each_step
    return -1;
}
```

Nested functions see the enclosing function's locals, which is how the
parsers in the samples are written: `parse`'s input, cursor and pools are its
locals, and the recursive descent functions nested inside it use them as free
variables instead of passing a state struct around
([`17_calc`](../samples/17_calc.goose),
[`18_json`](../samples/18_json.goose)).

One thing to know about recursion: it is opt-in and annotated. The entry of a
recursive cycle is a `recursive fn`, every function in the cycle needs an
explicit signature, and **no function in a cycle may own growable data** —
that would need a new data stack per activation. So a recursive builder takes
the pool it grows as a parameter:

```goose
recursive fn in_order(n: Node&, out: i32[>..]&) {
    let l = n.left;
    if l { in_order(l, out); }
    out.push(n.key);
    let r = n.right;
    if r { in_order(r, out); }
}
```

That is a real constraint on how you write recursive code, and it is also
most of why recursion depth costs nothing but native stack.

---

## 14. Threads that share nothing

There is no shared mutable memory, at all. A worker is declared `thread_fn`,
and the compiler compiles it and everything it calls as a **separate program**
with its own data stacks and its own copies of the globals it uses.

Values cross between them through typed queues — one queue per type, selected
by the type — and the values that may cross must be **flat**: no references,
no slices, at any depth. Which is cheap, because a flat Goose value is
contiguous, so "copy it into the queue" is a memcpy.

```goose
struct Job    { n: i32 }
struct Result { n: i32, digits: i32 }

thread_fn worker() {
    loop {
        let job = qget<Job>();          // blocks
        guard job.n >= 0;               // -1 means stop
        qput(Result { n: job.n, digits: count_digits(job.n) });
    }
}

fn main() {
    let n = max(1, hardware_threads());
    for i in 8 { qput(Job { n: (1 << (i * 3)) as i32 }); }
    for i in n { qput(Job { n: -1 }); }              // one quit job per worker
    var ids: i64[>..] = [];
    for i in n { ids.push(thread_spawn(worker)); }
    var total = 0;
    for i in 8 { let r = qget<Result>(); total += r.digits; }
    for id in ids { thread_wait(id); }
    print(n, " workers, ", total, " digits in total");
}
```

Because a "flat value" includes variable-size parts, a job or a result can
carry real data rather than a handle to it — a `struct Row { y: i32, cells:
u8[] }` crosses the queue as one copy, pixels included, which is exactly what
[`23_mandelbrot_threads`](../samples/23_mandelbrot_threads.goose) does.

A worker that increments a global increments *its own copy*; `main`'s is
untouched. If that sounds restrictive, notice what it removes: there are no
data races to have, and no locks, atomics or memory ordering anywhere in the
language.

(Threads need a real C compiler — the TinyCC in-process backend cannot place
thread-local storage, so compile with `-o` for these.)

---

## 15. Your data is already a file format

If a structure's links are self-relative offsets, it means the same thing
wherever it sits in memory. So saving it is a write of its bytes, and loading
it is a read. No serializer, no schema, no pointer fixups, no allocation per
node.

```goose
struct Node { word: u8[..24], count: i32, left: Node&<u32>?, right: Node&<u32>? }

var index: Node[>..] = [];
// ... build the tree ...

var image: u8[>..] = index.to_bytes();      // a framed image of the array
var loaded, ok = from_bytes<Node[>..]>(image);
print("loaded ", ok, ", ", loaded.len, " nodes, 'fox' = ", lookup(loaded, "fox"));
```

`bytes_of(a)` is the same thing without the copy — a read-only *view* of the
element region, which you can hand straight to `write_file`.

The interesting half is `from_bytes`. Bytes arriving from outside the program
are the one place a reference could enter without the compiler having proved
it, so `from_bytes` **verifies before the bytes become a value**: the
framing, every tag, every length, and every link landing on an element start
of that same image. A corrupt or hostile file is a `false` and an empty
array, never a wild reference.

```goose
image[image.len - 3] = 200;                  // tamper with a link
var bad, bok = from_bytes<Node[>..]>(image);
print("accepted: ", bok, ", nodes ", bad.len);
```

```
loaded true, 9 nodes, 'fox' = 4
accepted: false, nodes 0
```

What it verifies is *safety*, not integrity — an image whose data bytes were
edited still describes a well-formed structure, and it is a checksum, not the
verifier, that tells you the file is the one you wrote. What comes back is an
ordinary array: it indexes, it grows, references into it are ordinary
references, and the lifetime system needs no new rule for it.

---

## 16. Calling C

An `extern fn` binds a Goose signature to a C symbol. That is the whole FFI,
and it is how the `math` and `os` modules are built.

```goose
extern "cbrt" fn cube_root(x: f64) -> f64;
extern fn hypot(x: f64, y: f64) -> f64;

extern fn crc32_bytes(s: const u8[:]) -> u32;   // a slice in, a scalar out
extern fn stats_of(xs: i32[:], out: Stats&);    // a struct filled through a reference
extern fn c_version(out: u8[>..]&);             // a builder C appends to
```

What may cross is exactly what has a plain C shape: the scalars and `bool`,
flat fixed-size structs and arrays (by value, or through a reference as a
pointer), slices of those (as a `{ data, len }` struct), and a `u8[>..]&`
builder. Anything else is rejected at the declaration rather than
mis-marshalled at the call. A call compiles to a direct C call with no
calling-convention extras. Your own C arrives with `--include header.h`.

---

## 17. About the speed

Goose is not fast because of a clever optimizer. It is fast because of what
it does not do, and it is worth knowing which is which.

**Where the wins come from.** No allocator on any path. No teardown — a scope
exit is a pointer store, not a walk. Contiguous data, so the cache does less
work: an array of records is *one* block, not one block per record. Narrow
links: 2 and 4-byte offsets where a pointer would be 8. And variable-mode
enums, where a tree of mostly-small nodes stops paying for its largest
variant everywhere.

**Where it does need the optimizer**, and where you can help it. Every index
is bounds-checked, and the compiler proves most of them away — but "most" is
not "all", and what it fails to prove stays in the program. The idiom that
works is to give the analysis a length it can use. Here is a blur kernel over
row slices:

```goose
let W = 512;

// Each row of the image is a slice; the caller passes three source rows and
// one destination row per output row.
fn blur_row(above: u8[:], here: u8[:], below: u8[:], out: u8[:]) {
    assert(above.len == W && here.len == W && below.len == W && out.len == W);
    for x in 1..W - 1 {
        let sum = above[x - 1] as i64 + above[x] + above[x + 1] +
                  here[x - 1]  + here[x]  + here[x + 1] +
                  below[x - 1] + below[x] + below[x + 1];
        out[x] = (sum / 9) as! u8;
    }
}
```

```
bce: elided 11/11 index and 0/4 slice checks
```

One `assert` per row, and every index inside the pixel loop is a loop counter
plus a constant against a known length, so all eleven checks go and the C
backend sees the loop it would have seen from C. Without it, none of them do,
and the loop does not vectorize. `--bce-lines` will tell you, per line,
exactly which checks survived — use it when a kernel is slower than you
expect.

Two other things the language hands the analysis for free: a grow-only array
can only shrink at a `pop`/`resize`/`clear` the compiler can see, so a bound
established before a `push` still holds after it; and `%` being Euclidean
means a reduction is in range by construction.

**What the numbers actually say** ([`bench/summary.md`](../bench/summary.md),
[`bench/results.md`](../bench/results.md)): 3.3x the speed of idiomatic C++,
1.16x hand-optimized C++, 1.04–1.12x the best safe Rust, on 1.9x/1.3x/1.2x
less memory, over sixteen benchmarks. Read the losses too — they are in
there, with explanations. `particles` is a flat float kernel where the design
never predicted an advantage and there is none. `calc`, at 0.82x against
Rust, is the one loss the summary attributes to Goose's own design rather
than to a backend, and it says exactly which three things cost it. Every
measurement is whole-process wall clock including teardown, which is a real
cost the other languages pay and Goose does not.

---

## 18. What it costs you

You will meet all of these.

* **You have to think about where data lives.** Not constantly, but the
  question "who owns this, and how long does its scope last" is one you now
  answer explicitly. Most of the time the answer is "the function that builds
  it", and that is free. When it is not, it is a `reusable` pool.
* **Recursive functions cannot own growable data.** Pass the pool in. This
  changes how a recursive-descent parser is structured — the state becomes
  locals of the non-recursive entry function, which is arguably nicer, but it
  is a change.
* **Arrays of variable-size elements cannot be indexed.** You pick, per
  container, between "compact and walkable" and "indexable".
* **A fixed-mode enum cannot be pointed into; a variable-mode one cannot be
  overwritten.** You pick, per use site.
* **No closures that escape, no function pointers, no dynamic dispatch beyond
  ADT tags.** The closed-world compiler is what makes roots and destinations
  static; it is also what stops you writing a plugin system.
* **Whole-program compilation.** No separate compilation, no shared
  libraries of Goose code.
* **A `reusable` pool manages its elements loosely.** Nothing is freed and
  nothing is unsafe, but a slot you handed back and then still name reads
  whatever its next owner put there — the array equivalent of a stale index,
  and yours to avoid.
* **v1 omissions** you will notice: no move for resizables, one resizable per
  struct, no labeled break, no namespace privacy, no directory listing or
  networking in the library yet.

---

## 19. Where to go next

* **[`samples/`](../samples/README.md)** — twenty-six complete programs in
  reading order, each one commented for what it demonstrates. Start with
  `01_tour` and `02_memory`, then jump to whatever looks like your problem.
  `13_linked_list`, `14_bst` and `18_json` are the ones that show the data
  structure story properly; `26_file_tree` is the most recent and uses both
  pool kinds together.
* **[`docs/stdlib.md`](stdlib.md)** — the library reference. Five modules:
  `std`, `dictionary`, `vec`, `math`, `os`. It is small and it is all
  readable Goose under `stdlib/`.
* **[`docs/goose_spec.md`](goose_spec.md)** — the actual rules, when you want
  to know why something did not compile. It is precise rather than friendly,
  and it is where every "§" in this document points.
* **[`bench/`](../bench/summary.md)** — the numbers and the design notes
  behind them.
* **[`docs/implementation.md`](implementation.md)** — how the compiler works,
  if you want to hack on it.

One last piece of advice: when the checker rejects something, read the whole
error. It names both ends — the reference *and* where it was bound, the
holder *and* the line that stored into it, the instantiation *and* the call
chain that produced it. It is trying to tell you which of the two to move,
and it is usually right.
