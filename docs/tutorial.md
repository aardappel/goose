# Goose, by example

<details>
<summary>Table of contents</summary>

- [1. The core idea](#1-the-core-idea)
- [2. Running something](#2-running-something)
- [3. Familiar syntax](#3-familiar-syntax)
- [4. Where values live](#4-where-values-live)
- [5. The array family](#5-the-array-family)
- [6. Strings are byte arrays](#6-strings-are-byte-arrays)
- [7. Slices that cannot dangle](#7-slices-that-cannot-dangle)
- [8. Nested data stays inline](#8-nested-data-stays-inline)
- [9. Enums, in two sizes](#9-enums-in-two-sizes)
- [10. Linking things without pointers](#10-linking-things-without-pointers)
- [11. Lifetimes that do not nest](#11-lifetimes-that-do-not-nest)
- [12. Errors, and `return … from`](#12-errors-and-return--from)
- [13. Generics, and blocks that disappear](#13-generics-and-blocks-that-disappear)
- [14. Threads that share nothing](#14-threads-that-share-nothing)
- [15. Saving and loading data as bytes](#15-saving-and-loading-data-as-bytes)
- [16. Calling C](#16-calling-c)
- [17. About the speed](#17-about-the-speed)
- [18. Limitations](#18-limitations)
- [19. Calling Goose from C](#19-calling-goose-from-c)
- [20. Where to go next](#20-where-to-go-next)

</details>

This tutorial introduces Goose through examples. It assumes some experience
with C, C++, Rust, Go, or a similar language and focuses on Goose's
differences. Start here, then go on to
[the language reference](reference.md), which covers the whole language topic
by topic. Consult [the specification](goose_spec.md) for exact rules, and see
[`samples/`](../samples/README.md) for complete programs.

Much of the syntax is familiar: braces, `let`, `fn`, `struct`, and `match`.
The main difference is how arrays store and manage data. We start with where
values live, since that explains many of the language's other choices.

---

## 1. The core idea

Here is an array of strings.

```goose
var words: u8[][>..] = [];
for i in 5 { words.push(str("word", i)); }
```

In C++ that is a `vector<string>`: one heap allocation for the vector, one
per string, a pointer and a capacity per element, and a walk over all of them
when it goes out of scope. In Rust it is a `Vec<String>` with the same shape.

In Goose it is **one contiguous block of bytes**: a length, then five
back-to-back `(length, characters)` runs. The block was created by bumping a
pointer, and it disappears when the enclosing scope ends and that pointer
moves back. No heap allocator or per-string deallocation is needed, and the
block contains no pointers.

```
words:  05 00 00 00 | 05 w o r d 0 | 05 w o r d 1 | 05 w o r d 2 | ...
```

The memory model has four main properties:

* Memory is a handful of big **data stacks**. Growth is a pointer bump.
* Every dynamic value is **owned by a variable**. The only deallocation is a
  scope ending, which resets a pointer.
* Values nest **inline**. A string inside a struct inside an array is bytes
  inside bytes inside bytes.
* You may still take **references into a growing container**, and the
  compiler proves they never dangle, without annotations such as `'a` and
  without `Rc`.

This model has limitations. They are listed in
[section 18](#18-limitations), together with the rules that impose
them and how to work with each. It also has measurable benefits: over
sixteen benchmarks Goose runs at about **3.3x the speed of idiomatic C++**,
roughly level with hand-tuned C++ and with the best safe Rust, while using
**1.9x less memory** than the C++ and 1.2x less than the Rust
([`bench/summary.md`](../bench/summary.md)). Most of the gains come from
differences in data layout and memory management.

---

## 2. Running something

Goose compiles the whole program to C and builds that with whatever C
compiler is around, into an executable beside the source:

```bash
goose build hello.goose && ./hello
```

`goose -o hello.c hello.goose` writes the C for you to build yourself, and
`goose build --dry-run hello.goose` prints the commands.
[command_line.md](command_line.md) describes the rest of `goose build` and
the other commands, and [packages.md](packages.md) how a program uses
libraries from other repositories.

The bundled TinyCC backend needs no separate C compiler. With no verb, or
with `goose run`, the program is compiled into the compiler's own process and
run there, which is what you want while you are reading this:

```bash
goose hello.goose
```

Here is a minimal program:

```goose
fn main() {
    print("hello, goose");
}
```

`print` takes any number of arguments of any type, renders each as text and
writes them all as a single line. It uses a single `write`, so the output of
two threads never interleaves. There is no format string and no `<<`.

```goose
print("answer=", 42, " ratio=", 0.75, " yes=", 3 > 2);
```

Every value can be printed, not only scalars: arrays print as `[1, 2, 3]`,
structs as `Point { 1, 2 }`, and a `u8` array as its bytes. The samples print
a lot because printing is a convenient way to inspect values.

---

## 3. Familiar syntax

`let` creates a binding that cannot be reassigned; `var` allows reassignment.
Neither makes the contents read-only: use `const` for that. Types follow the
name and can usually be inferred:

```goose
let answer = 42;
var count: i32 = 0;
let ratio = 0.75;
count += 1;
count++;              // a statement, not an expression
```

A struct literal either names its fields or lists them in order, and it may
leave out fields that declare a default. A literal that ends in `..` may
leave out any field: a field without a declared default then gets its type's
default (zero, an empty array or string, or null). Globals are declared like
locals, and a `var` global given only a type starts out with these same
defaults:

```goose
struct Unit { kind: i32, hp: i32 = 100, x: f32, y: f32, name: u8[] }

let scout = Unit { kind: 2, x: 1.5, .. };   // hp 100, y 0, name ""
var selected: Unit;                         // kind 0, hp 100, x 0, y 0, name ""
var log: u8[][>..];                         // empty
```

Functions are free functions. There are no methods and no `impl` blocks, but
`x.f(a)` calls `f(x, a)`, so you can write method-call syntax
wherever it reads better:

```goose
fn total(xs: i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    return t;
}
```

`if`, `match`, `block` and `{ }` are **expressions**: a block's value is its
trailing expression. So there is no ternary operator, and no need to declare
a variable and then assign it in every branch:

```goose
let kind  = if hp > 50 { "healthy" } else { "hurt" };
let level = match hp { 0 => "dead", 1..50 => "low", _ => "fine" };
```

`guard` states a condition that the rest of its block depends on.
`guard c;` runs the rest of the block only where `c` holds, and
`guard c else { … }` runs the `else` block instead. To leave more than the
current block, put a `return`, `break` or `continue` in that `else` block.
`block` gives `break` a target without a loop, so a chain of checks can bail
out with a value:

```goose
let verdict = block {
    guard p.alive else { break "gone"; }
    guard p.hp > 0 else { break "dead"; }
    "standing"
};
```

`defer` runs a statement or a block when the enclosing block ends, whichever
way it ends: the end of the block, `break`, `continue`, `return`, or a
`return … from` passing through. The cleanup stays next to what it cleans
up, and runs on the ways out that the code after it does not mention:

```goose
guard gfx::open("demo", 640, 480, 0) else { return; }
defer gfx::close();                 // every way out of this block from here on
```

A `defer` in a loop body runs at the end of each iteration, and an exit runs
only the defers it has run past, latest first. The body cannot jump out, and
it cannot use long-distance returns, since it may run while one is in
flight.

Loops are `for x in range_or_array`, `while`, and `loop`:

```goose
for i in 3 { }              // 0, 1, 2
for i in 10..13 { }         // 10, 11, 12
for i: i32 in 0..n { }      // i is an i32; n must widen to one
for i: u8 in 0..256 { }     // every u8
for x in xs { }             // copies, for fixed-size elements
for &x in xs { x *= 2; }    // references: the mutation form
for x, i in xs { }          // with the index
```

Several return values are a calling convention, not a tuple type (Goose has
no tuple type). By custom, a trailing `bool` says whether the other values
mean anything:

```goose
fn divide(a: i64, b: i64) -> i64, bool {
    if b == 0 { return 0, false; }
    return a / b, true;
}

let q, ok = divide(7, 2);
```

To assign the results to variables, fields or elements that already exist,
use `=`: `q, ok = divide(9, 3);`.

Only `return` and a call's results carry several values, so a branch cannot
end in `a, b`. `return if c { 1, 2 } else { 3, 4 };` is an error; instead,
each branch returns on its own, as in `divide`.

Trailing parameters can have defaults, which a call may leave out:

```goose
fn limit(x: i64, lo: i64 = 0, hi: i64 = 100) -> i64 {
    if x < lo { lo } else if x > hi { hi } else { x }
}

print(limit(250), " ", limit(-5, -10));   // 100 -5
```

A default is evaluated at each call that leaves it out. It can refer to the
same things a top-level declaration can (globals, constants and functions),
but never to the caller's variables, even ones with matching names.

Three arithmetic rules need particular attention:

**Arithmetic happens at the operands' own width.** `u8 + u8` is an 8-bit add.
A narrower type widens into a wider one implicitly; nothing narrows without
`as` (range-checked in debug) or `as!` (never checked). `u32 + i32` is a
compile error that asks which one you meant; the language does not silently
promote both to 64 bits. The declared widths determine the operation.

Integers become floats implicitly, so `n * x` with `x: f32` is an `f32`
multiply; a float becomes an integer only through `as`. A float literal has
no width of its own, and neither does an expression made of float literals
and integers: `n * 0.5` is computed in `f32` when it is passed to an `f32`
parameter, and is an `f64` in `let h = n * 0.5;`. An `if` or `match` whose
branches are all literals adapts to its context as a literal does. A branch
that is a literal takes the type of the other branches:
`if big { x } else { 0.5 }` is an `f32` for an `x: f32`.

A cast that changes nothing gets a warning, so the compiler points out the
casts that these rules make unnecessary. With `b: u8`,
`let w: i64 = b as i64;` gives the warning
``redundant `as i64`: a u8 converts to i64 implicitly here``. A cast that
picks a type or an operation's width, as in `let id = b as i32;` or
`(a as i64) * b`, is not redundant and gets no warning.

**`%` is Euclidean.** The result is in `[0, |b|)` and never negative, at
every integer type. So `x % n` is a valid index into a length-`n` array for
*any* `x`, and the compiler can drop the bounds check instead of demanding a
guard you know is redundant.

```goose
print(-7 % 3, " ", 7 % -3);       // 2 1
```

Integer `/` truncates toward zero: `-7 / 3` is `-2`. It does not pair with
Euclidean `%` for negative dividends. Import `std` for `rem(a, b)`, the
remainder that pairs with `/`, and `div_euclid(a, b)`, the quotient that
pairs with `%`. For example, a coordinate of `-1` in tiles 10 wide has tile
`div_euclid(-1, 10) == -1` and offset `-1 % 10 == 9`.

**Unsigned arithmetic wraps, in every build**, by definition, because that
is what hashes and PRNGs mean by unsigned math. Signed overflow aborts in
debug and wraps in release.

---

## 4. Where values live

A Goose program has the native call stack, some static data, and **N data
stacks**, where N is a small number the compiler works out for your program.
A data stack is a multi-gigabyte address-space reservation with a bump
pointer. There is no other memory system: no `malloc`, no free list, no GC,
no reference counting and no destructors.

So every dynamic value is owned by a variable, and the rules that make this
work are:

1. Values are pushed in lifetime order and popped **en masse** when the
   owning scope exits.
2. At most one *resizable* value is live per stack, and it is the topmost
   thing on that stack for its entire lifetime. So growth is a pointer bump
   with no capacity check, and the value **never moves**.

The compiler proves all of this statically. There is no runtime bookkeeping
beyond the bump pointers.

"One resizable per stack" sounds alarming until you notice the plural in
"N data stacks": three growable arrays alive at once get three stacks, and a
stack is reused as soon as a scope frees it. N is whatever the deepest point
of your program needs, computed once at compile time. An unused stack needs
only reserved address space. You never write any of this down: the language
has no syntax for stacks.

### Leaving a scope frees its data

```goose
for round in 3 {
    var scratch: u8[>..] = [];
    for i in 100000 { scratch.push((i % 256) as! u8); }
    print("round ", round, ": ", scratch.len, " bytes of scratch");
}
```

Each iteration gets a fresh 100 KB buffer, and at the closing brace the
stack's top goes back to where it was. No destructor runs and no list is
walked, so "freeing" that buffer takes a single store.

### Nothing moves, so references survive growth

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

Neither a `vector<T>` nor a `Vec<T>` can promise that: both reallocate, which
invalidates references into them. This property is why so much Goose code
holds typed references where the equivalent Rust code holds `u32` indices
into an arena, and it accounts for a good share of the benchmark gains.

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

Without the `copy`, `var b = a;` binds `b` to `a` by reference. The same
holds at a call: `f(xs)` passes a growable array by reference, without
copying the elements, whether the parameter is declared as
`fn f(xs: i64[:])`, as `fn f(xs: i64[>..]&)` or as an untyped `fn f(xs)`. A
function that needs its own copy calls `copy`.

---

## 5. The array family

Goose has no single "array type". It has several, and they differ only in
what can happen to their size.

| Type | What it is | Metadata |
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
stack.push(1); stack.push(2); stack.push(3);
let top = stack.pop();
stack.pop_n(2);                    // drops the last two at once

var small: u8[..8] = [];           // lives inline anywhere a fixed value can
small.push('h'); small.push('i');
```

Which to reach for:

* **`[>..]` is the default.** Use it for arenas, pools, string builders, tree
  storage, and scratch space refilled between phases. "Grow-only" guarantees
  that references into the array stay valid. You can still use `pop` and `clear`
  where the compiler can prove that nothing points into it.
* **`[>..<]`** for a stack or priority queue. The restriction is that
  references into it may be held in variables only, never in storage, so
  that the compiler can track them when it checks a shrink.
* **`[..k]` / `[..]`** when the array must sit inline inside something else,
  such as a 16-byte name inside a struct or a small list inside an array
  element.
* **`[]` / `[varint]`** for finished data: a string in a record, a node in a
  pool, anything built once and then only read.

Only `[>..]` and `[>..<]` are "resizable", and the one-per-stack rule applies
only to those two. The other kinds can go anywhere.

---

## 6. Strings are byte arrays

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

Text that spans several lines is written as a `"""` string. It is raw, with
no escapes, and the indentation of its closing `"""` is removed from every
line, so the string can be indented to match the code around it:

```goose
print("""
    usage: wc [-l] file
      -l   count lines only
    """);
```

```
usage: wc [-l] file
  -l   count lines only
```

`str(...)` formats into a fresh string built *directly at its destination*:

```goose
words.push(str("word", i));         // written straight into the new element
```

There is no temporary string that is then copied into the array. The
copy-free construction guarantee
([spec §4.3](goose_spec.md#43-the-in-place-construction-guarantee)) says
that a constructed value is always built in its final location, which is
passed down through calls, and that the compiler must not need to fall back
to building it somewhere else first.

Slicing text copies nothing, because a slice is a pointer and a count into
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

`split` gives you an **array of slices into the input**. Each part takes a
few bytes and no text is copied, where most languages return a list of newly
allocated strings:

```goose
let parts = split("alice,30,paris", ',');
print(parts.len, " parts: ", parts);      // 3 parts: ["alice", "30", "paris"]
```

`words` splits at runs of whitespace instead, and drops the empty parts,
which is what you want for a command line:

```goose
let cmd = words("  go   north ");
print(cmd.len, " words: ", cmd);          // 2 words: ["go", "north"]
```

A struct can also hold its strings inline, so an array of such structs is
one block that you can sort and copy as plain data:

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

A value that can be one of several strings is a view of whichever string it
turns out to be. When one branch is a slice, as a string literal is, a
branch that gives an inline string field gives a slice of that whole field,
with nothing copied, and the `if` is a `const u8[:]`:

```goose
let who = if people.len > 3 { "a crowd" } else { people[1].name };
print(who);                             // grace
```

---

## 7. Slices that cannot dangle

`T[:]` is a reference plus a count, like C++'s `span`/`string_view` or
Rust's `&[T]`. It lets a function process a range of any array kind without
copying its elements.

```goose
fn total(xs: i64[:]) -> i64 { var t = 0; for x in xs { t += x; } return t; }

total(fixed);         // a T[k]
total(grow);          // a T[>..]
total(grow[1..4]);    // a sub-range
total(grow[^2..]);    // ^k counts from the end
```

Any destination of slice type accepts an array this way, not only a
parameter: a variable, a field, an element or a return value. The slice
views the array itself, as `fixed[..]` would, and is `const` when the array
is:

```goose
let view: const i64[:] = fixed;   // fixed itself, not a copy
fixed[0] = 99;
print(view[0]);                   // 99
```

The difference from `string_view` is that this one is checked. Every
reference and slice carries a static **root**: the variable that bounds the
lifetime of what it points at. The lifetime system consists of one rule:

> A reference must not outlive the variable that owns its target, and must
> never observe its target at a wrong type.

There are **no lifetime annotations anywhere in the language**. Roots are
inferred, and functions are specialized per root. Through this
monomorphization you get the precision of Rust lifetimes without writing any
lifetime syntax. There are also no aliasing or exclusivity rules: two
references to the same thing are fine, because without shared-memory
concurrency, aliasing alone cannot break type safety in Goose. The checker
tracks lifetimes and valid access; it does not require references to be
exclusive.

A lifetime error identifies both the reference's root and its destination:

```goose
var line: u8[>..] = [];
// ...
let w = line[..5];
print(w);
line.clear();        // fine: w's last use is behind the clear
```

A scratch buffer that is refilled on each iteration can hand out slices of
itself freely. A reusable scratch buffer and a structure you can point into
have the same type.

Because a slice never copies, it also makes a natural *key*. The library's
dictionary keyed by `u8[:]` stores no strings, only addresses and lengths
into text you already have:

```goose
let text = "the quick brown fox jumps over the lazy dog the fox";
var counts = dictionary<const u8[:], i32> {};
each_split(text, ' ') { counts.update(it, 0) { it += 1; }; };
print(counts.count, " distinct words");
```

That is all of word-frequency counting, with nothing copied. The file is
read once into one buffer, and every key, every table row and every line of
the final report points into that buffer
([`15_word_freq`](../samples/15_word_freq.goose) does this over a book).

The `const` qualifier describes writability. Writability is inferred
rather than annotated: a string literal is read-only, and `&x` of a `var` is
writable. A *slot* (a field, an element or a global) has to say which kind
it holds. `dictionary<const u8[:], i32>` says "these keys may be read-only
views", which allows literals and other read-only views to be keys.
Parameters are generic over constness, so you almost never write `const`,
except on a slot that must accept read-only data and on a parameter you
want to document as read-only.

---

## 8. Nested data stays inline

A struct may contain variable-size parts, and they sit **inline**, in
declaration order. Combine that with `varint` fields (a LEB128 integer that
takes as many bytes as its value needs), and a record becomes a run of bytes
with no indirection in it:

```goose
struct Item  { sku: u8[varint], qty: varint, cents: varint }
struct Order { id: varint, customer: u8[varint], items: Item[varint] }
```

In most languages `Order` is a struct owning a `String` and a `Vec<Item>`,
with each `Item` owning another `String`. In Goose an order is its header
followed by its items, laid end to end, and the whole order book is one array
of such orders.

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

It is worth counting the bytes, because a real program can have millions of
records like this one. That order (id 1001, customer `alice`, and two items
with 5- and 7-character SKUs) takes:

| | bytes | allocations |
|---|---:|---:|
| Goose, as declared above | **29** | **0** |
| C++ `std::string` + `std::vector<Item>` | 160 | 1 |
| Rust `String` + `Vec<Item>` | 153 | 4 |

The C++ and Rust figures are `sizeof` plus the blocks actually allocated,
measured with MSVC and rustc; they do not include the allocator's own
per-block header and rounding, which would add more. C++ needs a single
allocation here only because all three strings are short enough for the
small-string optimization; with one more character in a SKU, C++ would need
four allocations too.

The smaller layout reduces cache traffic when traversing the order book. And
`id`, `qty` and `cents` take one byte each rather than eight, because a
`varint` is as wide as its value needs.

Note also that `parse_items` returns a growable array by value without any
copying: the callee is compiled knowing its destination, and writes the items
directly into the order's field, inside the book. In Goose, returning a
built-up value costs the same as writing through an out-parameter, which is
why out-parameters rarely appear.

The downside of variable-size elements is that the array becomes
**sequential**: offsets are data-dependent, so you can iterate it but not
index it.

```goose
for o in book { print(o); }     // fine
print(book[0]);                 // error: arrays of variable-size elements
                                // cannot be indexed, only iterated
```

Use variable elements for data you traverse, such as records, parsed nodes, and
log lines. Use fixed elements for data you index and sort, with a `u8[..k]` if
an element needs a string. When you need both, keep a second array of references
as an index. You can index and sort those references while the variable-size
elements stay packed in place. Section 10 ends with an example.

---

## 9. Enums, in two sizes

Algebraic data types are the only dynamic polymorphism in the language.
Goose has no inheritance, traits or vtables.

```goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }
```

What is unusual is that every ADT type can be stored **two ways**, chosen at
the point of use:

**Fixed mode** (`Shape`) is what Rust's `enum` and C++'s `variant` do: a tag
plus room for the largest payload, here 1 + 16 = 17 bytes for every value,
including `Dot`. In return, an array of fixed-mode values can be indexed, and
you may overwrite an element with a different variant.

```goose
var shapes: Shape[>..] = [];
shapes.push(Shape.Circle { r: 1.0 });
shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
shapes.push(Shape.Dot);
shapes[2] = Shape.Rect { w: 1.0, h: 1.0 };      // a different variant, in place
```

**Variable mode** (`Shape..`, note the trailing dots) gives each value
exactly the size of its own variant: a `Dot` is 1 byte, a `Circle` 9, a
`Rect` 17. The downside is that the elements are packed against each other,
so the array is sequential (unless you keep an index beside it, see
[section 10](#an-index-for-variable-size-elements)), and a value can never
change variant. In return, you may take references *into* a payload:

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

The two modes offer different guarantees. A fixed-mode value can be
overwritten with another variant, so nothing may point inside it. A
variable-mode value can be pointed into, so it can never be overwritten.
This keeps both modes sound, and it is why `&`-binders in a `match` are legal
only on variable-mode payloads.

For a tree of mostly-small nodes the memory difference is large. It is the
biggest single source of the memory savings in the benchmarks: the `records`
benchmark uses 4.0x less memory than a Rust `enum` in which every element is
sized for its `String` variant, and it runs 2.0–2.3x faster for the same
reason, because the cache does less work.

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

An arm may list several variants, as in `Circle, Dot => "round"`. Such an
arm binds no payload, because each variant's payload is a type of its own.

A match on an integer accepts literals, half-open ranges and named constants
(`let` globals, or `ns::NAME` from another namespace), so a program's own
codes can appear by name in a `match` too. One arm may list several of them.
The type of the scrutinee decides whether a bare name is a variant or a
constant:

```goose
let GROUND = 0;
let SHALLOW = 1;
let DEEP = 4;
let LAVA = 7;

fn terrain(t: i64) -> u8[:] {
    match t {
        GROUND => "ground",
        SHALLOW..DEEP => "water",
        LAVA, 10..12 => "hot",
        _ => "rock",
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
checked as it is for a `match`: if you add a variant, this code stops
compiling until the new variant gets its own case. Variant types
(`Shape.Circle`) are ordinary types you can name and pass around.

---

## 10. Linking things without pointers

A `T&` is a machine address. It is eight bytes, allows no arithmetic or
casts, and never dangles. For a data structure with a lot of links, eight
bytes per link is often more than you want, and an address is not something
you can save to a file.

So Goose also has **relative references**, which store a link as a narrow
offset:

* `T&<u32>` — *self-relative*: an offset from the field itself to the target,
  which must live in the same enclosing array. These links are
  position-independent, so a structure built from them can be relocated as a
  whole. Section 15 explains how this supports serialization.
* `T&<u32 in pool>` — *pool-relative*: an offset from a named global pool's
  base. Storing one is a subtraction from a base already in a register. Unlike
  self-relative links, these let other arrays hold links *into* the pool.

Widths are `u8`, `u16`, `u32`, `u64` or `varint`. Loading either form gives
you an ordinary `T&`, and storing a `T&` into one re-encodes it. `T&<u32>?`
uses offset 0 as null, so a link takes four bytes including its null.

Here is a binary search tree stored in one grow-only array, with 4-byte
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

The example uses three reference features:

**`.=` binds and rebinds a reference.** References are *transparent*: an
expression that denotes a reference behaves as its target everywhere, with no
`*` and no `->`. So there has to be some way to say "I mean the reference
itself", and `.=` does that. `var cur .= pool[0];` binds `cur` to the
element; `cur .= l;` retargets it. (`.==` is the matching identity test:
`==` compares the two nodes, while `.==` asks whether they are the same
node.)

**Optionals are references that can be null, narrowed by flow.** `Node?` is a
nullable `Node&`: null is address 0, so it takes no extra space. You cannot
use it until an `if`, `while`, `guard` or `assert` has narrowed it, and
inside the narrowed region it *is* a `Node&`.

**The insert is one store.** `cur.left .= pool.push(...)` pushes the node and
writes the offset, and the push cannot invalidate `cur` because nothing
moves. In safe Rust this is where you switch to `Vec<Node>` plus `u32`
indices.

The node is 16 bytes: two `i32`s and two 4-byte links, whereas a tree of
`Box<Node>` or `unique_ptr<Node>` would need 24 bytes of node plus an
allocation header per node. A thousand random inserts give:

```
442 nodes, 7072 bytes of tree, no allocator involved
```

### `self`, and sentinels

A non-optional relative reference has no null, so a structure whose links
point back at itself could never be constructed, because there is nothing to
point at yet. `self` means "the value this literal is constructing", and it
is legal only as the whole initializer of such a field:

```goose
struct Node { val: i64, prev: Node&<u32 in pool>, next: Node&<u32 in pool> }

reusable var pool: Node[>..] = [];
let head = pool.alloc_index(Node { val: 0, prev: self, next: self });
```

This is the sentinel of a circular doubly-linked list, allocated before
`main` runs. Because the sentinel exists, no link in the list is ever null.
Every insert and remove is the same four stores, with no special cases, and
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

A node is 8 + 4 + 4 = 16 bytes, compared with a `std::list` node plus a map
node in the C++ version of the same structure. That difference is most of
the reason the `lru` benchmark uses 3.2x less memory.

### An index for variable-size elements

Section 8 said an array of variable-size elements can be walked but not
indexed: where an element starts depends on every element before it. Since
other arrays can hold links into a pool, you can keep the index yourself in
a second array, with one 4-byte link per element, pushed alongside it:

```goose
enum Mark {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Label { text: u8[varint] },
    Dot,
}

var marks: Mark..[>..] = [];
var mark_at: (Mark..&<u32 in marks>)[>..] = [];

fn main() {
    mark_at.push(marks.push(Mark.Label { text: "hello, goose" }));
    mark_at.push(marks.push(Mark.Circle { r: 1.5 }));
    mark_at.push(marks.push(Mark.Dot));
    mark_at.push(marks.push(Mark.Rect { w: 2.0, h: 3.0 }));
    mark_at.push(marks.push(Mark.Label { text: "x" }));
    for i in [3, 0, 4, 1, 2] {
        match mark_at[i] {
            Circle c => print(i, ": circle ", c.r),
            Rect r => print(i, ": rect ", r.w, " by ", r.h),
            Label l => print(i, ": label \"", l.text, "\""),
            Dot => print(i, ": dot"),
        }
    }
}
```

```
3: rect 2 by 3
0: label "hello, goose"
4: label "x"
1: circle 1.5
2: dot
```

`push` builds each mark in place at the end of `marks` and returns a reference
to it. When that reference is stored in `mark_at`, it becomes the mark's
offset from the base of `marks`. `mark_at[i]` is then a load and an add, so
the marks can be reached in any order.

Look at what `marks` holds. A `Label` consists of its tag, a one-byte length
and its text, all inside the element: with `hello, goose` it is 14 bytes, and
the next mark starts where the text ends. The five marks, including their
strings, take 44 bytes in one block, with no pointers and no allocations. An
enum in Rust or C++ has one fixed size, so a string variant holds a
fixed-size string object, which stores any text that does not fit in it
somewhere else, and every element is as large as the largest variant. Here
each value is exactly as long as its contents, and the index can still reach
any of them at 4 bytes per element.

The array that an `in` offset is measured from has to be a global. An array
local to a function can be indexed the same way with plain references,
`(Mark..&)[>..]`, at 8 bytes per entry.
[`05_shapes`](../samples/05_shapes.goose) ends with this example.

---

## 11. Lifetimes that do not nest

Some data has lifetimes that do not nest: cache entries, mutable graph nodes,
or files deleted in arbitrary order. Goose supports these uses with a
`reusable` pool: a grow-only array the compiler pairs with a hidden
freelist.

```goose
reusable var pool: Item[>..] = [];

let s0 = pool.alloc_index(Item { 10, 1.0 });
let s1 = pool.alloc_index(Item { 11, 1.0 });
pool.free(s0);
let s2 = pool.alloc_index(Item { 12, 2.0 });   // takes slot s0 back
```

`alloc_index` / `alloc_ref` take a free slot if there is one, and push a new
element otherwise. `free(i)` records a slot for reuse. That looks like an
allocator, so it is worth being precise about what `free` does and does not
do:

> **All elements remain valid at all times.** `free` does not release any
> memory and does not end any lifetime; it adds an index to a freelist.
> The slot is still a live, well-typed `Item` afterwards, and it still
> belongs to the pool, which still belongs to its owning scope.

Reuse preserves memory and type safety. A reference to a slot that was freed
and reused reads a *different `Item`*: a valid one, but not the one you were
thinking of. That is the same class of mistake as keeping an index into an
array you have since overwritten: a logic bug, and one you can reason about
locally. Memory is never accessed at a type it was not written with, and
nothing goes out of bounds, because nothing was freed.

Within a pool, the program manages slot reuse independently of scope.
After returning a slot, stop using references to its old contents: they
remain memory safe but may read a replacement value.

Running the linked list above on such a pool gives:

```
built: 10 20 30 40 50   (6 slots)
evens removed: 10 30 50   (6 slots)
two more, both from the freelist: 10 30 50 7 8   (6 slots)
```

There is a second flavour, `reusable[]`, that hands out *runs* of elements
rather than single slots, with `alloc_slice(n)`, `realloc_slice(s, n)` and
`free_slice(s)`. Its freelist holds spans and merges neighbouring ones. Use
it for the child lists of an n-ary tree, where each node's entries should be
one contiguous run that you can walk as a plain slice
([`26_file_tree`](../samples/26_file_tree.goose) builds a complete
`mkdir`/`mv`/`rm -r` file tree out of the two pools together).

---

## 12. Errors, and `return … from`

There is no exception mechanism, no `Result` type, and no specified error
convention either: the application chooses. For a call with two outcomes,
the trailing `bool` you have already seen is enough
(`let age, ok = parse_age(s);`). For "found or not" it is a `T?`, which
copies nothing because it is a reference into the input. Both are ordinary
return values.

**`return E from f`** returns `E` as the result of the innermost active call of
`f`, unwinding every frame in between. It provides lightweight exceptions. With
the two idioms above, a failure eight calls deep in a parser would require all
eight functions to carry an error in their signatures, even if they only pass it
along.

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

Look at what `parse_record` does *not* have. Its return type is `User`, not
`Result<User, E>` or `(User, bool)`. It has no error parameter, no `?` on
the calls it makes, and nothing to propagate. The same is true of every
function between it and `load`, however many there are. All the error
handling in the program consists of the two `guard`s that produce a message
and the one `if err.len > 0` that reports it.

Three properties keep the cost low:

* **It is checked statically.** Validity is a compile-time property: every
  call site of a function containing `return … from load` must lie inside the
  dynamic extent of a `load` call, and the whole-program compiler verifies
  that in call-graph order. There is no "uncaught" case at runtime.
* **The implementation is a hidden discriminant**, not an unwinder. Each
  frame on the path gains a flag that says either "this result is mine" or
  "propagate to `load`", and a caller checks it and returns immediately in
  the second case. Normal epilogues run, so every data-stack watermark is
  restored automatically. There are no unwind tables and no `setjmp`, and no
  destructors run, because there are none.
* **The result is built where `load`'s caller wants it.** `str("line ", …)`
  builds its message directly at `load`'s return destination, without
  passing it through the frames being unwound.

So on the success path the only cost is one flag check per frame on the way
out. That is why the parsers in the samples use this for *every* syntax
error rather than reserving it for catastrophes
([`17_calc`](../samples/17_calc.goose),
[`18_json`](../samples/18_json.goose): a bad token, a missing bracket and a
bad escape take one line each).

The [`exception` module](stdlib.md#exception) packages this mechanism as
`exception::try<E>(body) { error => ... }` and `exception::throw(value)`.
It can catch owned text or application-defined error values without naming
the application's outer function at each throw site. See its reference for
the ownership and recursion restrictions. Its `finally` is a `defer`.

The same machinery lets a block `return` from its enclosing function rather
than from the HOF that calls it
([section 13](#13-generics-and-blocks-that-disappear)). That is the other
place you will meet it.

Some calls stop the program instead of reporting an error: `assert(c)` for
invariants, `abort(msg)` with a message, and `exit(code)`. The checker knows
that the last two never return, so a branch that ends in either one gives no
value. That knowledge also propagates through ordinary calls, including
wrappers around `exception::throw`: a value-returning function can end by
calling a function that never returns, without a dummy result afterward.
Statements and a trailing expression after a proven exit are not
typechecked. Unknown recursive calls are conservatively assumed to return.

---

## 13. Generics, and blocks that disappear

An untyped parameter is generic:

```goose
fn twice(x) { x + x }
fn span<T>(a: T, b: T) -> T { if a > b { a - b } else { b - a } }
```

`<T>` either says that two parameters must have the same type, or names a
type that no parameter carries. Type arguments are normally inferred from
the arguments, as in `foo(1)`. An explicit list such as `foo<i64>(1)` can
supply leading type arguments; it is required for a type variable that no
argument determines:

```goose
fn zero<T>() -> T { default<T>() }   // T's default value

let half = zero<f64>() + 0.5;
```

Everything is monomorphized, so there is nothing abstract to typecheck: a
generic body is only checked at an instantiation where every type is
concrete, and errors are reported with the compile-time call chain that
produced them.

Overloads resolve on argument types, and a slice parameter takes any array
kind:

```goose
fn describe(x: i64) -> u8[>..]     { str("int ", x) }
fn describe(x: f64) -> u8[>..]     { str("float ", x) }
fn describe(xs: i64[:]) -> u8[>..] { str(xs.len, " ints, first ", xs[0]) }

print(describe(7), "; ", 2.5.describe(), "; ", xs.describe(), "; ", describe(xs[2..]));
```

Function values are **compile-time entities**. They are passed as generic
parameters, every call is direct and inlinable, and they cannot escape:
storing one, returning one or putting one in data is a compile error. There
are no closure objects and no runtime function pointers. A higher-order
function can therefore compile to a direct loop:

```goose
fn each_pair<T, F>(xs: T[:]) {
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}

var gaps: i64[>..] = [];
each_pair(xs) { a, b => gaps.push(b - a); };     // writes an enclosing local
```

`{ ... }` after a call is the trailing-block sugar, with `it` as the implicit
parameter, or `a, b =>` for named parameters. The standard library is written
this way throughout, and `filter`/`map` build their results directly in the
variable that receives them, with no intermediate array and no allocation:

```goose
let evens   = xs.filter() { it % 2 == 0 };
let squares = xs.map()    { it * it };
let total   = fold(xs, 0) { acc, x => acc + x };
sort(xs) { a, b => a > b };
```

A block may also `return` from the **lexically enclosing function**, not from
the HOF that calls it. This uses the same `return from` machinery, and it
means that a `return` in the block of a hand-written iterator works as it
would in the body of a `for` loop:

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

Nested functions can use their enclosing function's locals. The sample parsers
use this to share their input, cursor, and pools: all are locals of `parse`,
accessible to the recursive descent functions nested inside it. This avoids
passing a state struct through every call
([`17_calc`](../samples/17_calc.goose), [`18_json`](../samples/18_json.goose)).

Recursion is opt-in and annotated. The entry of a recursive cycle is a
`recursive fn`, every function in the cycle needs an explicit signature, and
**no function in a cycle may hold growable data across a call back into the
cycle**, because every activation would need a data stack of its own. So a
recursive builder takes the pool it grows as a parameter:

```goose
recursive fn in_order(n: Node&, out: i32[>..]&) {
    let l = n.left;
    if l { in_order(l, out); }
    out.push(n.key);
    let r = n.right;
    if r { in_order(r, out); }
}
```

A function in a cycle may still own scratch it is done with before it
recurses, as long as the scratch's scope ends before the call. Here each
line is built and printed in a block, so its stack is free again when the
recursive calls start:

```goose
recursive fn show(n: Node&, depth: i64) {
    {
        var line: u8[>..] = [];
        for i in depth { line.append("  "); }
        line.append(str(n.key));
        print(line);
    }
    let l = n.left;
    if l { show(l, depth + 1); }
    let r = n.right;
    if r { show(r, depth + 1); }
}
```

That is a real constraint on how you write recursive code, and it is also
the main reason that each level of recursion uses only native stack.

### Several tables, one context

A compiler or a simulation keeps its state in several growable tables (nodes,
symbols, output) and wants to pass them around together. A struct cannot
own them, because a struct holds at most one resizable array (as its last
field), and a recursive function cannot keep them across its recursive
calls. Instead, make each table a local of a driver function, and pass the
code that works on the tables a single struct of references to them:

```goose
struct Node { at: i64, first: i64, next: i64 }   // a source offset, first child, next sibling

struct Ctx {                 // a slice and three references: fixed-size, cheap to pass
    source: const u8[:],
    nodes: Node[>..]&,
    text: u8[>..]&,
    scratch: i64[>..<]&,     // the recursion's working stack
}

recursive fn walk(c: Ctx&, n: i64, depth: i64) -> i64 {
    let mark = c.scratch.len;
    c.scratch.push(depth);                 // this activation's entries, above its callers'
    c.text.push(c.source[c.nodes[n].at]);
    var total = c.scratch[mark];
    var e = c.nodes[n].first;
    while e >= 0 {
        total += walk(c, e, depth + 1);
        e = c.nodes[e].next;
    }
    c.scratch.resize(mark);                // gone again before it returns
    return total;
}

fn compile(source: const u8[:]) -> i64 {
    var nodes: Node[>..] = [];
    var text: u8[>..] = [];
    var scratch: i64[>..<] = [];
    nodes.push(Node { at: 0, first: 1, next: -1 });   // a root with two children
    nodes.push(Node { at: 1, first: -1, next: 2 });
    nodes.push(Node { at: 2, first: -1, next: -1 });
    var c = Ctx { source: source, nodes: nodes, text: text, scratch: scratch };
    let r = walk(c, 0, 1);
    print(text, " ", scratch.len);
    return r;
}
```

`compile("abc")` prints `abc 0` and returns 5. `walk` pushes into, indexes
and shrinks the tables through `c`, as it would through separate reference
parameters. The tables are locals of `compile`, so they are owned outside
the cycle and outlive every activation of `walk`. The grow-shrink `scratch`
is a stack shared by the whole recursion: each activation pushes above its
callers' entries and truncates back to its mark before it returns.

The context lives for one call of `compile`. A second call, for another file
in the same process, starts from fresh tables, and when each call's scope
exits, all its tables are freed at once, so there is no global state to
reset. When the code that works on the tables is small enough to fit inside
the driver, nested functions can use the tables as free variables with no
struct, as the samples' parsers do. The struct is for code spread over many
functions or files.

---

## 14. Threads that share nothing

There is no shared mutable memory. A worker is declared `thread_fn`, and the
compiler compiles it and everything it calls as a **separate program** with
its own data stacks and its own copies of the globals it uses.

Values pass between threads through typed queues, with one queue per type,
selected by the type. The values that may cross must be **flat**: they hold
no references and no slices, at any depth. Copying them is cheap, because a
flat Goose value is contiguous, so copying it into the queue is a memcpy.

```goose
struct Job    { n: i32 }
struct Result { n: i32, digits: i32 }

thread_fn worker() {
    loop {
        let job = qget<Job>();           // blocks
        guard job.n >= 0 else { break; } // -1 means stop
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

Because a "flat value" can include variable-size parts, a job or a result
can carry the data itself rather than a handle to it. A `struct Row { y: i32,
cells: u8[] }` crosses the queue as one copy, including its pixels, which is
what [`23_mandelbrot_threads`](../samples/23_mandelbrot_threads.goose) does.

A worker that increments a global increments *its own copy*, and `main`'s
copy is unchanged. That may sound restrictive, but it means there can be no
data races, and the language has no locks, atomics or memory ordering.

(Threads need an external C compiler: the in-process TinyCC backend cannot
place thread-local storage, so compile threaded programs with `-o`.)

---

## 15. Saving and loading data as bytes

If a structure's links are self-relative offsets, the structure means the
same thing wherever it is in memory. So saving it is a write of its bytes,
and loading it is a read. There is no serializer, no schema, no pointer
fixup and no allocation per node.

```goose
struct Node { word: u8[..24], count: i32, left: Node&<u32>?, right: Node&<u32>? }

var index: Node[>..] = [];
// ... build the tree ...

var image: u8[>..] = index.to_bytes();      // a framed image of the array
var loaded, ok = from_bytes<Node[>..]>(image);
print("loaded ", ok, ", ", loaded.len, " nodes, 'fox' = ", lookup(loaded, "fox"));
```

`bytes_of(a)` is the same thing without the copy: a read-only *view* of the
element region, which you can pass directly to `write_file`.

The more interesting half is `from_bytes`. Bytes arriving from outside the
program are the only place where a reference could enter without the
compiler having proved it valid, so `from_bytes` **verifies before the bytes
become a value**. It checks the framing, every tag, every length, and that
every link points to the start of an element in that same image. A corrupt or
hostile file gives `false` and an empty array, never a wild reference.

```goose
image[image.len - 3] = 200;                  // tamper with a link
var bad, bok = from_bytes<Node[>..]>(image);
print("accepted: ", bok, ", nodes ", bad.len);
```

```
loaded true, 9 nodes, 'fox' = 4
accepted: false, nodes 0
```

What it verifies is *safety*, not integrity. An image whose data bytes were
edited still describes a well-formed structure, so it takes a checksum, not
the verifier, to tell you that the file is the one you wrote. What comes back
is an ordinary array: you can index it and grow it, references into it are
ordinary references, and the lifetime system needs no new rule for it.

---

## 16. Calling C

An `extern fn` binds a Goose signature to a C symbol. The `math` and `os`
modules use this foreign-function interface.

```goose
extern fn cube_root(x: f64) -> f64 = "cbrt";
extern fn hypot(x: f64, y: f64) -> f64;

extern fn crc32_bytes(s: const u8[:]) -> u32;   // a slice in, a scalar out
extern fn stats_of(xs: i32[:], out: Stats&);    // a struct filled through a reference
extern fn c_version(out: u8[>..]&);             // a builder C appends to
```

The types that may cross are exactly those with a plain C shape: the scalars
and `bool`, flat fixed-size structs and arrays (by value, or through a
reference as a pointer), slices of those (as a `{ data, len }` struct), and
a `u8[>..]&` builder. Anything else is rejected at the declaration rather
than mis-marshalled at the call. A call compiles to a direct C call with no
calling-convention extras. Your own C can be added with
`--include header.h`.

---

## 17. About the speed

Goose's performance depends on both its memory model and compiler
optimization. The distinction helps when choosing data layouts and tuning
loops.

**Benefits of the memory model.** There is no allocator on any path. There
is no teardown: a scope exit is a pointer store, not a walk. Data is
contiguous, so the cache does less work: an array of records is *one* block,
not one block per record. Links are narrow: 2- and 4-byte offsets where a
pointer would be 8. And with variable-mode enums, a tree of mostly-small
nodes no longer reserves room for its largest variant in every node.

**Where it does need the optimizer**, and where you can help it. Every index
is bounds-checked, and the compiler proves most of the checks unnecessary
and removes them. But "most" is not "all", and any check it cannot prove
unnecessary stays in the program. What helps is to give the analysis a
length it can use. Here is a blur kernel over row slices:

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

`goose check --stats` reports what it removed:

```
bce: elided 11/11 index and 0/4 slice checks
```

With one `assert` per row, every index inside the pixel loop is a loop
counter plus a constant, checked against a known length. So all eleven
checks are removed, and the C backend gets the same loop it would get from
C. Without the assertion, none of them are removed, and the loop does not
vectorize. `--bce-lines` tells you, per line, which checks remain; use it
when a kernel is slower than you expect.

Two language rules also help the analysis. A grow-only array can only shrink
at a `pop`/`pop_n`/`resize`/`clear` the compiler can see, so a bound
established before a `push` still holds after it. And because `%` is
Euclidean, the result of a reduction is always in range.

**What the numbers say** ([`bench/summary.md`](../bench/summary.md),
[`bench/results.md`](../bench/results.md)): over sixteen benchmarks, Goose
runs at 3.3x the speed of idiomatic C++, 1.16x the speed of hand-optimized
C++ and 1.05–1.11x the speed of the best safe Rust, on 1.9x/1.3x/1.2x less
memory. The losses are listed too, with explanations. `particles` is a flat
float kernel where the design never predicted an advantage, and there is
none. `calc`, at 0.82x against Rust, is the only loss the summary attributes
to Goose's own design rather than to a backend, and the summary names the
three things that cause it. Every measurement is whole-process wall-clock
time including teardown, which takes time in the other languages and none
in Goose.

---

## 18. Limitations

You will meet all of these. Each one links to the section above that shows
how to work with it, where there is one, and to the specification rule that
imposes it.

* **You have to think about where data lives.** Not constantly, but you now
  answer the question "who owns this, and how long does its scope last"
  explicitly. Most of the time the answer is "the function that builds it",
  and then nothing more is needed. When it is not, the answer is a
  `reusable` pool
  ([section 11](#11-lifetimes-that-do-not-nest);
  [spec §1.2](goose_spec.md#12-memory-model),
  [§5.4](goose_spec.md#54-reusable-arrays-safe-allocation-escape-hatch)).
* **Recursive functions cannot keep growable data across a recursive call.**
  Scratch in a block that ends before the call is fine; anything that lives
  longer is passed in. This changes how a recursive-descent parser is
  structured: the state becomes locals of the non-recursive entry function,
  or a struct of references to those locals. That is arguably nicer, but it
  is a change ([Several tables, one context](#several-tables-one-context);
  [spec §7.8](goose_spec.md#78-recursion)).
* **Arrays of variable-size elements cannot be indexed.** For each
  container you choose between "compact and walkable" and "indexable", or
  you spend extra memory on an index of references beside a compact one
  ([section 10](#an-index-for-variable-size-elements);
  [spec §3.3](goose_spec.md#33-the-array-family)).
* **A fixed-mode enum cannot be pointed into; a variable-mode one cannot be
  overwritten.** You choose the mode at each use site
  ([section 9](#9-enums-in-two-sizes);
  [spec §3.5](goose_spec.md#35-algebraic-data-types),
  [§8.1](goose_spec.md#81-match)).
* **No closures that escape, no function pointers, no dynamic dispatch beyond
  ADT tags.** The closed-world compiler is what makes roots and destinations
  static; it is also what stops you writing a plugin system
  ([section 13](#13-generics-and-blocks-that-disappear);
  [spec §7.6](goose_spec.md#76-static-function-values),
  [§8.2](goose_spec.md#82-case-functions-match-as-an-overload-set)).
* **Whole-program compilation.** There is no separate compilation, and there
  are no shared libraries of Goose code
  ([spec §10.1](goose_spec.md#101-whole-program-call-graph-order)).
* **A `reusable` pool manages its elements loosely.** Nothing is freed and
  nothing is unsafe, but if you still use a slot after handing it back, you
  read whatever its next owner put there. This is the array equivalent of a
  stale index, and it is up to you to avoid it
  ([section 11](#11-lifetimes-that-do-not-nest);
  [spec §9.4](goose_spec.md#94-type-safe-reuse-and-stale-references)).
* **v1 omissions** you will notice: no move for resizables, one resizable per
  struct, no labeled break, no namespace privacy
  ([spec §12](goose_spec.md#12-builtins-the-standard-library-and-what-v1-leaves-out)),
  no subprocesses or networking in the library yet ([`os`](stdlib.md#os)).

---

## 19. Calling Goose from C

`export fn` is the reverse of `extern fn`: it makes a Goose function callable
from C, so a Goose module can be linked into a C or C++ program as a library.

```goose
var calls: i32 = 0;

export fn hello_world() -> const u8[:] {
    calls += 1;
    return "Hello, world!"
}

export "c_add" fn add(a: i32, b: i32) -> i32 { a + b }

fn main() {
    calls = 100;    // runs once, inside goose_init
}
```

An `export fn` is an ordinary Goose function; the compiler adds a C wrapper
for it. The optional string picks the C symbol; without it, the Goose name
is used. When you ask for a header, the compiler builds the C for a C host
program, with no `main` of its own:

```sh
goose --standalone --header hello.h -o hello.c hello.goose
cc hello.c app.c -o app
```

```c
#include "hello.h"
#include <stdio.h>

int main(int argc, char **argv) {
    int64_t length;
    goose_init(argc, argv);
    const uint8_t *message = hello_world(&length);
    fwrite(message, 1, (size_t)length, stdout);   // not NUL-terminated
    putchar('\n');
    printf("%d\n", c_add(20, 22));
    return 0;
}
```

The host calls `goose_init` once before anything else. It sets up the
runtime and the globals and then runs your Goose `fn main()`, so `main` is the
place for a library's own start-up code. A library with nothing to start up
may leave `main` out. A `const u8[:]` result comes back as a pointer plus an
extra `int64_t *` parameter that receives its length; scalars and flat
fixed-size values are returned by value. The generated functions are not
thread-safe: call them from the thread that called `goose_init`. The
signatures that may cross are the ones from [section 16](#16-calling-c), with
a few more limits; see
[spec §7.11](goose_spec.md#711-exporting-goose-functions-to-c).

---

## 20. Where to go next

* **[`docs/reference.md`](reference.md)** — the whole language, topic by
  topic, with a compiling example for every rule: where to go after this
  tutorial, and where to look things up.
* **[`samples/`](../samples/README.md)** — thirty-one complete programs in
  reading order, each one commented for what it demonstrates. Start with
  `01_tour` and `02_memory`, then jump to whatever looks like your problem.
  `13_linked_list`, `14_bst` and `18_json` are the ones that show the data
  structures in detail; `26_file_tree` uses both pool kinds together.
* **[`docs/stdlib.md`](stdlib.md)** — the library reference. Fourteen modules:
  `std`, `dictionary`, `vec`, `math`, `os`, `binary`, `base64`, `csv`,
  `json`, `regex`, `audio`, `gfx`, `physics`, and `ui`, with Goose interfaces
  under `stdlib/`.
* **[`docs/goose_spec.md`](goose_spec.md)** — the detailed language rules,
  including why a program may fail to compile. Every "spec §" link in this
  tutorial points there.
* **[`bench/`](../bench/summary.md)** — the numbers and the design notes
  behind them.
* **[`docs/implementation.md`](implementation.md)** — how the compiler works,
  if you want to hack on it.

One last piece of advice: when the checker rejects something, read the whole
error. It names both ends of the problem: the reference *and* where it was
bound, the holder *and* the line that stored into it, or the instantiation
*and* the call chain that produced it. Use those locations to decide which
binding, store, or scope needs to change.
