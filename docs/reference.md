# The Goose Language Reference

<details>
<summary>Table of contents</summary>

- [1. Lexical structure](#1-lexical-structure)
- [2. Declarations, variables and scope](#2-declarations-variables-and-scope)
- [3. Scalars and arithmetic](#3-scalars-and-arithmetic)
- [4. Where values live](#4-where-values-live)
- [5. The array family](#5-the-array-family)
- [6. Strings and text](#6-strings-and-text)
- [7. Slices](#7-slices)
- [8. Structs](#8-structs)
- [9. Enums (algebraic data types)](#9-enums-algebraic-data-types)
- [10. References and optionals](#10-references-and-optionals)
- [11. Relative references](#11-relative-references)
- [12. Lifetimes: roots, stores and writability](#12-lifetimes-roots-stores-and-writability)
- [13. Growing, shrinking and assigning whole arrays](#13-growing-shrinking-and-assigning-whole-arrays)
- [14. Reusable pools](#14-reusable-pools)
- [15. Index types and checkfree functions](#15-index-types-and-checkfree-functions)
- [16. Expressions and control flow](#16-expressions-and-control-flow)
- [17. Functions](#17-functions)
- [18. Generics and function values](#18-generics-and-function-values)
- [19. Recursion](#19-recursion)
- [20. Long-distance return and errors](#20-long-distance-return-and-errors)
- [21. Program structure](#21-program-structure)
- [22. Threads](#22-threads)
- [23. Serialization](#23-serialization)
- [24. Calling C and being called from C](#24-calling-c-and-being-called-from-c)
- [25. Builtins](#25-builtins)
- [Appendix A. Keywords and operators](#appendix-a-keywords-and-operators)
- [Appendix B. Glossary](#appendix-b-glossary)

</details>

This is the reference for the Goose language: every feature, topic by topic,
with examples. It sits between the two other descriptions of the language.
The [tutorial](tutorial.md) teaches by example and spends its time on what
is different about Goose; the [specification](goose_spec.md) states the
exact rules in the language an implementor needs. This document is for the
programmer who has read the tutorial and wants a description of the whole
language. It is as complete as the specification, but easier to read, and
it has an example program for every rule.

Each section has a pointer of the form "spec §3.3", at its end or within
it, to the specification section that has the final say on the topic.
Features unique to Goose get the most space. Features that work as they do
in C or Rust, such as `if` and `while`, get a paragraph.

Every example in this document compiles with the current compiler, and
`scripts/check_doc_examples.py` checks that they still do. A complete
program is shown with its output after it. A shorter example is either the
body of a `main` or a set of declarations, and is written so that it can
be pasted into a program. Run any of them with

```bash
goose example.goose
```

which compiles the program into the compiler's own process and runs it
there, with no C compiler and no build step. `goose check` typechecks
without running, and `goose build` makes an executable
([command_line.md](command_line.md) has the rest of the command line).

---

## 1. Lexical structure

A Goose source file is UTF-8 text. A byte-order mark at its start is
ignored, and line endings are read as `\n` whatever convention the file
uses.

**Comments** are `//` to the end of the line and `/* … */`, which nests:
`/* a /* b */ c */` is one comment.

**Identifiers** are `[A-Za-z_][A-Za-z0-9_]*`. The keywords are listed in
[Appendix A](#appendix-a-keywords-and-operators). A few words are keywords
only in one position and names everywhere else: `from` after a `return`
value, `upto` after an unsigned type in a type, `inline` and `checkfree`
before `fn`, and `fields`, `variants` and `types` in a `const for` header. `in` is a
keyword throughout.

**Integer literals** are decimal (`42`) or hexadecimal (`0xFF`). The whole
unsigned 64-bit range is accepted: a value above `i64.max` is a `u64`
constant. A character literal `'a'` is an integer constant holding the
byte's value, and it accepts the same escapes as a string literal.

**Float literals** have a `.`, an exponent, or both: `1.5`, `2.5e-3`, `1e9`.
A `.` begins a fraction only when a digit follows it, so `1..2` is a range
and `1.` is not a number. C99 hexadecimal floats with a binary exponent are
accepted too: `0x1.8p3` is 12.0.

**String literals** are `"…"` with the escapes `\n \t \r \0 \\ \" \'` and
`\xNN` (two hexadecimal digits). A literal is static read-only data, of type
`const u8[:]` ([section 6](#6-strings-and-text)).

**Raw strings** are `"""…"""`. They have no escapes, and the next `"""`
ends the text. On one line, the text is everything between the delimiters,
including backslashes and quotes. A multi-line raw string starts with its
opening `"""` at the end of a line and ends with the closing `"""` on a
line of its own. The closing delimiter's indentation is removed from every
line, so the text can be indented to match the surrounding code. The line
breaks just after the opening and just before the closing delimiter are not
part of the text; an empty line before the closing delimiter adds a final
newline.

```goose
fn main() {
    let usage = """
        usage: wc [-l] file
          -l   count lines only
        """;
    print(usage);
    print("""C:\temp\new "quoted" \n""");
}
```

```
usage: wc [-l] file
  -l   count lines only
C:\temp\new "quoted" \n
```

**Other literals**: `true` and `false`; `null`, the empty value of any
optional type ([section 10](#10-references-and-optionals)); and `self`,
which is legal only inside a struct or variant literal and denotes the value
that literal is constructing ([section 11](#11-relative-references)).

### Statements and semicolons

An expression statement ends in `;`. A statement that *is* a block-ended
construct, meaning `if`, `match`, `block`, a loop, `guard … else { }`, a
nested `fn`, or a call with a trailing function block, needs none: it ends
at its closing `}`. An operator never continues such a statement, so to use
one of these constructs as an operand, put it in parentheses:

```goose
let a = 3;
let v = (if a > 2 { 1 } else { 2 }) + 3;
if a > 2 { print("big"); }                 // no `;` needed
let w = if a > 2 { 10 } else { 20 };       // as an initializer: fine as it is
print(v, " ", w);
```

A stray `;` after a block is harmless. Every bracketed, comma-separated
list accepts a trailing comma: struct and array literals, call arguments,
parameter and generic lists, field lists and match arms.

### Evaluation order

Everything evaluates left to right: operands, arguments, and the fields of
a literal. A by-value operand is read before the operands after it run; a
reference or slice operand keeps its address or view, not a snapshot of
the contents. The destination of an assignment is resolved first, and a
compound assignment reads its old value before its right-hand side runs.
Spec §2.

---

## 2. Declarations, variables and scope

### `let`, `var` and `const`

```goose
let answer = 42;              // a binding that cannot be reassigned
var count: i32 = 0;           // a variable: reassignable
const limits = [10, 20, 30];  // neither reassigned nor written into
count += 1;
count++;                      // a statement, not an expression
```

The type follows the name and can usually be left out: an un-annotated
declaration takes the type of its initializer. `let` only forbids assigning
to the name as a whole: the contents of a `let` array or struct are as
writable as their type allows, so `let xs = [1, 2, 3]; xs[0] = 9;` is
fine. Read-only *contents* come from the type, `const T`
([section 12](#12-lifetimes-roots-stores-and-writability)), and
`const x = e;` declares a `let` of type `const T`. A `var` of a `const`
type is the converse: it can be reassigned but never written into.

```goose
let xs = [1, 2, 3];
xs[0] = 9;                    // fine: let forbids `xs = …`, not element writes
const ys = [1, 2, 3];
// ys[0] = 9;                 // error: ys is const
var s: const u8[:] = "text";  // a read-only view that can be re-pointed
s = s[1..];
print(xs, " ", ys, " ", s);
```

A `let` without an initializer gets its value exactly once, and the
compiler checks that it does: no path may read it before it is assigned,
and no path may assign it twice. Assigning it in a loop it is declared
outside of, or in a function called more than once, counts as assigning it
twice.

```goose
fn main() {
    let n: i64;
    let ready = true;
    if ready { n = 1; } else { n = 2; }
    print(n);
}
```

```
1
```

Every local must be initialized before it is read, on every path. Fixed
arrays must be fully initialized; the fill literal `[v; n]` makes this easy
for a large array. An inner scope may redeclare a name, which shadows the
outer one. Spec §4.4.

### Globals

Globals are declared like locals, at the top level of a file, with any
type: scalars, structs, and growable arrays too. They are initialized
before `main` runs, imported files first and then in declaration order
within a file, and their initializers may call functions. A `var` global
given only a type starts out as that type's default value:
`var log: u8[>..];` is an empty array. A `let` or `const` global must be
given its value.
[Section 21](#21-program-structure) has the rest.

### Named constants

A `let` or `const` global with no written type and a constant initializer
is a **named constant**. An integer named constant can be used exactly as
its literal could: it adapts to any integer type its value fits, serves as
an array size or a fill count, and names a value in a `match` arm.

```goose
let SIZE = 16;
let HALF = SIZE / 2;
var grid: u8[SIZE] = [0; SIZE];
fn main() {
    let b: u8 = SIZE;            // adapts, as the literal 16 would
    let wide = SIZE * 1000000000000;
    grid[HALF] = 1;
    print(b, " ", wide, " ", grid.len, " ", match 8 { HALF => "half", _ => "other" });
}
```

```
16 16000000000000 16 half
```

A global *with* a written type (`let SIZE: i64 = 16;`), a local `let`, and
any `var` are ordinary values of their type and do not adapt to other
types. Spec §3.1, §11.1.

### Type aliases

`type Name = T;` names a type. An alias is the same type, not a new one:

```goose
type Id = u32;
type Names = u8[..16][>..];
fn main() {
    let a: Id = 7;
    let b: u32 = a;
    var ns: Names = [];
    ns.push("goose");
    print(a + b, " ", ns);
}
```

```
14 ["goose"]
```

A nominal type comes from a `struct` or `enum` declaration
([section 8](#8-structs), [section 9](#9-enums-algebraic-data-types)).

### Type syntax at a glance

Type syntax is postfix. Every form below is described in its own section;
this table is an overview.

| Written | Meaning | Section |
|---|---|---|
| `i8 … i64`, `u8 … u64`, `f32`, `f64`, `bool`, `varint` | the scalars | [3](#3-scalars-and-arithmetic) |
| `T[4]`, `T[]`, `T[varint]`, `T[..8]`, `T[..]`, `T[>..]`, `T[>..<]` | the array family | [5](#5-the-array-family) |
| `T[:]` | a slice: a view of a run of `T`s | [7](#7-slices) |
| `Point`, `Pair<i64>` | a struct, a generic struct instance | [8](#8-structs) |
| `Shape`, `Shape..`, `Shape.Circle` | an enum in fixed mode, in variable mode, one variant | [9](#9-enums-algebraic-data-types) |
| `T&`, `T?` | a reference, an optional (nullable) reference | [10](#10-references-and-optionals) |
| `T&<u32>`, `T&<u32 in pool>`, `T&<u32>?` | relative references | [11](#11-relative-references) |
| `const T`, `const T&`, `const T[:]` | read-only contents, a read-only reference or view | [12](#12-lifetimes-roots-stores-and-writability) |
| `u32 in a`, `u32 upto a` | an index of the global array `a`, a bound of it | [15](#15-index-types-and-checkfree-functions) |
| `fn(i64) -> bool` | a function value's shape, as documentation | [18](#18-generics-and-function-values) |

`const` qualifies the first reference or slice built on the base type:
`const u8[:]` is a read-only view of bytes. To apply `const` to an outer
reference or slice, use parentheses: `const (u8[:])&` is a read-only
reference to a slice variable. An index type takes no postfix of its own,
so an array of an index type is written `(u32 in a)[>..]`.

---

## 3. Scalars and arithmetic

### Integers

The integer types are `i8 i16 i32 i64` and `u8 u16 u32 u64`, two's
complement, usable everywhere: fields, elements, locals, parameters,
results. `i64` is the conventional type for counts, sizes and indices; it
is what `.len`, integer literals and the builtins produce. The narrower
types are for data that should be compact and for kernels that need a
particular width.

**Arithmetic happens at the operands' own width.** `u8 + u8` is an 8-bit
add, `i32 * i32` a 32-bit multiply. There is no promotion to `int`: the
declared types decide the operation.

**A literal has no type of its own.** It adapts to any integer type whose
range holds it, and is an `i64` where nothing constrains it. `let x: u8 =
255;` is fine and `let x: u8 = 256;` is a compile error.

```goose
let byte: u8 = 255;
let answer = 42;              // an i64
let wrapped: u8 = byte + 1;   // an 8-bit add: 0, since unsigned math wraps
let widened = byte + answer;  // the u8 widens into the i64: 297
print(wrapped, " ", widened);
```

**Mixing widths.** A binary operator's operands must reach one common
type, which is also the result's type. The rules, in order:

1. If the types are equal, that type is the common type.
2. A literal adapts to the other operand's type (an error if it does not
   fit).
3. Otherwise, if exactly one operand widens implicitly into the other's
   type, the wider type is used: `u8 + i64` is an `i64` add.
4. An integer meeting a float converts to the float's type: `i32 * f32` is
   an `f32` multiply.
5. Anything else is a compile error asking for a cast: same-width signed
   and unsigned (`u32 + i32`), or `u64` with anything signed. Nothing is
   silently promoted to 64 bits.

```goose
let a: u32 = 7;
let b: i32 = 3;
// let c = a + b;             // error: u32 and i32 do not unify; cast one
let c = a as i64 + b;         // an i64 add
let d: u8 = 200;
let e = d + 100;              // 100 adapts to u8: an 8-bit add, 44
print(c, " ", e);
```

Shifts are the exception: `x << k` has `x`'s type whatever the count's
type, and the count is masked to the width. Comparisons unify the same way
but produce `bool`.

**`%` is Euclidean.** The result lies in `[0, |b|)` and is never negative,
at every integer type, whatever the dividend's sign. So `x % n` is always a
valid index into an array of length `n`, and the compiler drops the bounds
check on it. A C-style remainder would need a guard before the check could
be dropped.

```goose
print(-7 % 3, " ", 7 % -3, " ", -7 / 3);
```

```
2 1 -2
```

Integer `/` truncates toward zero, so `/` and `%` do not form a matching
quotient/remainder pair for negative dividends. `std` supplies the two
missing functions, at every integer width: `rem(a, b)` pairs with `/`, and
`div_euclid(a, b)` pairs with `%`.

```goose
import std;
fn main() {
    print(-7 / 3, " ", rem(-7, 3));
    print(div_euclid(-7, 3), " ", -7 % 3);
    print(div_euclid(-1, 10), " ", -1 % 10);  // tile and offset
}
```

```
-2 -1
-3 2
-1 9
```

`div_euclid` rounds down for a positive divisor and up for a negative one;
its quotient overflows exactly when `/` does. `rem` is zero or has the
dividend's sign, and `rem(MIN, -1)` is zero at every signed width.
Both functions abort on a zero divisor. Float `%` uses C `fmod`, so
`-7.0 % 3.0` is `-1.0`; these `std` functions take integers only.

**Overflow.** Unsigned arithmetic wraps modulo 2^width in every build, by
definition, since that is what hashes, random number generators and bit
tricks expect from unsigned math. Signed overflow aborts in a debug build
(`--debug`) and wraps in a release build. Division or remainder by zero
always aborts, and so does `i64.min / -1`. A debug build checks each
operation as it executes after optimization. It is therefore a tool for
finding bugs, not a guarantee about every intermediate value written in the
source.

```goose
var h: u64 = 14695981039346656037;
for c in "goose" { h = (h ^ c) * 1099511628211; }   // FNV-1a, wrapping by design
print(h);
```

A `u64` literal above `i64.max`, as above, is accepted; negating one is an
error except for `-9223372036854775808`, which is exactly `i64.min`.
Spec §6.2.

### Floats

`f32` and `f64` are IEEE floats. `f32` arithmetic stays 32-bit; an `f32`
widens to `f64` implicitly, never the reverse. Every integer converts to
either float type implicitly, so `2` works wherever `2.0` would.

A float literal adapts to whichever float type its destination or other
operand has, and is an `f64` where nothing gives it a type. So does an
expression whose float-ness comes only from literals and integers: `n *
0.5` is computed in 32 bits when it is passed to an `f32` parameter and is
an `f64` in `let h = n * 0.5;`. A `let` with no written type, local or
global, whose initializer is such an expression adapts the same way at each
use. This is how `PI` in the standard library works at both widths.

```goose
let PI = 3.141592653589793;
fn area32(r: f32) -> f32 { PI * r * r }     // PI adapts to f32 here
fn main() {
    let n = 3;
    let h = n * 0.5;                        // an f64
    let r: f32 = 2;                         // an integer becomes a float implicitly
    print(h, " ", area32(r), " ", PI);
}
```

```
1.5 12.566371 3.141592653589793
```

A float prints in the shortest form that reads back as the same value *of
its own type*, with `.0` after a whole number: `1.0`, `0.1`, `-0.0`; `inf`,
`-inf` and `nan`. On floats `%` is C's `fmod`. Spec §3.1, §6.3.

### `bool`

`bool` holds `true` or `false` in one byte. Comparisons produce it, and
`if`, `while` and `guard` require it. There is no integer-to-bool coercion,
so `if n { … }` with an integer `n` is an error; write `if n != 0` instead.
`&&` and `||` short-circuit.

### `varint`

`varint` is a variable-length integer *storage* type for fields and array
elements, one to ten bytes long depending on the value. Reading one gives
an `i64`; it can only be written when its containing value is constructed.
[Section 8](#8-structs) covers it with the other field types.

### Conversions

Within a kind, a conversion the compiler can prove value-preserving is
implicit, and anything that could lose a bit or flip a sign needs a cast.
Between the kinds, an integer becomes a float implicitly and a float
becomes an integer only through a cast.

| Conversion | How |
|---|---|
| to a wider integer of the same signedness (`i8 → i32`, `u16 → u64`) | implicit |
| unsigned to a strictly wider signed type (`u8 → i16`, `u32 → i64`) | implicit |
| `f32 → f64` | implicit |
| any integer, `u64` included, to `f32` or `f64` | implicit, rounding to the nearest float |
| a literal or named constant into any type its value fits | implicit |
| any integer but `u64` into a `varint` store | implicit |
| narrowing, same-width signedness change, signed to unsigned, `u64` to signed, `f64 → f32`, float to integer | `x as T` or `x as! T` |

`x as T` converts between any two numeric types. It is range-checked in a
debug build, which aborts if the value changes, and truncates, wraps or
rounds toward zero silently in a release build. `x as! T` never checks,
not even in a debug build: it is for when losing bits is intended.
Conversions to a float are never checked, in either form. Float to integer
conversion is fully defined: truncate toward zero, then wrap into the
target's width; NaN gives 0.

```goose
let big = 300;
let n: u8 = big as! u8;       // 44, by design
let f = 2.75;
let i = f as i64;             // 2
let back = i as f64 / 4;      // 0.5
let neg = -1;
let u = neg as! u32;          // 4294967295
print(n, " ", i, " ", back, " ", u);
```

A cast that changes nothing is a **warning**, so the compiler points out
casts that the rules above make unnecessary: `b as i64` of a `u8` stored in
an `i64` says ``redundant `as i64`: a u8 converts to i64 implicitly here``.
A cast that decides something gives no warning, whether it decides a type
that nothing else gives (`let k = 5 as u32;`), the width of an operation
(`(a as i64) * b` with `i32` operands), or how a float is rounded.

**Comparing `u64` with signed values.** `u64` is the only unsigned type
with no signed type to widen into. A comparison between a `u64` and a signed
operand is allowed when the signed operand is *syntactically* known to be
non-negative: a non-negative literal, a `.len` or `.cap`, or a `let` bound
to one of those (and not also bound to a writable reference). The
comparison is then a single unsigned one. Any other case needs a cast.

```goose
let h: u64 = 18446744073709551615;
var xs: i64[>..] = [1, 2, 3];
let n = xs.len;
print(h > 5, " ", h > xs.len, " ", h > n);   // all allowed: 5, .len and a let of it
```

Spec §6.1, §6.3.

### Arithmetic on structs and fixed arrays

The arithmetic operators apply memberwise to two values of the same struct
or fixed-array type whose scalar leaves are all integers or all floats.
Unary `-` applies memberwise too, where no leaf is unsigned. `*` and `/`
also take one scalar operand, in either order, when every leaf has exactly
the same type; the scalar converts to that type. This is how the library's
vector types work; the language has no operator overloading.

```goose
struct V3 { x: f32, y: f32, z: f32 }
fn main() {
    let a = V3 { 1, 2, 3 };
    let b = V3 { 0.5, 0.5, 0.5 };
    var c = a + b;
    c *= 2;
    print(c, " ", -a, " ", a / 2.0, " ", 2.0 / a);
    let sum = [1, 2, 3] + [10, 20, 30];
    print(sum);
}
```

```
V3 { 3.0, 5.0, 7.0 } V3 { -1.0, -2.0, -3.0 } V3 { 0.5, 1.0, 1.5 } V3 { 2.0, 1.0, 0.6666667 }
[11, 22, 33]
```

`+`, `-` and `%` need two matching aggregates. Both operands are evaluated
once, before any member is written. Spec §6.1.

### Bitwise operators

`~ & | ^ << >>` work on integers and keep the operand's type. `>>` shifts
in zeroes on unsigned types and copies the sign bit on signed ones. The
bitwise operators bind tighter than comparisons, so `x & mask != 0` is
`(x & mask) != 0`, and shifts bind tighter than `&`. `~c` of a
non-negative constant adapts like a constant, so `let a: u8 = ~4;` is 251
and `flags & ~F_ROAD` clears a bit of a `u8`. A constant shifted by a
variable count still adapts like a constant: `1 << k` takes the type of its
destination.

```goose
var flags: u8 = 0;
let F_ROAD: u8 = 4;
flags |= F_ROAD;
flags |= 1 << 1;              // the constant 2 adapts to u8
flags &= ~F_ROAD;
let k = 40;
let m: u64 = 1 << k;          // a u64 shift, since m is one
print(flags, " ", m, " ", flags & 2 != 0);
```

There are no binary literals; write `0x` hexadecimal or a shift. The
complete precedence table is in
[Appendix A](#appendix-a-keywords-and-operators).

---

## 4. Where values live

This section is short, but everything after it rests on it.

### Size classes

Every type is in one of four **size classes**:

* **fixed**: the byte size is a compile-time constant. Scalars, structs of
  fixed fields, fixed arrays `T[k]`, static-capacity arrays `T[..k]`,
  references and slices.
* **variable**: the size is chosen when the value is constructed and never
  changes afterwards. `T[]`, `T[varint]`, `T[..]`, a struct with a `varint`
  or variable field, an enum in variable mode.
* **resizable**: the size may change after construction. `T[>..]`
  (grow-only) and `T[>..<]` (grow-shrink), and a struct whose last field is
  one of them.
* **root**: a `root struct` owns independently stored fields under one lifetime.
  It has no contiguous value layout and is owned only by a local or global
  declaration. See [Root structs](#root-structs).

An ordinary compound type's class is the largest of its parts' classes. A root
struct always retains its class, even with only fixed fields. A type is
**flat** when it holds no reference, slice or relative reference at any
depth and is not a root struct; only flat values may cross between threads
([section 22](#22-threads)). Spec §1.1.

### Data stacks

A Goose program has the native call stack, some static data (string
literals and compile-time constants), and **N data stacks**, where N is a
number the compiler computes for the program. A data stack is a large
reservation of address space with a bump pointer. This is the entire
memory system. There is no `malloc`, free list, garbage collector,
reference counting or destructor.

Fixed-size values live on the native stack or in registers. Every variable
or resizable value lives on a data stack and is owned by the variable that
declares it (a local, a global, a by-value parameter, or a field or
element of one of those). Two rules make this work:

1. Values are pushed in lifetime order and freed all at once when the
   owning scope ends. Freeing them is a single store: the stack's top is
   set back to where it was.
2. At most one resizable value is live per stack, and it is the topmost
   thing on that stack for its whole life. So growth is a pointer bump
   with no capacity check, and nothing ever moves.

Three growable arrays alive at once get three stacks; a stack is reused
as soon as a scope frees it. The compiler proves all of this statically,
and there is no syntax for a stack anywhere in the language. Spec §1.2,
§1.3, §10.3.

```goose
fn main() {
    for round in 3 {
        var scratch: u8[>..] = [];                   // a fresh buffer, each round
        for i in 100000 { scratch.push((i % 256) as! u8); }
        print("round ", round, ": ", scratch.len, " bytes");
    }                                                // freed here: one store
}
```

```
round 0: 100000 bytes
round 1: 100000 bytes
round 2: 100000 bytes
```

### Nothing moves

Because a growable array's elements never move, a reference into it stays
valid however much is pushed afterwards:

```goose
struct Item { id: i32, weight: f32 }
fn main() {
    var items: Item[>..] = [];
    let first .= items.push(Item { 1, 0.5 });         // a reference to element 0
    for i: i32 in 2..1000001 { items.push(Item { i, 0.0 }); }
    first.weight = 99.5;                              // still valid
    print(items.len, " items, first is ", items[0]);
}
```

```
1000000 items, first is Item { 1, 99.5 }
```

A `std::vector` or a `Vec` reallocates and cannot promise this. This is
why Goose code holds typed references where other languages hold integer
indices into an arena.

### Copies are visible

Fixed-size values copy silently on assignment, as in C, whatever their
size. **Non-fixed values are never copied implicitly**: such a copy takes
O(size) time, so it should be visible in the source. A variable, field or
element of variable or resizable class *binds by reference* when it
reaches a destination that is a reference, a slice, an untyped parameter,
an un-annotated `let`/`var`, or a `for` binding. At a destination that
needs a value of its own, it is an error. A value-typed destination takes
a fresh value (a literal, a call's result) or an explicit `copy(x)`.

```goose
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b = copy(a);          // an explicit, visible O(n) copy
    b.push(4);
    let c = a;                // no copy: c names the same array as a
    c.push(5);
    var p = Item { 1, 0.5 };
    var q = p;                // fixed-size: a silent copy
    q.id = 2;
    print(a, " ", b, " ", p.id, " ", q.id);
}
struct Item { id: i32, weight: f32 }
```

```
[1, 2, 3, 5] [1, 2, 3, 4] 1 2
```

The same rule applies to calls: `f(xs)` passes a growable array by
reference to a slice parameter, to a `T[>..]&` parameter and to an untyped
parameter alike. Spec §4.1.

### Built in place

A constructed value that is not fixed-size is built **directly in its final
location**, through any depth of calls and through whichever branch an `if`
or `match` takes. A function returning a growable array by value writes its
elements directly into the caller's variable, into the element being
pushed, or into the field of a record being built inside another array.
No temporary is ever built and then copied into place. Returning a built-up
value is therefore as cheap as filling an out-parameter, and
`words.push(str("word", i))` formats the text directly into the new
element. Spec §4.3, §7.3.

---

## 5. The array family

There is no single array type. Instead there is a family of array types
that differ only in what can happen to their size. All of them are
represented as `[metadata][elements…]`, inline and packed, never as a
pointer to a separate block.

| Type | Class | What it is | Stored metadata |
|---|---|---|---|
| `T[k]` | fixed | `k` elements, `k` a compile-time constant | none |
| `T[]` | variable | sized at construction, frozen after | a `u32` length |
| `T[u8]`, `T[u16]`, `T[u32]`, `T[u64]` | variable | the same, with a length of that width | a length of that type |
| `T[varint]` | variable | the same, with a variable-length length | 1 byte for short arrays |
| `T[..k]` | fixed | capacity `k` inline; grows and shrinks within it | a length, in the smallest type holding `k` |
| `T[..]` | variable | capacity chosen at construction; grows and shrinks within it | a `u32` capacity and length |
| `T[>..]` | resizable | **grow-only**: the workhorse | a length |
| `T[>..<]` | resizable | **grow-shrink**: stacks, queues, heaps | a length |

```goose
fn main() {
    var fixed: i32[4] = [1, 2, 3, 4];
    let zeros: f64[8] = [0.0; 8];           // the fill form
    let frozen: i64[] = [7, 8, 9];          // sized once, never changes
    let packed: u8[varint] = "hi";          // a 1-byte length before the bytes
    var small: u8[..8] = [];                // capacity 8, inline wherever it sits
    small.push('h'); small.push('i');
    var sized: i64[..] = [..16];            // capacity 16 chosen now, length 0
    sized.push(1);
    var grow: i64[>..] = [];                // the default growable array
    for i in 5 { grow.push(i * i); }
    var stack: i64[>..<] = [];              // the one that pops
    stack.push(1); stack.push(2); stack.push(3);
    let top = stack.pop();
    print(fixed, " ", zeros.len, " ", frozen, " ", packed, " ", small, " ",
          small.len, "/", small.cap, " ", sized.cap, " ", grow, " ", stack, " ", top);
}
```

```
[1, 2, 3, 4] 8 [7, 8, 9] hi hi 2/8 16 [0, 1, 4, 9, 16] [1, 2] 3
```

### Which one to use

* **`T[>..]`** is the default: arenas, pools, string builders, tree
  storage, scratch that is refilled between phases. "Grow-only" names the
  guarantee that references into it stay valid, not the operations it
  supports; it can still `pop` and `clear` at points where the compiler
  can tell that nothing points into it
  ([section 13](#13-growing-shrinking-and-assigning-whole-arrays)).
* **`T[>..<]`** for a stack, a queue or a heap. References into it may live
  in variables only, never in storage, so the compiler can check them at
  each shrink.
* **`T[..k]`** and **`T[..]`** when the array must sit inline inside
  something else: a 16-byte name inside a struct, a small list inside an
  array element. `T[..k]` is fixed-size, so a struct holding one is still
  fixed-size.
* **`T[]`** and **`T[varint]`** for finished data: a string in a record, a
  node in a pool, anything built once and then read.
* **`T[k]`** for a fixed shape: a 4x4 matrix, a 256-entry table.

Only `T[>..]` and `T[>..<]` are resizable, and only those are subject to the
one-per-stack rule. Everything else can go anywhere a value of its class
can.

### Compile-time sizes

The `k` of `T[k]` and `T[..k]`, and the count of a fill literal `[v; n]`,
is a constant expression: literals, named constants, arithmetic on them,
and the `.len` of a global whose length is fixed by its declaration (`const
names = ["a", "bc"]; var counts: i64[names.len];`). A type parameter is a
type, never a size: `fn f<N>(a: i64[N])` is an error, since there are no
size parameters. A local that hides a global's name is not a constant
either. Spec §3.3.

### Element restrictions

* Elements of `T[>..<]`, `T[..k]` and `T[..]` must be **fixed-size**: a
  shrink or a reuse of storage must never change what type a location
  holds.
* Elements of `T[]` and `T[>..]` may be fixed or variable, never resizable.
  An array of strings, `u8[][>..]`, is one block: a length, then each
  string's own length and bytes, back to back.
* An array with variable-size elements is **sequential**: where an element
  starts depends on every element before it, so the array can be iterated
  but not indexed, and has no `pop`. To reach its elements in any order,
  keep an index of references beside it
  ([section 11](#an-index-for-a-sequential-array)).

```goose
fn main() {
    var words: u8[][>..] = [];
    for i in 3 { words.push(str("w", i)); }
    for w in words { print(w); }         // fine
    // print(words[0]);                  // error: arrays of variable-size elements
                                         //        cannot be indexed, only iterated
}
```

```
w0
w1
w2
```

A stored length must hold the actual count: `T[u8]` holds at most 255
elements, and a construction that would exceed its length type's range is
rejected when it is known statically, or aborts when discovered at run
time. Spec §3.3.

### Placement: what may contain what

* **Fixed** values go anywhere: locals, globals, any field, elements,
  payloads, parameters, results.
* **Variable** values go in locals and globals, parameters and results,
  payloads, array elements (which makes the array sequential), and struct
  fields at any position. A field after a variable-size one is reached by
  computing its offset, which is fine while iterating and costly for
  random access.
* **Resizable** values go in locals and globals, by-value parameters,
  results, and the **last field** of an ordinary struct, which makes the struct
  resizable too. A root struct can own several, in any field position. Never
  an array element.
* **Root** owners go only in local/global declarations. A root cannot be
  owned inside another struct, an array or slice (even a fixed-size array),
  or an enum payload, or passed or returned by value. References to roots
  follow the ordinary reference rules.
* **Zero-size** values (`T[0]`, `T[..0]`, a variant with no fields) exist
  only as values of their own, never as a field or an element.

No type contains itself by value, directly or through other types. It may
refer to itself through a reference or a slice, which are fixed-size
whatever they point at: `enum List { Nil, Cons { v: i64, next: List? } }`
is fine. Spec §3.4.

### Array literals

* `[1, 2, 3]`, and `[]` where the element type is known from the
  destination: a typed variable, parameter, field, element or result.
* `[v; n]`: `n` copies of `v`, with `n` a compile-time constant; `v` is
  evaluated once.
* `[..cap]`: an empty `T[..]` with the given run-time capacity.
* A literal with no destination type is a `T[k]` (or a `T[]` when its
  elements are not fixed-size), and its elements decide `T`: `[1, 2, 3]`
  on its own is an `i64[3]`. A literal of constants adapts to a parameter
  of the shape it fits: `take([0, 0, 0, 7])` passes a `u8[4]` to
  `fn take(c: u8[4])`.
* `var out = [];` with nothing else makes `out` a grow-only `T[>..]` whose
  `T` is fixed by the first `push`, `append`, `format` or whole assignment
  into it, which must come before any other use.

```goose
fn take(c: u8[4]) -> i64 { c[3] }
fn main() {
    var out = [];                    // a T[>..]; T is decided by the first push
    out.push(2.5);                   // an f64[>..]
    let tbl: i64[4] = [7; 4];
    let names: u8[..8][3] = ["a", "bb", "ccc"];
    print(out, " ", tbl, " ", names, " ", take([0, 0, 0, 7]), " ", [1, 2, 3].len);
}
```

```
[2.5] [7, 7, 7, 7] ["a", "bb", "ccc"] 7 3
```

A struct literal and a variant literal are described in
[section 8](#8-structs) and [section 9](#9-enums-algebraic-data-types).
Spec §4.2.

### Members

These are the built-in operations. Per UFCS ([section 17](#17-functions))
`a.push(v)` is `push(a, v)`, so the standard library's functions on arrays
read the same way.

| Member | On | Meaning |
|---|---|---|
| `.len -> i64` | every array kind and slices | the element count |
| `.cap -> i64` | `T[..k]`, `T[..]` | the capacity |
| `.push(v) -> T&` | limited, grow-only, grow-shrink | one element, constructed in place; returns a reference to it |
| `.append(src)` | limited, grow-only, grow-shrink | the elements of an array, slice or array literal, copied in |
| `.move_from(src)` | slices of fixed-size elements | copies equal-length slices with overlap-safe ordering |
| `.pop() -> T` | limited, grow-shrink; grow-only where a shrink is allowed | removes and returns the last element |
| `.pop_n(n)` | the same | drops the last `n` elements |
| `.resize(n, v)`, `.resize(n)` | the same | grows with copies of `v`, or shrinks |
| `.clear()` | the same | empties the array |
| `.index_of(r) -> i64` | arrays of fixed-size elements | the index of the element `r` refers to |
| `.range_index(s, msg?) -> i64` | the same | the start of a wholly contained slice; checked when provenance does not prove it |
| `.check_length(n, msg?) -> i64` | grow-only, grow-shrink, fixed-size elements | validates a count against the data-stack reservation; returns it without changing the array |

Growth always supplies element values: there is no operation that exposes
an uninitialized slot. Exceeding a limited array's capacity aborts; so does
shrinking below empty. `index_of` needs a reference that the compiler can
prove points into that very array
([section 12](#12-lifetimes-roots-stores-and-writability)).
`range_index` accepts slices whose origin is only known at runtime: a nonempty
slice must be element-aligned and wholly within the array's current length.
An empty slice, including a default or foreign empty slice, returns zero.
`check_length` rejects negative counts and counts whose elements alone would
exceed a data-stack reservation, including oversized unsigned counts. It neither
allocates storage nor promises that an append fits the array's remaining space.
Both checks apply in every build. A string literal as a last argument,
`a.range_index(s, "msg")`, is the message a failed check aborts with instead
of the runtime's own; the standard library's pools word theirs this way.
`dst.move_from(src)` requires equal lengths and aborts before writing if they
differ. It copies the original source values even when the slices overlap;
the source is not cleared. Both operands are evaluated once, left to right.
The destination must be writable, and the usual lifetime and relative-reference
copy restrictions apply. Empty slices may be copied, including default slices.
The standard-library pool types and deprecated `reusable` operations are in
[section 14](#14-reusable-pools).
Spec §3.3, §12.

```goose
fn main() {
    var xs: i64[>..<] = [];
    xs.push(1);
    xs.append([2, 3, 4]);
    let last .= xs.push(5);
    print(xs.index_of(last), " ", xs.pop(), " ", xs);
    xs.resize(6, 0);
    xs.pop_n(2);
    print(xs, " ", xs.len);
    xs.clear();
    print(xs.len);
}
```

```
4 5 [1, 2, 3, 4]
[1, 2, 3, 4] 4
0
```

### Equality

`==` and `!=` compare arrays structurally: length first, then elements.
Two operands of *different* array kinds compare too, as slices, whenever
their slices would have the same type: a `u8[>..]` against a string literal, a
`u8[..32]` field against a `u8[:]`. Spec §4.5.

---

## 6. Strings and text

There is no string type. A string is any array-family type with `u8`
elements, and which one you pick says what you want:

* `u8[>..]`: a **builder**. Appending is a pointer bump.
* `u8[]` and `u8[varint]`: a **finished string**, stored inline in whatever
  holds it.
* `u8[..16]`: a **small string** inside a struct, with no indirection.
* `u8[:]`: a **view** of bytes that live elsewhere. A string literal is one
  of these, specifically a `const u8[:]`, since literals are read-only
  static data.

No encoding is enforced; UTF-8 is a library convention, and the library's
`each_utf8` and `push_utf8` understand it.

```goose
fn main() {
    var s: u8[>..] = [];
    s.append("hello");
    s.push(',');
    format(s, " world! ", 6 * 7);            // format appends the text of anything
    let t: u8[] = "fixed";                   // a copy of the literal, 5 bytes and a length
    let v: const u8[:] = s[..5];             // a view of s's first five bytes
    print(s, " [", s.len, " bytes] ", t, " ", v);
}
```

```
hello, world! 42 [16 bytes] fixed hello
```

A string literal is stored only in a slot declared `const`, since it is
read-only; copying it into any `u8` array (`let t: u8[] = "fixed";`) gives
a writable value of its own. `u8[..16]` and the other limited arrays are
builders too, up to their capacity: `format(p.name, "linus")`.

### Rendering text

Three builtins turn values into text, and each takes any number of
arguments of any type, inserting nothing between them:

* `print(a, b, …)` writes the text and a newline to standard output, as a
  single write, so lines from different threads never interleave.
* `format(out, a, b, …)` appends the text to `out`, any growable `u8` array.
* `str(a, b, …)` builds a fresh `u8[>..]` of the text, constructed directly
  at its destination: `words.push(str("item", i))` writes directly into
  the new element.

Every scalar has a text form: integers in decimal, floats in the shortest
form that reads back to the same value, `true` and `false`, and a `u8`
array as its bytes. Every other value renders structurally: an array as
`[1, 2, 3]`, a struct or variant as its positional literal with the type
name (`Point { 1, 2 }`, `Circle { 1.5 }`), a reference as what it points
at, and a null optional as `null`. A character literal is an integer, so
`str('x')` is `"120"`; a single byte is appended with `push`. A `u8` array
nested inside an aggregate is quoted and escaped.

A **`format` overload** `fn format(out: u8[>..]&, v: T)`, taking `T` by
value or by reference, is called to render a `T` wherever a `T` occurs in
an argument of any of the three builtins, so a type defines its text form
in one place. The overload is looked up in the namespace `T` is declared
in, then globally, so the type decides how it is printed, not the code
that prints it. A `T` that is not fixed-size is never copied implicitly,
so its overload takes it by reference.

```goose
struct Person { name: u8[..16], age: i32 }
fn format(out: u8[>..]&, p: Person) { format(out, p.name, " (", p.age, ")"); }

fn main() {
    var people: Person[>..] = [];
    people.push(Person { name: "ada", age: 36 });
    people.push(Person { name: "grace", age: 45 });
    print(people, " ", str(people[0]).len);
    print(['a', 'b'], " ", "text"[0], " ", str('x'));
}
```

```
[ada (36), grace (45)] 8
[97, 98] 116 120
```

Spec §3.7. The standard library's `std` module adds the usual
operations, all operating on `u8[:]`: `find`, `trim`, `split`, `words`,
`join`, `parse_int`, `parse_flt`, `format_int`, `compare`, `to_lower`, and
more ([stdlib.md](stdlib.md#strings)). `split` returns an array of slices into
its input: each part takes a few bytes, and no text is copied.

```goose
fn main() {
    let line = "  key = some value  ";
    let body = trim(line);
    let eq = find(body, "=");
    print("'", trim(body[..eq]), "' -> '", trim(body[eq + 1..]), "'");
    let parts = split("alice,30,paris", ',');
    print(parts.len, " parts: ", parts, " ", words("  go   north "));
}
```

```
'key' -> 'some value'
3 parts: ["alice", "30", "paris"] ["go", "north"]
```

---

## 7. Slices

`T[:]` is a reference plus an element count: a view of a contiguous run of
`T`s inside some array. It is the universal "process a range" parameter
type, since every array kind converts to a slice without copying its
elements.

* **Slicing**: `a[x..y]` is elements `x` up to `y` (exclusive); either
  bound may be left out, and `^k` means "`len - k`": `a[^2..]` is the last
  two elements. Re-slicing a slice is always allowed and never grows it.
* **Coercion**: when an array, or a reference to one, reaches a
  destination whose type is a slice of its element type, it becomes a
  slice of that whole array itself, as `a[..]` would. This holds for a
  parameter, an annotated variable, an assignment, a field, a pushed
  element, a `return`, and a branch of an `if` whose other branch is a
  slice.
* Slices of fixed-size elements index and iterate, bounds-checked against
  the slice's own length. Slices of variable-size elements iterate only.
* A slice of a grow-only array taken before a `push` stays valid, but does
  not include the new elements.

```goose
fn total(xs: i64[:]) -> i64 { var t = 0; for x in xs { t += x; } return t; }

fn main() {
    let fixed: i64[4] = [1, 2, 3, 4];
    var grow: i64[>..] = [10, 20, 30, 40, 50];
    let view: const i64[:] = fixed;       // fixed itself, not a copy
    print(total(fixed), " ", total(grow), " ", total(grow[1..4]), " ",
          total(grow[^2..]), " ", view[0], " ", grow[..2].len);
    let s = grow[1..3];
    grow.push(60);                        // s still views elements 1 and 2
    print(s, " ", s == [20, 30]);
}
```

```
10 150 90 90 1 2
[20, 30] true
```

A slice is **checked**. It carries a static root, the variable that bounds
the lifetime of what it views. A slice can neither outlive that variable
nor survive a shrink that would free the elements. The checker tracks
liveness, not scope: a slice whose last use is before a `clear` is dead,
and the `clear` is allowed.
[Section 12](#12-lifetimes-roots-stores-and-writability) has the rules,
and [section 13](#13-growing-shrinking-and-assigning-whole-arrays)
the shrink side.

```goose
fn main() {
    var line: u8[>..] = [];
    line.append("hello world");
    let w = line[..5];
    print(w);
    line.clear();                         // fine: w's last use is above
    line.append("again");
    let v = line[..];
    // line.clear();                      // error: cannot clear while v is still used
    print(v);
}
```

```
hello
again
```

A slice is a reference, so it follows the same writability rule. A slice
of a `var` array is writable through, while a string literal and a slice
of a `const` value are not. A parameter declared `T[:]` accepts either, and
is checked against the argument it is given. A function that only reads
through a slice parameter documents that by declaring the parameter
`const T[:]`. Spec §3.10, §9.5.

Because a slice never copies, it is the natural key for a table. The
library's dictionary keyed by `const u8[:]` stores views into text the
program already holds:

```goose
import dictionary;

fn main() {
    let text = "the quick brown fox jumps over the lazy dog the fox";
    var counts = dictionary<const u8[:], i32> {};
    each_split(text, ' ') { counts.update(it, 0) { it += 1; }; };
    let n = counts.get("the");
    if n { print(counts.count, " distinct words; 'the' x", n); }
}
```

```
8 distinct words; 'the' x3
```

---

## 8. Structs

```goose
struct Unit { kind: i32, hp: i32 = 100, x: f32, y: f32, name: u8[] }
```

An ordinary struct is its fields in declaration order, **packed**: no padding is
inserted, unaligned access is assumed cheap, and a struct of an `i8[3]` and
an `i32` is 7 bytes. Struct declarations introduce nominal types; two
structs with the same fields are different types.

### Literals

A literal names its fields, or lists them positionally in declaration
order; the two forms do not mix. Fields with a declared default may be left
out: trailing ones in the positional form, any of them in the named form.
An optional field with no default is null when left out. A literal that
ends in `..` may leave out *any* field, which then takes its type's default
value: zero, `false`, null, an empty array or string. A field whose type
has no default value (a non-optional reference) must still be given.

```goose
struct Unit { kind: i32, hp: i32 = 100, x: f32, y: f32, name: u8[] }
var selected: Unit;                                             // a global with a type only:
                                                                // every field its default
fn main() {
    let a = Unit { kind: 1, x: 1.5, y: 2.5, name: "scout" };   // hp 100
    let b = Unit { 2, 50, 0, 0, "" };                           // positional, all given
    let c = Unit { kind: 3, .. };                               // hp 100, x 0, y 0, name ""
    print(a, " ", b.hp, " ", c, " ", selected.hp);
}
```

```
Unit { 1, 100, 1.5, 2.5, "scout" } 50 Unit { 3, 100, 0.0, 0.0, "" } 100
```

A global `var` given only a type starts as its type's default value. A
local must be initialized at its declaration when its type is not
fixed-size, since the value's storage is laid out when it is constructed.
A fixed-size local may be declared first and assigned on every path before
it is read ([section 2](#2-declarations-variables-and-scope)).

Named initializers may be written out of declaration order only when the
compiler can tell that putting them back in order changes nothing: reads
and arithmetic that cannot trap are fine; calls with effects, indexing and
loops are not. Construction always proceeds in declaration order, directly
into each field. If effects must happen in another order, do them before
the literal. Spec §4.2.

### Field defaults

A default is an expression that is evaluated at each construction that
leaves the field out. It is evaluated in field order, among the explicit
initializers. It may name globals and call functions, but it can never
refer to the constructor's locals or to sibling fields.
`default<T>()` ([Builtins](#25-builtins)) builds a fixed-size `T` from its
declared defaults, so an invariant that a declaration encodes as a default
is kept even in generic code that fills in values.

```goose
var serial = 0;
fn next() -> i64 { serial += 1; serial }
struct Rec { id: i64 = next(), tag: u8[..4] = "none", score: f64 = 1.0 }

fn main() {
    let a = Rec { .. };
    let b = Rec { score: 2.0 };
    let c = default<Rec>();
    print(a, " ", b, " ", c.id);
}
```

```
Rec { 1, "none", 1.0 } Rec { 2, "none", 2.0 } 3
```

A default may not lead back to a construction that uses the same default
again, since that would never end. Spec §3.2.

### Field qualifiers and padding

* `let f: T` marks a field that is not assigned after construction.
* `const f: T` is `let f: const T`: its contents are read-only too.
* `pad n` inserts `n` unused bytes; a bare `pad` aligns the *next* field to
  its scalar width, for foreign layouts. Pad bytes are never read and
  `==` ignores them.

```goose
struct Hdr { magic: u8[4], pad, size: u32, let kind: u8, const name: u8[..8] }

fn main() {
    var h = Hdr { magic: "GOOS", size: 4, kind: 1, name: "x" };
    h.size = 8;
    // h.kind = 2;                    // error: kind is a let field
    // h.name.push('y');              // error: name is const
    print(h, " ", h.name.cap);
}
```

```
Hdr { "GOOS", 8, 1, "x" } 8
```

Spec §3.2, §4.4.

### Variable-size fields and `varint`

A struct may hold variable-size parts, and they sit inline, in declaration
order. `varint` is a LEB128 integer field that takes as many bytes as its
value needs (one byte up to 63, two up to 8191, and so on; small negatives
are as short as small positives). A struct with a `varint` or a `u8[]`
field is variable-class, and its fields after such a field are found by
walking. A `varint` field is written only at construction; reads give an
`i64`.

```goose
struct Item  { sku: u8[varint], qty: varint, cents: varint }
struct Order { id: varint, customer: u8[varint], items: Item[varint] }

fn main() {
    var book: Order[>..] = [];
    book.push(Order { id: 1001, customer: "alice",
                      items: [Item { sku: "SKU-441", qty: 2, cents: 1999 },
                              Item { sku: "SKU-7", qty: 1, cents: 500 }] });
    for o in book {
        var t = 0;
        for it in o.items { t += it.qty * it.cents; }
        print(o.customer, " owes ", t, " cents over ", o.items.len, " items, ",
              bytes_of(book).len, " bytes in all");
    }
}
```

```
alice owes 4498 cents over 2 items, 29 bytes in all
```

That order is 29 bytes with no pointer in it. The downside is that `book`
is sequential: iterate it, or keep an index beside it. A variable-class field
or element is frozen once built: to change one, rebuild its container.
Spec §3.4, §3.6.

### A resizable tail

An ordinary struct's **last** field may be a resizable array, which makes the struct
resizable: it can live wherever a growable array can (a local, a global, a
by-value parameter, a result), but never as an element or a middle field. The
tail grows through the struct like any growable array.

```goose
struct Stack { name: u8[..8], items: i64[>..<] }

fn push_twice(s: Stack&, v: i64) { s.items.push(v); s.items.push(v); }

fn main() {
    var s = Stack { name: "main", items: [] };
    push_twice(s, 7);
    s.items.pop();
    print(s.name, " ", s.items, " ", s.items.len);
}
```

```
main [7] 1
```

Spec §3.4, Appendix C.2.

### Root structs

Like an ordinary struct, a `root struct` bundles related state behind named
fields. It lifts the requirement that those fields form one contiguous value:
each field has independent storage, so several fields can grow separately.
Fields can be fixed, variable or resizable, in any order, and all share the
lifetime of the owning variable.

The tradeoff is placement: an owning root struct must be a local or global
variable, rather than a field or array element. It can group application state,
a workspace, or a library instance:

```goose
root struct Workspace<T> {
    items: T[>..] = [],
    scratch: i64[>..<] = [],
    calls: i64 = 0
}

fn add<T>(s: Workspace<T>&, value: T) -> T& {
    s.calls += 1;
    let result .= s.items.push(value);
    s.scratch.push(s.calls);
    s.scratch.clear();                // does not invalidate result
    return result;
}

fn main() {
    var state = Workspace<i64> { .. };
    let first .= state.add(7);
    state.add(8);
    print(first, " ", state.items, " ", state.calls);
}
```

```
7 [7, 8] 2
```

Construct an owner with a literal directly in a local or global declaration.
Field defaults and `..` work normally; a typed global may omit its initializer.
The compiler determines all component stacks statically and restores them on
scope exit. Existing recursive-cycle restrictions apply to owners with nonfixed
fields: declare such state outside the cycle and pass a reference into it.

Functions receive a root by reference. An unannotated binding from an existing
owner also borrows it; `.=` is available to make that intent explicit. Owner
references can be optional, stored in ordinary structs or arrays, returned, and
captured by nested functions, subject to normal lifetime and writability rules.
Field access, UFCS, reflection and formatting work through those references.

A mutable grow-only array field of a global `var` root can be named in compact
reference and index types: `Node&<u32 in pool.items>`, `u32 in pool.items`, and
`u32 upto pool.items`. The name resolves to that particular field's storage;
another field or owner is a different array. Only direct fields are supported:
the path cannot follow an alias, reference, index or nested field.

The owner itself cannot be copied, reassigned, passed or returned by value, used
as a temporary, or placed inside another struct or array. This applies even to a
root with no resizable fields. `root` only acts as a modifier before `struct`;
variables, fields and functions may still be named `root`.

A root has no packed byte image, whole-value equality or arithmetic, or public
C layout. `pad` belongs in an ordinary component struct, not on the root.
Serialize eligible fields separately, for example `to_bytes(state.items)`;
that image contains only the selected component. Workers can construct local
roots but cannot receive owners or owner references, or use a root global
directly or through another function. A root global unused by a worker is fine.
Nested owning roots and fixed arrays of owners are deferred. Spec §3.4,
Appendix C.2.

### Generic structs

```goose
struct Pair<T> { a: T, b: T }
struct Node<T> { v: T, next: Node<T>? }

fn main() {
    let p = Pair<i64> { 1, 2 };
    let q = Pair { a: 0.5, b: 1.5 };           // T inferred from the fields
    var n = Node<u8> { v: 1, next: null };
    print(p, " ", q, " ", n.next == null);
}
```

```
Pair<i64> { 1, 2 } Pair<f64> { 0.5, 1.5 } true
```

A generic struct is monomorphized like a function, and its instantiations
must be finite: a field naming the struct again with a *larger* type
argument each round (`struct P<T> { v: T, next: P<T[]>? }`) is rejected.
Spec §3.2.

### Equality and arithmetic

`==` and `!=` compare ordinary structs memberwise, excluding pads; a reference or
slice *field* compares by identity (address and length), since recursing
through it could loop. The arithmetic operators apply memberwise
([section 3](#arithmetic-on-structs-and-fixed-arrays)). Ordering operators
exist on numbers only. Spec §4.5.

---

## 9. Enums (algebraic data types)

```goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }
```

An enum is a tag followed by the payload of one of its variants. It is the
only form of dynamic polymorphism in the language; there is no
inheritance, no traits and no vtables. A variant's payload is declared like
a struct's fields, with the same defaults and `..` rules, and a variant
with no payload is a plain constant. An enum whose variants all have no
payload is a C-like enum, one byte. A variant literal is
`Shape.Circle { r: 1.0 }`, or `Shape.Dot` for a variant with no payload.

### Two modes

Every enum type can be stored two ways, chosen at the point of use by the
type written there:

**Fixed mode** (`Shape`) is the layout of a Rust `enum` or a C++ `variant`:
a tag plus room for the largest payload, so every `Shape` is 17 bytes,
including `Dot`. It is fixed-size, so an array of them is indexable, and a
value may be overwritten in place with a different variant. In exchange,
nothing may point *into* its payload: `match` binds the payload by value
only. It is usable only when every payload is fixed-size.

**Variable mode** (`Shape..`, with the trailing dots) gives each value
exactly its variant's size: 1 byte for a `Dot`, 9 for a `Circle`, 17 for a
`Rect`. An array of them is sequential, and a value can never change
variant, since its neighbours are packed right behind it. In exchange you
may take references into a payload and edit it in place. A variant with a
variable-size payload (a string, say) can only ever be used in this mode.

```goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

fn main() {
    var shapes: Shape[>..] = [];                 // fixed mode: indexable
    shapes.push(Shape.Circle { r: 1.0 });
    shapes.push(Shape.Dot);
    shapes[1] = Shape.Rect { w: 1.0, h: 1.0 };   // a different variant, in place
    print(shapes);

    var packed: Shape..[>..] = [];               // variable mode: packed
    packed.push(Shape.Circle { r: 1.0 });
    packed.push(Shape.Rect { w: 2.0, h: 3.0 });
    packed.push(Shape.Dot);
    for s in packed {
        match s {
            Rect &r => { r.w += 1.0; },          // edits the payload inside the array
            _ => {},
        }
    }
    print(packed, " ", bytes_of(packed).len, " bytes");
}
```

```
[Circle { 1.0 }, Rect { 1.0, 1.0 }]
[Circle { 1.0 }, Rect { 3.0, 3.0 }, Shape.Dot] 27 bytes
```

Allowing either variant replacement or interior references, but never
both, is what keeps the type system sound. Spec §3.5.

### `match`

```goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

fn name(s: Shape) -> u8[:] {
    match s {
        Circle c => if c.r > 1.0 { "big circle" } else { "circle" },
        Rect r => if r.w == r.h { "square" } else { "rectangle" },
        Dot => "dot",
    }
}

fn round(s: Shape) -> bool {
    match s { Circle, Dot => true, Rect => false }
}

fn main() {
    print(name(Shape.Circle { r: 2.0 }), " ", name(Shape.Rect { w: 1.0, h: 1.0 }), " ",
          round(Shape.Dot));
}
```

```
big circle square true
```

* A match over an enum is exhaustive: every variant has an arm, or `_`
  covers the rest. No variant is named twice.
* `Circle c =>` binds the payload **by value**, as a copy that cannot be
  written. `Circle &c =>` binds it **by reference**. This is legal only for
  a variable-mode scrutinee, and the variant can never be replaced through
  such a reference.
* An arm may list several variants, `Circle, Dot =>`, and then binds
  nothing, since their payloads have different types.
* `match` is an expression; its arms must agree on a type, with the usual
  adaptation of literals ([section 16](#16-expressions-and-control-flow)).
  An arm of several statements is a block, `Rect &r => { … },`.

**Over integers**, the arms are literals, named constants, half-open
ranges, or a comma-separated list of those, and `_` is required. The first
arm that matches is taken; an arm that an earlier arm covers entirely is an
error. A name is a variant when the scrutinee is an enum and a constant
when it is an integer.

```goose
let GROUND = 0;
let SHALLOW = 1;
let DEEP = 4;
let LAVA = 7;

fn terrain(t: i64) -> u8[:] {
    match t {
        GROUND => "ground",
        SHALLOW..DEEP => "water",
        LAVA, 10..12, -3 => "hot",
        _ => "rock",
    }
}

fn main() {
    for t in [0, 2, 7, 11, -3, 20] { print(t, " ", terrain(t)); }
}
```

```
0 ground
2 water
7 hot
11 hot
-3 hot
20 rock
```

Spec §8.1.

### Case functions

The arms of a `match` may instead be written as separate functions, one
per variant type, and called with the enum value. The call dispatches on
the tag through a jump table, and exhaustiveness is checked exactly as for
a `match`: every variant needs exactly one applicable overload, and the
result types must agree. This gives the effect of a virtual call without a
vtable, and adding a variant is a compile error until it has its own case.

```goose
enum Shape { Circle { r: f64 }, Rect { w: f64, h: f64 }, Dot }

fn area(s: Shape.Circle) -> f64 { 3.14159 * s.r * s.r }
fn area(s: Shape.Rect) -> f64   { s.w * s.h }
fn area(s: Shape.Dot) -> f64    { 0.0 }

fn main() {
    var shapes: Shape[>..] = [];
    shapes.push(Shape.Circle { r: 1.0 });
    shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
    shapes.push(Shape.Dot);
    var total = 0.0;
    for s in shapes { total += area(s); }
    let c = Shape.Circle { r: 2.0 };          // a variant type is an ordinary type
    print(total, " ", area(c));
}
```

```
9.14159 12.56636
```

Variant types (`Shape.Circle`) are ordinary types: they can be the type of
a value, a parameter, a field, or a reference `Shape.Circle&`. An overload
taking `Shape.Rect&` binds the payload by reference and needs a
variable-mode scrutinee, as a `&`-binder does. Dispatch is on one
parameter position; the other arguments are passed through, and every
argument is evaluated once before the tag selects the case. Tag dispatch is
tried only when no ordinary overload matches the call. Spec §8.2.

### Enums with references

An enum may refer to its own type through a reference, which is what makes
linked and tree-shaped data in a pool possible. Combined with variable mode
and the narrow links of [section 11](#11-relative-references), a tree of
mostly small nodes no longer needs room for its largest variant in every
node:

```goose
enum Expr {
    Num { v: f64 },
    Add { l: Expr..&<u16>, r: Expr..&<u16> },
}

fn eval(e: Expr.Num&) -> f64 { e.v }
recursive fn eval(e: Expr.Add&) -> f64 { eval(e.l) + eval(e.r) }

fn main() {
    var pool: Expr..[>..] = [];
    let a = pool.push(Expr.Num { v: 1.5 });
    let b = pool.push(Expr.Num { v: 2.0 });
    let sum = pool.push(Expr.Add { l: a, r: b });
    print(eval(sum), " in ", bytes_of(pool).len, " bytes");
}
```

```
3.5 in 23 bytes
```

Each `Num` is 9 bytes and the `Add` is 5: a tag and two 2-byte offsets.
The `recursive` keyword is explained in [section 19](#19-recursion).

---

## 10. References and optionals

`T&` is a reference to a `T`: one machine address, with no pointer
arithmetic, no casts, and no way to make it dangle. Spec §3.8.

### References are transparent

An expression that denotes a reference behaves as its target in every
value context: reads, arithmetic, comparisons (`r1 == r2` compares the
targets), passing to a by-value parameter, `print(r)`. There is no
dereference operator and no `->`. A write through a reference, `r = v` or
`r += v`, writes the target.

Since the reference itself is invisible in ordinary expressions, there is
a separate syntax for "the reference, not the target":

* `let r .= e;` and `var r .= e;` declare `r` as a reference to `e`,
  whatever `e` is: a variable, a field, an element, or a call that returns
  a reference.
* `r .= e;` rebinds an existing reference variable, field or element to
  point at `e`.
* `r1 .== r2` and `r1 .!= r2` compare two references by address.

```goose
fn main() {
    var xs: i64[>..] = [10, 20, 30];
    var r .= xs[0];              // r refers to element 0
    var c = xs[0];               // c is a copy of element 0
    r = 11;                      // writes xs[0]
    c = 99;                      // writes the copy
    r .= xs[2];                  // r now refers to element 2
    r += 1;
    print(xs, " ", c, " ", r, " ", r .== xs[2], " ", r == xs[2]);
}
```

```
[11, 20, 31] 99 31 true true
```

### Making and binding references

`&x` makes a reference to an lvalue: a variable, a field, an element, or
what another reference points at. `&` of a temporary is an error. `&` of
a location that holds a reference yields the stored reference: there are
no references to references.

Where the destination's declared type is a reference, an lvalue of the
pointee type binds **without** `&`, and writing the `&` is a warning:

```goose
fn inc(n: i64&) { n += 1; }

struct Cursor { line: i64& }

fn main() {
    var count = 0;
    inc(count);                  // binds count by reference
    inc(count);
    let r: i64& = count;         // an annotated reference variable
    r += 10;
    let cur = Cursor { line: count };
    cur.line += 100;
    print(count);
}
```

```
112
```

Where a variable's type is inferred, a reference to a **fixed-size** value
decays to a copy of the target: `let x = r;` copies. The exceptions are an
explicit `&lvalue` initializer, which infers the reference type, and the
`.=` form. A **non-fixed** lvalue binds by reference whatever the form,
since non-fixed values never copy implicitly ([section 4](#copies-are-visible)):

```goose
fn main() {
    var n = 1;
    var xs: i64[>..] = [1];
    let a = &n;                  // a reference: explicit &
    let b .= n;                  // a reference: the .= form
    let c = xs;                  // a reference: xs is not fixed-size
    var r .= n;
    let d = r;                   // a copy of n: r decays
    a += 1; b += 1; c.push(2);
    print(n, " ", d, " ", xs);
}
```

```
3 1 [1, 2]
```

A function whose result type is written `T&` returns a reference. A
function whose result type is inferred returns a reference where it returns
a non-fixed lvalue or an explicit `&x`, and a copy where it returns a
reference to a fixed-size value. To bind a reference-returning call to a
fixed-size element, use `.=` or annotate: `let e .= pool.push(v);`.

A `let` reference variable cannot be rebound, but that says nothing about
the target, which is as writable as it was: `let r .= xs[i]; r = 5;` writes
the element.

### Optionals

References are never null. `T?` is an **optional `T`**: a nullable
reference, represented as a reference whose null is address 0, at no space
cost. It composes with any type (`i64?` is an optional integer, by
reference), and applied to a reference type it only makes that reference
nullable: `T&?` is `T?`. The literal `null` is its empty value, and an
optional struct field with no declared default is null when left out.

An optional is **not transparent**. It must first be *narrowed* by `if`,
`while`, `guard`, `assert`, or a comparison with `null`. Inside the
narrowed region it *is* a `T&`: transparent, writable through, and
bindable with `.=`. Rebinding the variable ends the narrowing, except in a
`while` whose condition is the test, which runs again before each
iteration.

```goose
fn first_at_least(xs: i64[:], min: i64) -> i64? {
    for &x in xs { if x >= min { return x; } }
    return null;
}

struct Node { v: i64, next: Node? }

fn main() {
    let xs: i64[4] = [1, 5, 8, 2];
    let big = first_at_least(xs, 5);
    if big { print("first >= 5 is ", big); } else { print("none"); }
    let none = first_at_least(xs, 100);
    print(none == null);

    var c = Node { v: 3, next: null };
    var b = Node { v: 2, next: c };
    var a = Node { v: 1, next: b };
    var cur: Node? = a;
    var sum = 0;
    while cur {                      // narrowed for the body, re-tested each time
        sum += cur.v;
        cur .= cur.next;
    }
    print(sum);
}
```

```
first >= 5 is 5
true
6
```

An optional rebinds with `.=` to a reference, which narrows it, or to
`null`, which does not. A narrowed optional writes through like any
reference; there is no way to write through one *without* narrowing it.
`assert(o)` narrows for the rest of the block. Spec §3.8.

### What references may point to

A reference may point to anything except a fixed-mode enum's payload ([section
9](#two-modes)). A reference into a grow-shrink array `T[>..<]` has a further
restriction: it may be held only in a variable and must no longer be used when
the array next shrinks ([section
13](#13-growing-shrinking-and-assigning-whole-arrays)). References into
grow-only arrays stay valid for as long as they can be named. References into
limited arrays stay valid across pops and pushes, and read whatever value their
slot currently holds.

---

## 11. Relative references

A `T&` is eight bytes, and an address means nothing outside the process
that made it. A **relative reference** is the same link stored as a narrow
offset: `T&<u8>`, `T&<u16>`, `T&<u32>`, `T&<u64>` or `T&<varint>`. Loading
one yields an ordinary `T&` and storing one re-encodes it; otherwise it is
used like any other reference. There are two forms, differing in what the
offset is measured from:

* **Self-relative**, the default, `T&<u32>`: the offset from the field
  itself to the target, which must live in the same enclosing array. The
  structure is position-independent, so it can be copied, saved and loaded
  as a whole ([section 23](#23-serialization)).
* **Pool-relative**, `T&<u32 in pool>`: the offset from the base of the
  named global array `pool`. A store is a subtraction from a base already
  in a register, and *other* arrays can hold links into the pool.

`T&<u32>?` is the optional form, with offset 0 as null, so a link costs
four bytes including its null. Spec §3.9.

### A tree in one array

```goose
struct Node { key: i32, count: i32 = 1, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];
    loop {
        if key == cur.key { cur.count += 1; return; }
        let next = if key < cur.key { cur.left } else { cur.right };
        if next { cur .= next; continue; }
        if key < cur.key { cur.left .= pool.push(Node { key: key }); }
        else { cur.right .= pool.push(Node { key: key }); }
        return;
    }
}

recursive fn in_order(n: Node&, out: i32[>..]&) {
    let l = n.left;
    if l { in_order(l, out); }
    out.push(n.key);
    let r = n.right;
    if r { in_order(r, out); }
}

fn main() {
    var pool: Node[>..] = [];
    for k in [50, 30, 70, 20, 40, 60, 80, 30] { insert(pool, k as i32); }
    var keys: i32[>..] = [];
    in_order(pool[0], keys);
    print(keys, " ", pool.len, " nodes, ", bytes_of(pool).len, " bytes, ", pool[1].count);
}
```

```
[20, 30, 40, 50, 60, 70, 80] 7 nodes, 112 bytes, 2
```

A node is 16 bytes: two `i32`s and two 4-byte links. `cur.left .=
pool.push(…)` pushes the node and stores the offset, and the push cannot
invalidate `cur`, because nothing moves. Storing a self-relative link
requires the compiler to see that the reference and the destination derive
from the **same root array, exactly**: here both are `pool`. The offset is
range-checked at the store only where the array could span more than the
width holds.

### Copying values that hold self-relative links

A self-relative offset is only meaningful where it was stored, so a value
that *contains* one cannot be copied in any way: by assignment from a
variable, as a by-value argument or match binder, as an element copy, or
by `copy(x)`. Such values are constructed in place (a literal pushed into
the array), bound by reference (`let n .= f();` rather than `let n =
f();`, as the error suggests), matched with `&`-binders, and taken by
reference in case functions and `format` overloads. A value whose links
are all `in pool` copies like any other, since its offsets do not depend
on where it sits. The only copy that is always allowed is of a default
value: a literal's `..` sets a relative field to null. Spec §3.9.

A function or trailing block can return a fresh value with relative links.
It is constructed at the receiver, including across function calls and
with optimization disabled:

```goose
struct Node { value: i64, next: Node&<u32>? }
fn linked(first: Node&, value: i64) -> Node {
    Node { value: value, next: first }
}
fn add<F>(nodes: Node[>..]&) { nodes.push(F()); }
fn main() {
    var nodes: Node[>..] = [Node { value: 1 }];
    nodes.push(linked(nodes[0], 2));
    let first .= nodes[0];
    add(nodes) { Node { value: 3, next: first } };
    let next = nodes[2].next;
    if next { print(next.value); }  // 1
}
```

Each call must place the result in the same root as the linked element.
For example, `let n = linked(nodes[0], 2)` is an error: the new local is
separate storage. Returning an existing node by value is still a prohibited
copy. A result that needs its receiver's root also cannot be used as a
resize fill or converted through a temporary array representation.

### Pool-relative links and `self`

`pool` in `T&<u32 in pool>` names a global `var` array (including deprecated
`reusable var`) or a mutable array field of a global `var` root, such as
`T&<u32 in pool.items>`. The array must be grow-only and its elements hold a `T`;
the pool is part of the type's
identity. The stored value is the offset from the pool's base plus one,
so 0 is null in the optional form and a `u32` link spans a pool of 4 GB.
A load is rooted at `pool`, exactly, wherever the link was read from. This
lets a container of links stay usable where a container of plain
references would be ambiguous.

A non-optional relative reference has no null, so a value whose links
should point at itself could not be built: there is nothing yet to point
at. **`self`**, written as the whole initializer of such a field, denotes
the value the literal is constructing. This is how a circular list gets a
sentinel. After that, no link is ever null, and every insert and remove is
the same four stores:

```goose
import slot_pool;

struct Node { val: i64, prev: Node&<u32 in pool.items>, next: Node&<u32 in pool.items> }

var pool = slot_pool<Node> {};
let head .= pool.alloc_with() { Node { val: 0, prev: self, next: self } };   // the sentinel

fn insert_after(p: Node&, v: i64) -> Node& {
    let n .= pool.alloc_ref(Node { val: v, prev: p, next: p.next });
    n.next.prev .= n;
    p.next .= n;
    return n;
}

fn remove(n: Node&) {
    n.prev.next .= n.next;
    n.next.prev .= n.prev;
    pool.free(pool.items.index_of(n));
}

fn is_end(n: Node&) -> bool { n .== head }

fn main() {
    insert_after(head, 1);
    let two .= insert_after(head.prev, 2);   // .=: `let two = …` would copy the node
    insert_after(head.prev, 3);
    remove(two);
    var line: u8[>..] = [];
    var n .= head.next;
    while !is_end(n) { format(line, n.val, " "); n .= n.next; }
    print(line, "| ", pool.items.len, " slots, ", 8 + 4 + 4, " bytes a node");
}
```

```
1 3 | 4 slots, 16 bytes a node
```

Note the `.=` on `two`: a `Node` is fixed-size, so `let two =
insert_after(…)` would copy the node out of the pool, and `remove(two)`
would then be asked to free the slot of a local copy, which the compiler
rejects.

`self` is legal only in a struct or variant literal, and only for a
non-optional relative-reference field whose pointee is the type being
constructed, and for an `in pool` field only in a literal built inside
that array (a `push`, an element store, or a `slot_pool.alloc_with` builder).
`alloc_with` selects the destination slot before calling the builder, so
`self` points at that slot. `alloc_ref` accepts an already-constructed value;
use it when no destination-dependent construction is needed. Both operations
are explained in [section 14](#14-reusable-pools).

### Which form

* **Self-relative** for position-independent blobs, meaning a compact tree
  that is saved, mapped or moved whole, and for structures living in a
  local or parameter pool, which has no name to write in a type.
* **`in pool`** for relink-heavy structures in a global pool, where a
  store is a subtraction from a register; for links *into* the pool from
  other arrays, which self-relative cannot express; and where the offset
  is wanted as an index, since `&pool[i]` encodes as `i * sizeof(T) + 1`
  and `index_of` reads it back.

Whether a parameter points into a pool is settled at each call site, so a
function that relinks is specialized per pool and needs no extra argument
for the pool. A function's result cannot be declared relative: declare
`T&` or `T?`, and a relative slot that receives it encodes it.

### An index for a sequential array

An array of variable-size elements cannot be indexed
([section 5](#element-restrictions)), but a second array of 4-byte links
into it can be:

```goose
enum Mark { Circle { r: f64 }, Label { text: u8[varint] }, Dot }

var marks: Mark..[>..] = [];
var mark_at: (Mark..&<u32 in marks>)[>..] = [];

fn main() {
    mark_at.push(marks.push(Mark.Label { text: "hello, goose" }));
    mark_at.push(marks.push(Mark.Circle { r: 1.5 }));
    mark_at.push(marks.push(Mark.Dot));
    for i in [2, 0, 1] {
        match mark_at[i] {
            Circle c => print(i, ": circle ", c.r),
            Label l => print(i, ": label \"", l.text, "\""),
            Dot => print(i, ": dot"),
        }
    }
    print(bytes_of(marks).len, " bytes of marks, ", mark_at.len, " links of 4 bytes");
}
```

```
2: dot
0: label "hello, goose"
1: circle 1.5
24 bytes of marks, 3 links of 4 bytes
```

`push` returns a reference to the element it built. When that reference
is stored into `mark_at`, it becomes the mark's offset from the base of
`marks`. The array an `in` offset is measured from must be a global; a
local array can be indexed the same way with an array of plain references,
`(Mark..&)[>..]`, at eight bytes per entry.

---

## 12. Lifetimes: roots, stores and writability

This is the whole of Goose's "borrow checker":

> A reference or slice must not outlive the variable that owns its target,
> and must never observe its target at a wrong type.

There are no lifetime annotations anywhere in the language, and no
aliasing or exclusivity rules: two references to the same thing are fine,
because without shared-memory concurrency aliasing alone cannot break type
safety here. Spec §9.1.

### Roots

Every reference and slice has one or more static **roots**. A root is a
local or global variable, or a temporary, that bounds the lifetime of the
reference's target. A root is **exact** when that variable's own storage
contains the target, and inexact when it only bounds the target's
lifetime. Roots are inferred, never written: `&x` is rooted at `x`
exactly, a slice of a function's parameter is rooted at whatever the
caller passed, and a reference read out of a container is rooted at the
variables whose storage could hold its target. Functions are specialized
per distinct root, which gives the precision of lifetime parameters
without any syntax for them. Spec §9.2.

### The rules

* **Store.** A reference may be stored into a location (a variable, a
  field, an element, a global) only if its root outlives the location's
  owner. A declaration stores its value into the variable it declares.
* **Return.** A returned reference's root must be visible to the caller:
  a caller-supplied argument, a global, or the function's own in-place
  constructed result. A part of the function's own local dies with it.
* **Branches.** The value of an `if`, `match`, `block` or loop is received
  after the scopes its branches opened have ended, so it may not be rooted
  at a variable declared inside a branch.
* **Merged values.** A value that may come from any of several places
  (the branches of an `if`, the bindings of a `var` reference, the returns
  of a function) has the roots of all of them, and must satisfy each rule
  for every one of those roots.
* **Rebinding.** A reference variable commits to its first binding's
  root: `.=` may rebind it within the same root or to a root at the same
  scope depth, which is the common case of walking a container. Anything
  else needs a new variable.
* **Temporaries.** A literal, a call's result, a `copy`, or the value of a
  control construct lasts for the rest of its statement. A view into one
  may be passed down, but not stored in a variable that outlives the
  statement, nor returned: `let s = f()[..];` is an error, and `let t =
  f(); let s = t[..];` is fine.
* **Holders.** A holder (a struct with reference fields, an array of
  slices, a payload with a reference) is rooted at what was stored into
  it. The checker records every such store, so it also catches a shrink or
  a scope exit that would strand a reference inside a holder.

A lifetime error names both ends: the reference's root and its
destination.

```goose
fn longest(a: u8[:], b: u8[:]) -> u8[:] { if a.len >= b.len { a } else { b } }

fn main() {
    var outer: u8[>..] = [];
    outer.append("outer");
    var w = outer[..];
    {
        var inner: u8[>..] = [];
        inner.append("inner text");
        w = longest(outer, inner);   // error: storing a reference rooted at inner
    }
    print(w);
}
```

The result of `longest` may point into either argument, so it is rooted
at both, and `inner` does not outlive `w`. Move the declaration of `w`
inside the block, or copy the text out, and it compiles.

```goose
fn make() -> u8[>..] { str("made") }

fn main() {
    let t = make();
    let s = t[..];                   // fine: t lasts
    let u = make()[..];              // error: a view of a temporary
    print(s, " ", u);
}
```

### Writability

Everything is writable unless its type says otherwise. `const T` is a
value whose contents cannot be written: a `const i64[3]` cannot have an
element assigned, and `const T&` and `const T[:]` are a reference and a
slice through which the target cannot be written, grown or shrunk.
Writability is *inferred* per instantiation, like roots:

* **Read-only**: a string literal; `&x` or `x[..]` of a `const` value, of
  a field of one, or of a by-value `for` or `match` binding; a view into a
  temporary; a `bytes_of` view; whatever is read out of a slot declared
  `const`.
* **Writable**: everything else, including `&x` and `x[..]` of a `var` or
  a plain `let`.
* **Parameters and results are generic over constness.** A parameter
  declared `u8[:]` takes a writable or a read-only argument, and the body
  is checked with what it is given: a write through a parameter given a
  literal is an error at that call, reported with the call chain. A
  parameter declared `const u8[:]` is read-only whatever it is given, which
  is how a function documents that it only reads. Adding `const` is
  implicit everywhere; dropping it never is.
* **Slots say what they hold.** A field, element, global or annotated
  variable holds a read-only reference only if its type is `const`:
  `struct Named { name: const u8[:] }` can hold a literal, `struct Buf {
  bytes: u8[:] }` cannot. An un-annotated variable takes its initializer's
  constness, so `var t = trim(line);` for a read-only `line` is a
  `const u8[:]` cursor.
* `const` is **shallow**: a reference read out of a field of a `const
  Box&` is as writable as the field's own type says.

```goose
fn upper_first(s: u8[:]) { if s.len > 0 && s[0] >= 'a' { s[0] -= 32; } }

struct Named { name: const u8[:] }

fn main() {
    var buf: u8[>..] = [];
    buf.append("goose");
    upper_first(buf);                  // a writable argument: fine
    let n = Named { name: "literal" }; // a const slot takes a literal
    let who = if buf.len > 3 { n.name } else { buf[..] };   // read-only, since one branch is
    print(buf, " ", who);
    // upper_first("literal");         // error: writes through a read-only parameter
}
```

```
Goose literal
```

Spec §9.5.

### Where a reference read out of a container may point

A container identifies a scope, but not necessarily the storage its contents
point into. The checker therefore derives the root again when reading a
reference from a container. Each variable in scope whose storage can hold the
pointee's type is a **candidate**. With one candidate, the reference is rooted
there exactly. With several, it is rooted at all of them inexactly.

Some operations need an array's exact identity: storing a relative link, calling
`index_of`, or determining which array a shrink through the reference frees. If
the checker cannot choose, it reports the candidates: "`n` was read out of
`slots` and may point into `pool` or `spare`". Two rules narrow the candidates.
A relative link `in pool` points exactly into `pool`. A local holder whose
stores are all known to the checker is rooted at the values stored into it. Spec
§9.5.

### Reading the errors

When the checker rejects something, read the whole message. It names the
reference *and* where it was bound, the holder *and* the line that stored
into it, the instantiation *and* the call chain that produced it. Use
those two locations to decide which end to change: use the view for the
last time before the shrink, move the shrink, or move the declaration.

---

## 13. Growing, shrinking and assigning whole arrays

Growth is simple: `push` and `append` bump the stack, and a limited array
grows within its capacity. Shrinking is where the array kinds differ,
because a shrink frees elements that something may still point at.

### Grow-only arrays shrink where nothing points in

"Grow-only" names a guarantee, not an operation set: **a `T[>..]` never
shrinks while a reference or slice into it can be live.** Memory below the
top never moves and is never reused while the owner lives, so every
reference into the array stays valid for as long as it can be named. A
shrink (`pop`, `pop_n`, `resize` downward, `clear`, or assigning the
whole array) is legal exactly where the compiler can see that nothing
rooted in the array is still in use:

* no reference or slice *variable* rooted at the array is used after the
  shrink (in its own block, an enclosing block, or a later iteration of a
  loop around it), and
* no *holder* (a struct with a reference field, an array of slices) still
  in use had a reference into the array stored into it, and
* the shrink's own statement does not use a reference into the array that
  it evaluated earlier (`use(a[0], a.pop())` is an error; `a[0] +
  a.pop()` is not, because it reads its left operand first).

The test is liveness, not scope: a slice whose last use is before the
shrink is dead, and the shrink is fine. So a scratch buffer refilled per
iteration can hand out slices of itself freely. The error, when there is
one, names the holder and the line where a reference into the array was
stored into it.

```goose
fn reset(buf: u8[>..]&) { buf.clear(); }

fn main() {
    var line: u8[>..] = [];
    for i in 3 {
        reset(line);                        // nothing points into line here
        format(line, "line ", i + 1);
        let head = line[..4];
        print(head, "|", line[4..]);        // head's last use
    }
    var holder: u8[:][>..] = [];
    holder.push(line[..]);
    // line.clear();                        // error: holder may hold a reference into line,
                                            //        stored at the push above
    print(holder.len);
}
```

```
line| 1
line| 2
line| 3
1
```

A shrink through a reference parameter, or of a global, cannot see the
caller's variables, so it is checked at every level. Each function records
which globals and which parameters' targets it shrinks, and a call is
checked as if it made those shrinks in the caller. `pop`, `pop_n` and
`resize` also need fixed-size elements, since a sequential array cannot
find its last element. Spec §5.1.

### Grow-shrink arrays keep their references in variables

A `T[>..<]` holds fixed-size elements and shrinks from anywhere: on a
local, through a reference, on a global, as a struct's tail. The
trade-off is that **references and slices into it live in variables
only.** They may be bound to a local, passed down and returned, but never
stored into a field, an element, a global or a literal. After a pop, the
freed memory at the top of the stack may be reused by a different type, so
no reference into it may outlive the next shrink. Because such references
are kept out of storage, the check only needs to scan the variables in
scope. A shrink is an error while any such variable is still used
afterwards; the error names the variable and where it was bound.

```goose
fn main() {
    var stack: i64[>..<] = [];
    for i in 5 { stack.push(i * 10); }
    let top .= stack[4];
    print(top);                             // last use of top
    stack.pop();                            // fine
    var popped = 0;
    while stack.len > 2 { popped += stack.pop(); }
    print(stack, " popped ", popped);
}
```

```
40
[0, 10] popped 50
```

A call is **balanced** for an array when the array is never shorter
during the call than when it began, for example a function that takes
`let mark = xs.len`, pushes, and ends with `xs.resize(mark)`. A balanced
call frees nothing that a view taken before it points into, so it does not
count as a shrink at the place where it is made. This is how a recursion
shares one scratch stack ([section 19](#19-recursion)). Spec §5.2.

### Limited arrays

`T[..k]` and `T[..]` grow and shrink freely within their capacity, and
references into them are allowed and survive pops: the capacity and the
element type are fixed, so a slot once written stays a valid value of the
element type forever. A stale reference reads whatever value the slot
holds now, never corrupt memory. Exceeding the capacity aborts. Spec §5.3.

### Assigning a whole array

An lvalue may be assigned after construction when its size cannot change,
or when it can absorb the change:

* fixed-size `var` lvalues: a plain overwrite;
* a whole resizable array, or a value whose tail is one: assignment means
  *clear, then construct*. The right-hand side is built where the old
  elements were, so it may not use the array at all, not even its length
  (`x = f(x)` is an error; build the new contents in a variable and assign
  `copy()` of it);
* limited arrays, if the new length fits;
* fixed-mode enum lvalues, with any variant;
* **not** assignable: variable-class lvalues (`T[]` fields, variable-mode
  enum values, `varint` fields), which are frozen at construction. Rebuild
  the container instead.

```goose
struct Rec { name: u8[], score: i64 }

fn main() {
    var xs: i64[>..] = [1, 2, 3];
    xs = [4, 5];                            // a shrink, then the new contents
    var ys: i64[>..] = [xs[0] + 1];
    xs = copy(ys);                          // computed from other storage: fine
    var recs: Rec[>..] = [];
    recs.push(Rec { name: "a", score: 1 });
    for r in recs { r.score = 2; }          // a fixed-size field: assignable
    // for r in recs { r.name = "bb"; }     // error: a variable-class field is frozen
    var one = Rec { name: "a", score: 1 };
    // one = Rec { name: "bb", score: 3 };  // error: and so is a variable-class local
    recs = [Rec { name: "bb", score: 3 }];  // rebuild the container instead
    print(xs, " ", recs, " ", one.score);
}
```

```
[5] [Rec { "bb", 3 }] 1
```

Spec §4.4.

### Construction in progress

While a value is being built in place inside an array (an element being
pushed, a call's result being appended, the new contents of a whole
assignment), nothing may grow that array: neither the expression being
built nor a function it calls. `xs.push(f(xs))` where `f` pushes into its
argument is rejected. Spec §1.3, §4.2.

---

## 14. Reusable pools

Some data has lifetimes that do not nest: cache entries, nodes of a graph
that is edited, files deleted in any order. Use the standard library's
[`slot_pool<T>`](stdlib.md#slot_pool) for individual slots and
[`slice_pool<T>`](stdlib.md#slice_pool) for contiguous runs. Each is a root
struct bundling an `items` array and its reuse metadata in one declaration.
Owners must be local or global variables; functions take them by reference.

**`reusable` and `reusable[]` are deprecated.** The builtins remain available
for existing code and the [remaining migration gaps](design/root_structs.md#separate-follow-up-reusable-pools-in-the-stdlib).
Their removal is a later step.

Import `slot_pool` to use these operations:

| Operation | Meaning |
|---|---|
| `a.alloc_index(v) -> i64` | the index of a slot holding `v`: a freed slot if there is one, else a fresh push |
| `a.alloc_ref(v) -> T&` | the same, as a reference |
| `a.free(i)` | records slot `i` for reuse; aborts if `i` is out of range |

`free` releases no memory and ends no lifetime. **Every element remains a
valid, live value of its type at all times**, so a reference to a freed
and reused slot reads a *different value of the correct type*: a logic
bug of the same kind as a stale index, never memory corruption, type
confusion or an out-of-bounds access. Such logic bugs are the full extent
of the unsafety, and this design is what lets tree-mutating workloads run
without an allocator.

```goose
import slot_pool;
struct Item { id: i32, weight: f32 }

fn main() {
    var pool = slot_pool<Item> {};
    let s0 = pool.alloc_index(Item { 10, 1.0 });
    let s1 = pool.alloc_index(Item { 11, 1.0 });
    pool.free(s0);
    let s2 = pool.alloc_index(Item { 12, 2.0 });   // takes slot s0 back
    let node .= pool.alloc_ref(Item { 13, 3.0 });
    pool.free(pool.items.index_of(node));          // a reference turned back into its slot
    print("slots ", s0, " ", s1, " ", s2, ", pool length ", pool.items.len);
}
```

```
slots 0 1 0, pool length 3
```

A pool may be a local or a global. Its `items` array never shrinks through
the pool API; `items.len` includes free slots. Use `items` for ordinary array
access, and pass the owner as `slot_pool<T>&`. Elements must be fixed-size.
`alloc_index` and `alloc_ref` take normally copyable values. For fresh values
with self-relative links or pool-relative `self`, use
`pool.alloc_with() { Node { value: 1, me: self } }`: the builder runs once,
directly in the chosen slot, and returns a reference to it.
Do not edit its `free_slots` metadata or shrink `items`
directly. See the [library contract](stdlib.md#slot_pool) for supported types
and lifetime rules.

### Slice pools

`slice_pool<T>` hands out *runs* of elements instead of single slots, with a
tree of free spans that merges neighbours:

| Operation | Meaning |
|---|---|
| `a.alloc_slice(n) -> T[:]` | `n` elements from the first free span that holds them, else from the end of the array, which grows; every returned element is reset to its default |
| `a.free_slice(s)` | records the run `s` for reuse |
| `a.realloc_slice(s, n) -> T[:]` | `s` resized to `n` elements, in place where it can be, else freed and copied to where `alloc_slice(n)` puts it |

This suits the child lists of an n-ary tree: each node's entries are one
contiguous run that can be walked as a plain slice.

```goose
import slice_pool;
fn main() {
    var pool = slice_pool<i64> {};
    let a = pool.alloc_slice(3);
    a[0] = 1; a[1] = 2; a[2] = 3;
    let b = pool.alloc_slice(2);
    b[0] = 7; b[1] = 8;
    pool.free_slice(a);
    let c = pool.alloc_slice(2);            // from the freed run, reset to defaults
    let d = pool.realloc_slice(b, 4);       // b ends the array: grows in place
    print(c, " ", d, " ", pool.items.len, " ", pool.items);
}
```

```
[0, 0] [7, 8, 0, 0] 7 [0, 0, 3, 7, 8, 0, 0]
```

Free and resize accept subranges of allocated runs. A nonempty slice must be
aligned and wholly within `items`; where the checker cannot prove that, the
call checks at runtime. Empty/default slices can be freed harmlessly, and growing
an empty slice allocates a new run. Double frees remain logical errors; there
is no occupancy check. A grown slice may be moved, so
`realloc_slice` is unavailable for an element type holding self-relative
links. Allocation and free do support defaultable self-relative elements.
Pass an owner as `slice_pool<T>&`, and use its `items` array for indexing and
slicing. The [library contract](stdlib.md#slice_pool) covers defaults,
subranges, empty slices, high-water storage and relocation.

### Deprecated builtin pools

The legacy declarations `reusable var pool: T[>..] = [];` and
`reusable[] var pool: T[>..] = [];` still provide the corresponding operations
directly on the array, with hidden metadata. Their parameter type is `T[>..]&`;
pool provenance carries the metadata through calls. Existing `push` and
`append` operations add at the end without using freed spans. Their unchanged
semantics are specified in [spec §5.4](goose_spec.md#54-reusable-arrays-safe-allocation-escape-hatch).

To migrate, import the appropriate module, declare `slot_pool<T> {}` or
`slice_pool<T> {}`, update owner parameters to the corresponding root reference,
and use `pool.items` for array operations. Global index/pool-relative types
can name `pool.items`. For a value that needs construction in its receiving
slot, replace `alloc_ref(value)` with `alloc_with() { value }`; obtain its index
with `pool.items.index_of` if needed. Ordinary value allocations and slice
operations keep their method names. Owner transfer to workers remains a
migration gap; keep the deprecated builtins for code requiring worker snapshots.
Worker-local library pools already work.

---

## 15. Index types and checkfree functions

Every index is bounds-checked, and the compiler removes the checks it can
prove unnecessary ([section 16](#bounds-checks)). The check that no
analysis removes is an index read out of *another* array, `dist[adj[k]]`,
since whether it is in range depends on the contents of one array compared
with the length of another. Index types move that check to where the
index enters the program.

An **index type** `uN in a` (`u8`, `u16`, `u32` or `u64`) is an unsigned
integer that is below `a.len` wherever it can be observed. A **bound
type** `uN upto a` is an unsigned integer that is at most `a.len`, used
for the end of a range. `a` names a global `var` array (including deprecated `reusable var`)
or a mutable array field of a global `var` root, such as `u32 in graph.nodes`.
It must be grow-only with fixed-size elements, and the array is part of the
type's identity: `u32 in nodes` is neither `u32` nor `u32 in edges`. The
value is the plain integer.

```goose
struct Node { lo: u32 upto edges, hi: u32 upto edges, weight: i32 }
var nodes: Node[>..] = [];
type NodeId = u32 in nodes;          // an alias reads better at every use
var edges: NodeId[>..] = [];
```

* **Making one.** `x as (uN in a)` aborts in every build unless
  `0 <= x < a.len`, with the error `a[x]` would give. It is a bounds check
  moved to this point, and the bounds-check pass elides it where it would
  elide `a[x]`. `as!` cannot make one, and a literal never adapts to one.
* **Using one.** Indexing `a` with an index of `a` needs no check. A
  value converts implicitly to its plain integer type, so arithmetic on it
  gives plain integers; compound assignment on a place of an index type is
  an error, and so is memberwise arithmetic on a struct or fixed array
  holding one.
* **Loops.** `for i in lo..hi` whose bounds are index or bound types of
  one array gives `i` that array's index type; so does `for i in n` with
  `n` of one.
* **Passing through.** Declarations, parameters, results, fields, payloads
  and elements carry the type, and a branch whose arms all have one
  array's index types keeps it (at the widest width). A reference to such
  storage is `(u32 in a)&`, never a `u32&`.
* **Never shrunk.** An array an index type names cannot be shrunk
  anywhere, so a value once in range stays in range. An index type has no
  default value. `from_bytes` checks every index in an image against its
  array.

An index type takes no postfix, so an array of them is
`(u32 in nodes)[>..]` or, with the alias, `NodeId[>..]`. An index type
written on a `for` binder is parenthesized too:
`for v: (u32 in nodes) in …`.

```goose
struct Node { lo: u32 upto edges, hi: u32 upto edges, weight: i32 }
var nodes: Node[>..] = [];
type NodeId = u32 in nodes;
var edges: NodeId[>..] = [];

fn add_node(w: i32) -> NodeId {
    let none = 0 as (u32 upto edges);
    nodes.push(Node { lo: none, hi: none, weight: w });
    return (nodes.len - 1) as NodeId;
}

fn main() {
    for w in [5, 3, 8, 1] { add_node(w as i32); }
    for v in nodes.len {                                   // v links to v + 1 and v + 2
        nodes[v].lo = edges.len as (u32 upto edges);
        for d in 1..3 { if v + d < nodes.len { edges.push((v + d) as NodeId); } }
        nodes[v].hi = edges.len as (u32 upto edges);
    }
    var total = 0;
    for v in nodes.len {
        let n = nodes[v];
        for k in n.lo..n.hi { total += nodes[edges[k]].weight; }   // no checks left
    }
    let w = edges[0];
    let plain: u32 = w;                                    // as a number
    print(total, " ", w, " ", plain + 1);
}
```

```
21 1 2
```

Spec §3.11, `docs/design/index_types.md`.

### `checkfree fn`

`checkfree` asks the compiler to reject the program if a bounds check is
left anywhere in the function's text after bounds-check elimination, in
every specialization and every copy inlined into a caller. It changes
nothing about what the program computes. Every call in such a function
must be to another `checkfree` function, a builtin or an `extern fn`, and
a block it calls must be written in a `checkfree` function. The other
aborts remain, in particular `assert`, which such a function uses to state
its preconditions once, outside its loops:

```goose
var nodes: i32[>..] = [];
type NodeId = u32 in nodes;

checkfree fn sum_at(ids: NodeId[:], weights: i32[>..]&) -> i64 {
    assert(weights.len >= nodes.len);          // once; the loop is then check-free
    var t = 0;
    for id in ids { t += nodes[id] + weights[id]; }
    return t;
}

fn main() {
    for w in [1, 2, 3] { nodes.push(w as i32); }
    var weights: i32[>..] = [10, 20, 30];
    let ids: NodeId[2] = [2 as NodeId, 0 as NodeId];
    print(sum_at(ids, weights));
}
```

```
44
```

Inside a `checkfree` function, the compiler must prove at least these
checks unnecessary:

* a constant index into a fixed array;
* an index of `a`'s index type into `a`;
* an index of `a`'s index type into `b`, where `b.len >= a.len` was
  asserted and neither array has changed since;
* a `for` index into the array whose length is its count;
* `a[i]` after a condition established `0 <= i < a.len`;
* a repeat of a check already passed.

The error names the check that remained and why. `checkfree` goes after
`export` and before `inline`, `simd`, `recursive` or `fn`, and a nested
function inherits it. Spec §7.14.

---

## 16. Expressions and control flow

The language is expression-oriented: `if`, `match`, `block` and a bare
`{ … }` are expressions, and a block's value is its trailing expression.
Assignment, `++` and `--` are statements. The operators are C's and
Rust's, with Rust's precedence ([Appendix A](#appendix-a-keywords-and-operators)).

### `if`

```goose
let hp = 30;
let kind = if hp > 50 { "healthy" } else if hp > 0 { "hurt" } else { "down" };
if hp > 0 { print(kind); }               // as a statement: no else needed
```

Used as a value, an `if` needs a final `else`, and its branches join into
one type. A literal branch adapts to the other's type: `if c { x } else {
0 }` has `x`'s type, a `u8` for a `u8` `x`, and `if c { x } else { 0.5 }`
is an `f32` for an `f32` `x`. An integer branch beside a float branch
converts to the float. A construct whose branches are all literals is
itself literal-like and adapts wherever it meets a type. Arrays and slices
of one element type join as a *slice*: where one branch is a string
literal and the other a `u8[..16]` field, the result is a `const u8[:]`
viewing the field, with nothing copied. Spec §6.4.

```goose
struct Item { name: u8[..16], count: i64 }
fn main() {
    let item = Item { name: "bolt", count: 0 };
    let name = if item.count == 0 { "none" } else { item.name };   // a const u8[:]
    let k: u8 = if item.count > 0 { 1 } else { 2 };               // literals adapt to u8
    print(name, " ", k);
}
```

```
none 2
```

A trailing construct that cannot supply a value is a statement: an `if`
without `else`, a `match` with an arm that ends in a `return`, or a `loop`
none of whose `break`s carries a value. So a function whose result is
inferred may end in one and produce nothing.

### `block`, `break` with a value, and bare blocks

`block { … }` gives `break` a target without a loop, so a chain of checks
can bail out with a value. A bare `{ … }` is only a scope: a `break`
inside it leaves the enclosing loop or `block`. All the `break E` of one
construct must agree on `E`'s type.

```goose
struct P { alive: bool, hp: i64 }
fn main() {
    let p = P { alive: true, hp: 0 };
    let verdict = block {
        guard p.alive else { break "gone"; }
        guard p.hp > 0 else { break "dead"; }
        "standing"
    };
    let doubled = { let t = p.hp + 1; t * 2 };
    var n = 0;
    let found = loop { n += 7; if n % 5 == 0 { break n; } };
    print(verdict, " ", doubled, " ", found);
}
```

```
dead 2 35
```

### `guard`

`guard c;` runs the rest of its block only where `c` holds, and `guard c
else { … }` runs the `else` block instead. It is `if` turned inside out,
and it does not jump anywhere by itself. Directly in a function body, the
rest of the block is the rest of the function; in a loop body, skipping
the rest goes on to the next iteration; in a function value's body, it
ends that call of the value. Leaving more than the block is the `else`
block's job: `guard c else { return; }` leaves the function,
`guard c else { break; }` the loop. In the code after a guard, the
condition is known to hold, which includes narrowing an optional: after
`guard r else { return; }`, `r` is a `T&`.

```goose
fn half(n: i64) -> i64 { guard n % 2 == 0 else { return -1; } n / 2 }
fn tenfold(p: i64?) -> i64 { guard p else { return 0; } p * 10 }

fn main() {
    for i in 5 { guard i % 2 == 0; print("even ", i); }
    let v = 4;
    print(half(8), " ", half(7), " ", tenfold(v), " ", tenfold(null));
}
```

```
even 0
even 2
even 4
4 -1 40 0
```

Spec §6.4.

### `defer`

`defer s;` and `defer { … }` run their body when the block they are
written in ends, however it ends: falling off the end, `break`,
`continue`, `return`, a block's `return` through a higher-order function,
or a `return … from` passing through the function. A `defer` in a loop
body runs at the end of every iteration. An exit runs the defers written
before it, latest first, so a cleanup sits next to what it cleans up and
is skipped where that never happened. The body is ordinary code run later:
it sees variables as they are at the exit, and on a `return` it runs after
the returned value is built and before the block's storage is released.

```goose
var depth = 0;

fn describe(items: i64[:]) -> i64 {
    depth++;
    defer depth--;                   // every way out of describe restores it
    print("depth ", depth);
    var count = 0;
    for x in items {
        defer count++;               // once per iteration, continue included
        if x < 0 { continue; }
        if x > 100 { return -1; }
    }
    return count;
}

fn main() {
    print(describe([1, 2, -3]), " ", describe([5, 500, 7]), " ", depth);
    {
        defer print("block done");
        print("in the block");
    }
}
```

```
depth 1
depth 1
3 -1 0
in the block
block done
```

The body is checked where it is written, so it names only what is declared
before it, and a `var` optional narrowed before it is not narrowed inside
it, since an assignment before an exit may undo that. Three things it may
not do, each a compile error:

* **Leave.** No `return`, `break` or `continue` in it may target anything
  outside it, including a `return` in a block passed to a call in the body.
* **Use long-distance returns.** It may not call a function that does
  `return … from`, propagates one, or catches one, since the body may run
  while such a return is in flight ([section 20](#20-long-distance-return-and-errors));
  the error names the chain of calls that brings the machinery in.
* **Name the function's named result.** A local that the function returns
  by name is built where the caller receives it ([section 17](#results)),
  and is complete when the body runs.

A `defer` belongs in a function: a global initializer has no scope exit.
Nothing runs a defer where no exit happens: `abort`, a failed `assert` or
bounds check, `exit`, and a contained worker's abort end the code without
running any, and the process or the worker's end reclaims what the body
would have released. Spec §6.6.

### Loops

<!-- fragment -->
```goose
for i in 3 { }              // 0, 1, 2
for i in 10..13 { }         // 10, 11, 12: half-open
for i: i32 in 0..10 { }     // i is an i32; each bound must be, or widen to, one
for i: u8 in 0..256 { }     // every u8, the end one past the type's last value
for x in xs { }             // copies, for fixed-size elements; references for others
for &x in xs { x *= 2; }    // references: the mutation form
for x, i in xs { }          // with the index, an i64
for x, i: u8 in grid { }    // the index at a narrower type, where the array's length fits it
while cond { }
loop { if done { break; } }
```

* A range's bounds unify like operands, and `i` runs at that type; `for i
  in n` is `0..n`. Bounds are evaluated once; an empty or reversed range
  does nothing.
* `for x in arr` copies fixed-size elements and binds variable-size ones
  by reference. A copy cannot be written (`x.f = 1` is an error naming
  `&x`), since the write would update only the copy. An element that
  *is* a reference binds as the reference it holds.
* `for &x in arr` binds every element by reference. Over a `T[>..<]` the
  binding is a live reference, so no shrink may happen inside the loop.
* A loop iterates its sequence in place. A resizable array's length is
  read again before each iteration, so pushes inside a by-value `for` add
  to the traversal; a slice keeps its own length. Whatever the loop walks
  in place is live for the whole body, so the array it lies in may not
  shrink there.
* `continue` goes to the next element; `break` leaves the innermost loop
  or `block`. There are no labels.

```goose
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    for &x in xs { x *= 10; }
    for x, i in xs { if i == 0 { xs.push(x + 1); } }   // the push extends the walk
    var count: u8 = 0;
    for i: u8 in 0..256 { count += 1; }
    print(xs, " ", count);
}
```

```
[10, 20, 30, 11] 0
```

(`count` wrapped to 0 after 256 increments, as a `u8` does.) Spec §6.5.

### `const for`: expansion at specialization time

`const for` specializes a fragment once per entry of a compile-time
sequence: the fields of a struct, the variants of an enum, a list of
types, or a list of constants.
It is the language's reflection, with no runtime iterator and no
metadata: each copy of the body is ordinary code.

```goose
struct Point { x: i64, y: i64, label: u8[..8] }

fn show_fields<T>(v: T&) {
    const for <N, A> in fields<T> { print(N, " = ", v.A); }
}

fn scaled<T, F>(v: T&) -> T {
    T { const for <N, A> in fields<T> { A: F(N, v.A), } }
}

fn main() {
    var p = Point { x: 2, y: 3, label: "pt" };
    show_fields(p);
    const for <U> in types<i8, u16, f32> { print(default<U>()); }
    const for <C> in [1, 2, 4] { let a: u8[C] = [0; C]; print(a.len); }
    let q = scaled(p) { n, x => x };
    print(q);
}
```

```
x = 2
y = 3
label = pt
0
0
0.0
1
2
4
Point { 2, 3, "pt" }
```

`fields<T>` binds each field's name (a string constant) and an
*accessor*, usable as `v.A` or as the label `A:` in a literal of that
struct type. `types<…>` binds a type, and `[…]` binds a constant
(literals, named constants, and the integer expressions an array size
allows). A statement fragment is a scope of its own for each entry, and a
constructor fragment contributes initializers. `break` and `continue`
cannot cross an expansion.

`variants<E>` binds each variant's name and the variant `V`, which is
written wherever a variant's name would be: `V` as a type (and `fields<V>`
for its payload), `V { … }` to build one, `V c` or `V &c` as a `match`
arm, and `V` as a value when it has no payload. A `match` fragment
contributes arms, which are checked like written arms: every variant
once, `_` last.

```goose
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }

fn show<T>(value: T) {
    match value {
        const for <N, V> in variants<T> {
            V v => {
                print(N);
                const for <F, A> in fields<V> { print("  ", F, " = ", v.A); }
            },
        }
    }
}

fn main() {
    let s: Shape = Shape.Rect { w: 3, h: 4 };
    show(s);
    const for <N, V> in variants<Shape> { print(N); }
}
```

```
Rect
  w = 3
  h = 4
Circle
Rect
Dot
```

Spec §6.5, `docs/design/const_for.md`.

### Functions that never return

`abort(msg)` and `exit(code)` never return, and the checker takes this
into account: a branch that ends in one gives no value, statements after
one are not checked, and this propagates through calls, so a function may
end in a call to one without a dummy result.

```goose
fn fail(what: u8[:]) { abort(str("cannot ", what)); }

fn value(ok: bool) -> i64 {
    if ok { 42 } else { fail("compute") }
}

fn main() { print(value(true)); }
```

```
42
```

An unknown recursive call is assumed to return. Spec §7.3, §9.3.

### Bounds checks

Every index into an array or slice is checked, as is every conversion
into an index type ([section 15](#15-index-types-and-checkfree-functions)).
A check that fails aborts the program. The compiler removes the checks it
can prove unnecessary, and the language is designed to give that analysis
what it needs:

* a `T[>..]` shrinks only at a `pop`, `pop_n`, `resize` or `clear` the
  analysis sees, so a bound established before a `push` still holds after
  it;
* `%` is Euclidean, so a reduction is always in range;
* fixed sizes and capacities are constants;
* every call site records what it proves about its arguments.

The idiom that helps most is an `assert` of the lengths at the top of a
kernel:

```goose
let W = 8;
fn blur_row(above: u8[:], here: u8[:], below: u8[:], out: u8[:]) {
    assert(above.len == W && here.len == W && below.len == W && out.len == W);
    for x in 1..W - 1 {                     // every index is x plus a constant: no checks
        let sum = above[x - 1] as i64 + above[x] + above[x + 1] +
                  here[x - 1] + here[x] + here[x + 1] +
                  below[x - 1] + below[x] + below[x + 1];
        out[x] = (sum / 9) as! u8;
    }
}
```

`goose --bce-lines` reports, per line, which checks survived. The other
aborts that remain in every build are division by zero, a limited
array's capacity overflow, shrinking below empty, a relative-reference
offset that does not fit its width, a failed `assert`, and data-stack
exhaustion. All of them are clean aborts with a message and the line of
the check that failed; a check inside a standard library function names the
line of the program's code that called the library. Spec §9.3, §10.5.

---

## 17. Functions

<!-- fragment -->
```goose
fn name(a: i64, var b: f64, xs: i32[:]) -> i64 { … }
fn generic<T>(a: T, b: T) -> T { … }
fn also_generic(a, b) { … }              // untyped parameters are generic
fn scaled(a: i32, b: i32 = 0, c: f32 = 1.0) -> f32 { … }   // scaled(2) is scaled(2, 0, 1.0)
```

Functions are free functions. There are no methods and no `impl` blocks,
but **`x.f(a)` calls `f(x, a)`** for any function, so method-call syntax
is available wherever it reads better. Resolution tries fields and
built-in members first, then functions, starting in the namespace of
`x`'s type ([Namespaces](#namespaces)). Top-level declarations are
order-independent. Spec §7.1.

### Parameters

* A parameter is a `let` binding: it cannot be reassigned, while its
  contents are as writable as its type says. `var b: f64` makes a by-value
  parameter a reassignable local.
* A parameter with no type is generic ([section 18](#18-generics-and-function-values)).
* A reference parameter, `n: i64&`, binds an lvalue of the pointee type
  without `&`. A slice parameter takes every array kind. A by-value
  non-fixed parameter, `xs: u8[>..]`, is constructed in the argument slot
  and owned by the callee like a local: `f(copy(xs))` or `f(make())`.
* **Defaults.** A trailing parameter may declare a default, `c: f32 =
  1.0`, and a call may leave out any number of trailing parameters that
  have one. The default is evaluated at each such call, after the written
  arguments, as a value of the parameter's type. It may name globals and
  call functions, but cannot refer to the caller's locals. A generic
  parameter's default adapts to the type the arguments bind
  (`fn scale<T>(x: T, by: T = 2)` doubles a `u8` and an `f64` alike) and
  never decides a type variable itself.

```goose
var counter = 0;
fn bump(r: i64& = counter, by: i64 = 1) { r += by; }
fn limit(x: i64, lo: i64 = 0, hi: i64 = 100) -> i64 {
    if x < lo { lo } else if x > hi { hi } else { x }
}
fn scale<T>(x: T, by: T = 2) -> T { x * by }

fn main() {
    bump(); bump();
    var mine = 10;
    bump(mine, 5);
    let b: u8 = 21;
    print(counter, " ", mine, " ", limit(250), " ", limit(-5, -10), " ", 7.limit(0, 5), " ",
          scale(b), " ", scale(1.5));
}
```

```
2 15 100 -5 5 42 3.0
```

### Results

A function declares its results after `->`, several of them separated by
commas. Several results are a calling convention, not a tuple type: a
call in an ordinary value context yields the first, `let a, b = f();`
binds them all, `x, xs[i] = f();` assigns them to places that exist, and
`return f()` forwards all of them. By convention a trailing `bool` or
error value says whether the rest means anything.

```goose
fn divide(a: i64, b: i64) -> i64, bool {
    if b == 0 { return 0, false; }
    return a / b, true;
}

fn main() {
    let q, ok = divide(7, 2);
    var r = 0;
    var flag = false;
    r, flag = divide(1, 0);
    print(q, " ", ok, " ", r, " ", flag, " ", divide(9, 3));
}
```

```
3 true 0 false 3
```

Only `return` and a call carry several values: a branch cannot end in
`a, b`, so each branch returns on its own.

A result type may be left out where it can be inferred. It is then taken
from the first `return` (or the body's final expression), the way an
un-annotated `let` takes its initializer's type. A non-fixed lvalue other
than the function's own local, or an explicit `&x`, makes the result a
reference, and a reference to a fixed-size value is returned as a copy.
Across a recursive cycle every signature must be written out.

A non-fixed result is **constructed directly in its destination**
([section 4](#built-in-place)): the caller's variable, the element being
pushed, the argument slot of another call. When every `return` of such a
result returns the same local, that local *is* the destination from its
declaration on, so `return xs` copies nothing (the compiler warns where a
second returned local stops the first one from being allocated at the
destination). A function's own local is *moved* by `return`; a part of
it, or of a temporary, dies with the call and has to be returned as
`copy(x)`. Spec §7.1, §7.3.

### Overloading

Functions overload by parameter types, resolved statically. A unique
exact match is chosen first, then a generic exact match, then a match that
needs a coercion (array to slice, a literal fitting a type, implicit
widening). A candidate's rank is its worst argument's rank, and ties are
errors. A default does not count as an argument, so `f(a, b = 0)` beside
`f(a)` makes every one-argument call ambiguous. Tag dispatch
([section 9](#case-functions)) is tried only when no ordinary candidate
matches. A candidate that converts an integer argument to a float comes
last of all: `f(2)` calls `f(i32)` beside `f(f64)`, and `sqrt(n)` with an
integer `n` is ambiguous between the two widths (write `sqrt(n as f64)`).
The expected result type never selects an overload.

```goose
fn describe(x: i64) -> u8[>..]     { str("int ", x) }
fn describe(x: f64) -> u8[>..]     { str("float ", x) }
fn describe(xs: i64[:]) -> u8[>..] { str(xs.len, " ints, first ", xs[0]) }

fn main() {
    let xs: i64[3] = [4, 8, 15];
    print(describe(7), "; ", 2.5.describe(), "; ", xs.describe(), "; ", describe(xs[1..]));
}
```

```
int 7; float 2.5; 3 ints, first 4; 2 ints, first 8
```

Spec §7.1.

### Nested functions

A function may be declared inside a function, and it reads and writes the
enclosing function's locals as free variables, under those variables'
normal rules. Names resolve where the nested function is declared. The
nested functions of one block may call each other in either order, as the
functions of a recursive-descent parser do, but the enclosing code can
call a nested function only after its declaration. Nested functions of
one name in one block overload like top-level ones, so case functions can
be nested too. A nested function is a compile-time entity: it is passed
as a function value and never escapes.

```goose
struct Employee { name: u8[..12], dept: u8[..8], salary: i32 }

fn main() {
    var staff: Employee[>..] = [];
    staff.push(Employee { name: "ada", dept: "eng", salary: 120 });
    staff.push(Employee { name: "linus", dept: "ops", salary: 90 });
    staff.push(Employee { name: "grace", dept: "eng", salary: 135 });
    var budget = 0;
    var seen = 0;
    fn spend(e: Employee&) {          // writes main's locals
        budget += e.salary;
        seen++;
    }
    for &e in staff { if e.dept == "eng" { spend(e); } }
    print("eng budget ", budget, " over ", seen, " people");
}
```

```
eng budget 255 over 2 people
```

Spec §7.5.

### Function qualifiers

| Qualifier | Meaning | Section |
|---|---|---|
| `recursive fn` | the entry of a recursive cycle | [19](#19-recursion) |
| `extern fn`, `extern fn … = "sym"` | a C function | [24](#24-calling-c-and-being-called-from-c) |
| `export fn`, `export "sym" fn` | a Goose function callable from C | [24](#24-calling-c-and-being-called-from-c) |
| `thread_fn` | a worker's entry point | [22](#22-threads) |
| `checkfree fn` | no bounds check may remain in it | [15](#checkfree-fn) |
| `inline fn` | inlined at every call, whatever its size | below |
| `simd fn` | built once per instruction-set level | below |

The qualifiers come in a fixed order: `export` or `extern` first, then
`checkfree`, `simd`, `inline`, `recursive`, and `fn` or `thread_fn`.

`inline` asks for the function to be inlined at every call; without it
the compiler decides from the function's size and its number of calls.
It changes nothing about what the program computes, and cannot be
combined with `extern`, `export`, `simd`, `recursive` or `thread_fn`.

`simd` asks for the function to be built once for the target's baseline
instruction set and once more for each wider one it supports (AVX2 and
AVX-512 on x86-64). At each call, the widest version the CPU has is
chosen. It suits a function holding a hot loop the C compiler can
vectorize: byte transforms, codecs, kernels over arrays. Every version
computes exactly what the baseline does, so nothing about a program's
output depends on it. A backend may ignore it, as the C backend does under
compilers other than clang and under the in-process TinyCC backend.
The exception is `math`'s `mul_add(x, y, z)`: `x * y + z` rounded once
where the code is built for FMA and twice where it is not. The AVX2 and
AVX-512 versions round once, and the baseline rounds as its target does.
Spec §7.12, §7.13.

```goose
simd fn checksum(xs: u8[:]) -> u32 {
    var h: u32 = 0;
    for x in xs { h += x as u32 * 31 + 7; }
    return h;
}

inline fn clamp01(x: f32) -> f32 { if x < 0 { 0 } else if x > 1 { 1 } else { x } }

fn main() { print(checksum("goose"), " ", clamp01(1.5)); }
```

```
16806 1.0
```

---

## 18. Generics and function values

### Type parameters

An untyped parameter is generic. An explicit `<T>` says that two
parameters must agree, or names a type no parameter carries. Everything is
monomorphized: a generic body is typechecked only at an instantiation
where every type is concrete, and errors are reported at the offending
instantiation with the compile-time call chain that produced it. Type
variables are never checked abstractly, so a generic body may do anything
the concrete types allow, with no trait bounds.

```goose
fn twice(x) { x + x }
fn span<T>(a: T, b: T) -> T { if a > b { a - b } else { b - a } }
fn zero<T>() -> T { default<T>() }

fn main() {
    print(twice(21), " ", twice(1.5), " ", span(3, 8), " ", span(2.5, 1.0), " ", zero<f64>() + 0.5);
}
```

```
42 3.0 5 1.5 0.5
```

Type arguments are inferred from the argument types. Where several
arguments mention one type variable, the typed ones bind it and a literal
then adapts: `max(n, 0)` with a `u32` `n` is the `u32` instantiation. A
float literal binds before an integer one, and `[]` and `null` bind
nothing. An explicit list `f<i64>(x)` binds the leading type parameters
in order; it is needed when no argument mentions a parameter
(`qget<i64>()`, `zero<f64>()`). `f<` begins a type argument list only
when the `<…>` is followed by `(`, `{` or `.Variant {`; otherwise `<` is a
comparison.

Inside generic code, `.=` on a location that is not a reference assigns
as `=` does. That lets generic code move whole values of a type parameter's
type with `.=`. Where `T` is a reference type, `.=` moves the reference
itself (where `=` would write through it); where `T` is a value, it is a
plain assignment. The library's `swap_at` is written that way, so it swaps
the elements of an array of references, including relative ones, as it
swaps those of any other array.

Where a literal is the only thing binding a type variable, the parameter
is a *literal parameter* inside the specialization: a constant of unknown
value that adapts wherever it meets a type. For example, with
`fn push_n<A, T>(xs: A&, v: T, n: i64)`, `push_n(flags, 1, n)` pushes a
`u8` into a `u8[>..]` with no cast at the call, and `push_n(flags, 300, n)`
is an error naming the `push` that takes a `u8`.

**Floating context.** An `f32` destination may refine an otherwise
unconstrained float computation in a generic call: with integer `width`
and `height`, `let scale: f32 = max(0.5, min(width / 960.0, height /
600.0));` computes both calls in `f32`. The rule applies to a function
that returns a bare `T` and whose `T` parameters are bare values, when it
is given literals and float expressions of literals and integers. A typed
argument fixes the type. Spec §7.7.

### Function values

Function values are compile-time entities passed as generic parameters,
not runtime data. A function argument is a function name, a bound generic
function parameter, or a block literal; the `: fn(…)` bound on the
parameter is optional documentation. Every call of a function value is
direct and inlinable, and a function value cannot escape: storing one,
returning one, or putting one in data is a compile error. There are no
closure objects and no function pointers, so a higher-order function
compiles to exactly the loop it looks like.

<!-- fragment -->
```goose
fn foo<F: fn(i64)>(a: i64) { F(a); }
foo(1) { print(it) }                 // the trailing-block form, with the implicit `it`
foo(1) { x => print(x) }             // named block parameters
fn h(v: i64) { print(v) }
foo(1, h)                            // a named function
```

* A trailing block `{ … }` after a call's parentheses is the last
  argument. Its parameters are `it`, or named ones before `=>`. Goose
  allows one trailing block per call; a second function value is passed
  by name.
* A block can read and write the enclosing function's locals, like a
  nested function.
* A `return` inside a block returns from the **lexically enclosing named
  function**, not from the function calling the block, so a hand-written
  iterator has the same structuring power as `for`. A reference returned
  this way may be one that the function calling the block handed to it,
  which points where that call's argument does. It may not point into that
  function's own locals or the block's, which die as the return leaves
  them.
* A named function keeps its parameter defaults when passed as a value; a
  block's parameters have none. A function value passed as an argument may
  come after fewer arguments than the callee has parameters, where the
  rest have defaults.
* A name with several overloads passes them all, and each call of the
  value resolves among them.
* A block's value type is inferred without the destination type its call
  meets, so a block's value cannot be `[]`. The storage that receives it
  is still known, and is used to check relative links in a fresh result.

```goose
fn each_pair<T, F>(xs: T[:]) {
    for i in 0..xs.len - 1 { F(xs[i], xs[i + 1]); }
}

fn each_step<F>(lo: i64, hi: i64, step: i64 = 1) {
    var i = lo;
    while i < hi { F(i); i += step; }
}

fn first_multiple_of_7(lo: i64, hi: i64) -> i64 {
    each_step(lo, hi, 3) { if it % 7 == 0 { return it; } };   // returns from first_multiple_of_7
    return -1;
}

fn main() {
    let xs: i64[5] = [4, 8, 15, 16, 23];
    var gaps: i64[>..] = [];
    each_pair(xs) { a, b => gaps.push(b - a); };
    let evens = xs.filter() { it % 2 == 0 };
    let total = fold(xs, 0) { acc, x => acc + x };
    let big = xs.find() { it > 10 };
    sort(gaps) { a, b => a > b };
    print(gaps, " ", evens, " ", total, " ", first_multiple_of_7(10, 40), " ",
          xs.map() { it * 2 }, " ", big);
}
```

```
[7, 7, 4, 1] [4, 8, 16] 66 28 [8, 16, 30, 32, 46] 15
```

The library's `filter` and `map` build their result directly in the
variable receiving it, with no intermediate array. A HOF never states what
its function value returns: `map`'s result element type is whatever `F(x)`
is in that instantiation. Spec §7.6, §7.7.

---

## 19. Recursion

Recursion is opt-in and annotated. The entry function of every recursive
cycle is marked `recursive fn`, every function in the cycle has a fully
explicit signature, and the key restriction is:

> No function in the cycle may call back into the cycle while one of its
> non-fixed locals is in scope.

A growable or variable-size local holds a data stack from its declaration
to the end of its block, and if one were in scope at a recursive call,
every activation would need a stack of its own. So a recursive function
owns no growable data across a recursive call, and what must outlive the
call is owned outside the cycle and passed in: a reference, a slice, a
pool, or a struct of references to several tables. Scratch declared in a
block that ends *before* the call is not affected by this rule. The same
rule applies to the temporaries of the calling statement: at a call into
the cycle, no non-fixed temporary may be live, which rules out a `str(…)`
as an argument of the call, or the call inside a `print`. Recursion depth
then uses only native stack, and exhausting it is a clean abort.

```goose
struct Node { key: i32, left: Node&<u32>?, right: Node&<u32>? }

recursive fn show(n: Node&, depth: i64) {
    {                                      // scratch in a block that ends before the calls
        var line: u8[>..] = [];
        for i in depth { line.append("  "); }
        format(line, n.key);
        print(line);
    }
    let l = n.left;
    if l { show(l, depth + 1); }
    let r = n.right;
    if r { show(r, depth + 1); }
}

recursive fn count(n: Node?) -> i64 {
    guard n else { return 0; }
    return 1 + count(n.left) + count(n.right);
}

fn main() {
    var pool: Node[>..] = [];
    let a .= pool.push(Node { key: 1 });
    a.left .= pool.push(Node { key: 2 });
    a.right .= pool.push(Node { key: 3 });
    show(a, 0);
    print(count(a), " nodes");
}
```

```
1
  2
  3
3 nodes
```

### Sharing scratch across the recursion

A grow-shrink array handed to the cycle by reference can serve every
activation at once: each takes `let mark = scratch.len`, pushes its own
entries above its callers', and ends with `scratch.resize(mark)`. Such
calls are *balanced* ([section 13](#grow-shrink-arrays-keep-their-references-in-variables)),
so a view of the activation's own part stays usable across them.

```goose
recursive fn walk(scratch: i64[>..<]&, depth: i64) -> i64 {
    let mark = scratch.len;
    scratch.push(depth);
    let mine = scratch[mark..];
    var total = 0;
    if depth < 3 { total = walk(scratch, depth + 1); }
    total += mine[0];
    scratch.resize(mark);
    return total;
}

fn main() {
    var scratch: i64[>..<] = [];
    print(walk(scratch, 0), " ", scratch.len);
}
```

```
6 0
```

A program whose state is several growable tables (nodes, text, a stack)
can own each as a local of a non-recursive driver function and pass the
recursion one struct of references to them. Alternatively, it can write
the recursive functions *nested* inside the driver, where they reach the
tables as free variables with no struct at all, as the samples' parsers
do.

### The rules in detail

* **Signatures.** No result type is inferred across a cycle, and none may
  be inferred as a reference.
* **Parameters.** No function in a cycle has a by-value non-fixed
  parameter: it takes a reference or a slice.
* **Stores.** A reference rooted at a cycle function's own local may be
  passed down, but not stored anywhere or returned, since activations of
  one local cannot be told apart. References rooted outside the cycle are
  unrestricted: a pool passed in by reference, a local of an enclosing
  non-recursive function, a global. Every recursive call must pass such a
  pool on as the entry call did.
* **Results.** A cycle function may return the result of a recursive
  call; the roots of such results are worked out in rounds over the
  cycle.
* **Polymorphic recursion.** A recursive call may instantiate its callee
  with other type arguments, but the instantiations must be finite: a call
  whose types grow with each round is rejected once it would put more than
  16 specializations of one function in progress.

Spec §7.8.

---

## 20. Long-distance return and errors

There is no exception mechanism and no `Result` type, and no required
error convention. For a call with two outcomes, a trailing `bool` is
enough; for "found or not", a `T?`, which copies nothing because it is a
reference into the input. Both are ordinary return values
([section 17](#results)). The third idiom is for a failure deep inside
nested calls that should be handled in one place.

### `return … from`

`return E from f` returns `E` as the result of the innermost active call
of `f`, unwinding every frame in between. The functions in between keep
their plain signatures: they take no error parameter and need no `?` on
each call or any other code to propagate the error.

```goose
struct User { name: u8[..16], age: i32 }

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
    let age, ok = parse_int(trim(line[comma + 1..]));
    guard ok else { return [], str("line ", lineno, ": bad age") from load; }
    return User { name: trim(line[..comma]), age: age as i32 };
}

fn report(text: u8[:]) {
    let users, err = load(text);
    if err.len > 0 { print("error: ", err); return; }
    print("loaded ", users.len, " users");
}

fn main() {
    report("ada, 36\nlinus, 20\n");
    report("ada, 36\nlinus\n");
    report("ada, x\n");
}
```

```
loaded 2 users
error: line 2: expected name,age
error: line 1: bad age
```

* **It is checked statically.** Every call of a function containing
  `return … from load` must lie within a call of `load`, which the
  whole-program compiler verifies in call-graph order, so no such return
  goes uncaught at run time.
* **It is a hidden discriminant, not an unwinder.** Each frame on the
  path checks a flag after the call and returns at once in the propagate
  case; normal epilogues run, so every data stack restores itself. There
  are no tables and no destructors; the only code that runs on the way is
  the `defer` bodies of the blocks being left ([section 16](#defer)).
* **The value lands where `load`'s caller wanted it.** `str(…)` builds the
  message directly at `load`'s return destination, bypassing the frames
  being unwound.
* `f` is resolved where the returning function is written: as a nested
  function in scope, otherwise as the name's overload set, and
  `from ns::parse` names the namespace explicitly. An unrelated function
  that merely shares the name never catches the return.
* The current compiler lets a long-distance return written in a named
  function carry references to globals and static data only; return owned
  values, as above.

The same machinery lets a block `return` from its enclosing function
through a higher-order function ([section 18](#function-values)).
The library's `exception` module packages it as `exception::try<E>(body)
{ error => … }` and `exception::throw(value)`, with a `finally` written
as a `defer` ([stdlib.md](stdlib.md#exception)). Spec §7.9.

### Stopping the program

`assert(c)` aborts unless `c` holds, and narrows an optional it is given;
`abort(msg)` aborts with a message; `exit(code)` ends the program with an
exit code. The last two never return, which the checker uses
([section 16](#functions-that-never-return)). None of them is catchable.
Spec §9.3.

---

## 21. Program structure

### Files and imports

A program is compiled whole, from the *root file* named on the command
line. `import a.b.c;` includes the file `a/b/c.goose` once. The path is
resolved first relative to the root of the importing file's package (for
the program's own files, that is the directory of the root file), then as
a module of a package the root declared, then in the standard library's
directory. `import
.a.b;` with a leading dot resolves relative to the importing file instead.
An import loads a file and nothing more: it creates no namespace and
brings no names into scope beyond what the file declares. The standard
library's modules are imported by name: `import std;`, `import
dictionary;`.

### Packages

`import name from "source";` in the root file declares the package
`name` (a directory holding its entry file `name.goose`) and imports that
entry. The source is a directory relative to the root file (`"../mylib"`)
or a git repository at a tag or commit (`"owner/repo@v1"` on GitHub,
`"host/owner/repo@ref"`, or a URL), which the compiler fetches and builds
once. Only the root file declares packages. The same line in any other
file, including a library's own files, is a *requirement* that the root
must satisfy, so a program's dependencies are flat, pinned and in one
place. [packages.md](packages.md) has the full details, including a
program's own C and how to make a package.

### Namespaces

`namespace image;` at the top of a file, before its declarations, places
them in the namespace `image`; a file without it declares into the global
namespace, and several files may share a namespace. From elsewhere, such
declarations are written `image::Pixel`, `image::brightness(p)`,
`image::Shape.Circle { r: 1.0 }`. `::name` names a global declaration that
a namespaced one shadows, or a builtin. A declaration may also write its
namespace itself, as in `fn image::brightness(…)`, or `fn ::main()` inside
a namespaced file.

```goose
import std;
namespace geo;

struct Pt { x: i64, y: i64 }
fn norm1(p: Pt) -> i64 { abs(p.x) + abs(p.y) }     // std's abs, found globally
fn format(out: u8[>..]&, p: Pt) { format(out, "(", p.x, ",", p.y, ")"); }

fn ::main() {                                       // declared in the global namespace
    let p = geo::Pt { -3, 4 };
    print(p, " ", geo::norm1(p), " ", p.norm1());   // the format hook and UFCS follow the type
}
```

```
(-3,4) 7 7
```

An unqualified name resolves lexically first (locals, type parameters,
nested functions), then in the current declaration's namespace, then in
the global namespace and the builtins. A function name's overload set is
that of the first namespace in this order that declares the name at all.
Sets never merge across namespaces, so a namespaced `hash` overload reaches
the global ones as `::hash(x)`. A UFCS call looks in the namespace of its
receiver's struct or enum type first, past the lexical scopes, where that
namespace declares the name. So `p.norm1()` finds `geo::norm1` for a
`geo::Pt` from any namespace, even one with a `norm1` of its own, and so
does a generic body's `x.norm1()`; a plain call `norm1(p)` does not. The
`format` hook follows the type too: `print(p)` finds `geo::format` for a
`geo::Pt` from anywhere. A global
variable and functions may share a name, since a call resolves past
variables to functions. Namespaces affect only name resolution, type
identity and generated C names. There is no privacy, no re-export and no
nesting of namespaces. Spec §11.1, `docs/design/namespaces.md`.

### Globals and initialization

Globals are initialized before `main`, in declaration order within a
file, with imported files first. So an initializer may use a global from a
module the file imports, and may call functions. It is an error to name a
global before its initializer has run, whether in an earlier initializer
or in a function that such an initializer calls. The whole program runs
inside an implicit outermost scope that owns the globals: they are the
outermost roots for the lifetime rules. A global growable array owns the
bottom of a data stack for the program's life, which makes it a natural
whole-program arena. `const` globals of flat fixed type with compile-time
initializers live in static data, shared and read-only.

```goose
var log: u8[>..];                        // a var with only a type: empty
let NAMES = ["ada", "grace"];            // static data
let FIRST = NAMES.len - 1;               // a named constant
var counts: i64[NAMES.len] = [0; NAMES.len];

fn main() {
    log.append("started");
    counts[FIRST] += 1;
    print(log, " ", NAMES[FIRST], " ", counts);
}
```

```
started grace [0, 1]
```

### `main`

`fn main() { }` in the global namespace of the root file is the entry
point. A `fn main` in an imported file is ignored entirely, so a runnable
file doubles as a library: give it `fn main_x()` and a `fn main() {
main_x(); }` wrapper, and importers call `main_x`. A program's arguments
come through the `os` module (`args()`, `arg_count()`), and `exit(code)`
sets the process exit code.

### `embed_file`

`embed_file("path")` reads a file when the program is *compiled*,
relative to the source file making the call, and gives its bytes as a
`const u8[:]` into static data: a font, a table, a shader. The path is a
string literal or a `let` global holding one, and each distinct file is
read once. `embed_shader` does the same for GLSL, compiling it for the
graphics module ([stdlib.md](stdlib.md#shaders)). Spec §12.

---

## 22. Threads

There is no shared mutable memory. A worker is declared `thread_fn`, and
the compiler compiles it and everything it calls as a **separate program**
with its own data stacks and its own copies of the globals it uses. Those
copies are taken from the spawning program at the moment of the spawn. A
worker that increments a global increments its own copy, and main's copy
keeps its value. So there are no data races, and the language has no
locks, atomics or memory orderings.

| Builtin | Meaning |
|---|---|
| `thread_spawn(worker, args…) -> i64` | starts a worker; its id, never reused |
| `thread_spawn_contained(worker, args…) -> i64` | starts a worker whose aborts end only that worker |
| `thread_wait(id) -> bool` | blocks until that worker has ended and its storage is released; `false` if it was contained and aborted |
| `thread_wait(id, msg) -> bool` | as `thread_wait(id)`, and the first such wait appends a contained worker's abort message to `msg` |
| `hardware_threads() -> i64` | a sensible worker count, never more than the data stacks allow |
| `qput(v) -> bool` | puts `v` on the queue for its type; `false`, at once, if the queue is full |
| `qget<T>() -> T` | takes the next `T`, blocking |
| `qget<T>(ms) -> T, bool` | the same, waiting at most `ms` milliseconds; else `T`'s zero value and `false` |
| `qpoll<T>() -> T, bool` | takes one if there is one; else `T`'s zero value and `false` |
| `qlimit<T>(n)` | lets the queue for `T` hold at most `n` messages |

Values cross between programs through **typed queues**, one per type.
Whatever crosses, including a worker's arguments, must be **flat**: it
holds no references or slices at any depth. This is cheap because a flat
Goose value is contiguous, so a job or a result carries real data,
including its variable-size parts, as a single copy. A `thread_fn`'s
parameters have no defaults.

<!-- check-only -->
```goose
struct Job    { n: i32 }
struct Result { n: i32, digits: i32 }

fn count_digits(n: i32) -> i32 { var d: i32 = 1; var v = n; while v >= 10 { v /= 10; d++; } d }

thread_fn worker() {
    loop {
        let job = qget<Job>();                   // blocks on the queue for Job
        guard job.n >= 0 else { break; }         // -1 means stop
        qput(Result { n: job.n, digits: count_digits(job.n) });
    }
}

fn main() {
    let n = max(1, hardware_threads());
    for i in 8 { qput(Job { n: (1 << (i * 3)) as i32 }); }
    for i in n { qput(Job { n: -1 }); }          // one quit job per worker
    var ids: i64[>..] = [];
    for i in n { ids.push(thread_spawn(worker)); }
    var total = 0;
    for i in 8 { let r = qget<Result>(); total += r.digits; }
    for id in ids { thread_wait(id); }
    print(total, " digits in total");
}
```

A queue has no message limit until `qlimit` gives it one. Once the queue
is full, `qput` returns `false` without waiting, so a server can turn work
away rather than queue it without end. `qput(v);` as a statement ignores
the result. To bound the memory a queue may hold, set its limit to that
memory divided by the size of a typical message.

A worker's return releases everything it owned, whether or not anyone
waits for it. Queued messages outlive their sender. Workers still running
when `main` returns are killed. A global that is not flat is an error in
any function a `thread_fn` reaches, unless it is a `let` whose references
all lead to static data, as a string literal's do. Programs with threads
need a real C compiler (`goose build`), since the in-process TinyCC
backend cannot place thread-local storage. Spec §11.2.

### Contained workers

A failed check or `abort(msg)` in a worker started with
`thread_spawn_contained` ends only that worker, and releases everything a
return would. The message goes to the waiter instead of stderr:
every `thread_wait` for the worker returns `false`, and the first
`thread_wait(id, msg)` appends the message, with its source location, to
`msg`. Later waits get no message, so ask for it once and keep it. A
supervisor can log the message and start another worker:

<!-- check-only -->
```goose
thread_fn serve(attempt: i64) {
    assert(attempt >= 2);                        // a bug the first two hit
    print("attempt ", attempt, " served");
}

fn main() {
    var attempt = 0;
    var why: u8[>..] = [];
    while !thread_wait(thread_spawn_contained(serve, attempt), why) {
        print("attempt ", attempt, " failed: ", why);
        why.clear();
        attempt++;
    }
}
```

Stack overflow, `exit(code)`, and aborts on main's thread still end the
program. A worker that a contained worker starts with `thread_spawn` is
not contained. Files and other resources opened through `extern fn` stay
open. Spec §11.2.

---

## 23. Serialization

A structure whose links are self-relative offsets means the same wherever
it sits in memory, so saving it is a write of its bytes and loading it is
a read. Three builtins do this for any array:

| Builtin | Meaning |
|---|---|
| `to_bytes(a) -> u8[>..]`, `to_bytes(a, out)` | a framed image of the array: a varint byte count, then its element region; fresh, or appended to `out` |
| `bytes_of(a) -> u8[:]` | the element region alone, as a read-only view: nothing copied |
| `from_bytes<T[>..]>(b: u8[:]) -> T[>..], bool` | a **verified** array from an untrusted image; `false` and an empty array if it is not one |

Bytes arriving from outside the program are the only place a reference
could enter without the compiler having proved it, so `from_bytes`
verifies them before they become a value. It checks the framing, every
tag, every length, that varints are canonical, and that every link lands
on an element start of that same image. A corrupt or hostile file gives
`false`, never a wild reference. What it verifies is *safety*, not
integrity: edited data bytes still describe a well-formed structure, and
adding a checksum is up to the program.

```goose
struct Node { word: u8[..8], count: i32, left: Node&<u32>?, right: Node&<u32>? }

fn insert(pool: Node[>..]&, word: u8[:]) {
    if pool.len == 0 { pool.push(Node { word: word, count: 1 }); return; }
    var cur .= pool[0];
    loop {
        let c = compare(word, cur.word[..]);
        if c == 0 { cur.count += 1; return; }
        let next = if c < 0 { cur.left } else { cur.right };
        if next { cur .= next; continue; }
        if c < 0 { cur.left .= pool.push(Node { word: word, count: 1 }); }
        else { cur.right .= pool.push(Node { word: word, count: 1 }); }
        return;
    }
}

fn lookup(pool: Node[>..]&, word: u8[:]) -> i32 {
    guard pool.len > 0 else { return 0; }
    var cur .= pool[0];
    loop {
        let c = compare(word, cur.word[..]);
        if c == 0 { return cur.count; }
        let next = if c < 0 { cur.left } else { cur.right };
        guard next else { return 0; }
        cur .= next;
    }
}

fn main() {
    var index: Node[>..] = [];
    each_word("the quick fox and the lazy dog and the fox") { insert(index, it); };
    var image: u8[>..] = index.to_bytes();          // a framed image of the array
    var loaded, ok = from_bytes<Node[>..]>(image);  // verified before it becomes a value
    print(ok, " ", loaded.len, " nodes, 'the' x", lookup(loaded, "the"), ", ",
          bytes_of(index).len, " bytes");
    image[image.len - 3] = 200;                     // tamper with a link ...
    var bad, bok = from_bytes<Node[>..]>(image);    // ... and it is false and an empty array
    print(bok, " ", bad.len);
}
```

```
true 6 nodes, 'the' x3, 126 bytes
false 0
```

What comes back is an ordinary array: it can be indexed and grown, and
references into it are ordinary references. `from_bytes` also builds
`T[>..<]` and the `T[]` family, and checks an array of index types against
its array. Links into a *field* of an element, and `in pool` links, are
not serializable. A `bytes_of` view is never writable, since bytes written
through it would be links the checker never proved. The byte contract is in
`docs/design/serialization.md`. Spec §3.9, §9.4, §12.

---

## 24. Calling C and being called from C

### `extern fn`

A declaration without a body binds a Goose signature to a C function. A
string after `=`, where the body would go, is the C symbol; without one,
the Goose name is used. The older `extern "cbrt" fn cube_root(...)`, with
the string after `extern`, is deprecated and gives a warning. Such a
declaration is top-level, fully typed and not generic, and joins overload
resolution like any function. A call compiles to a direct C call.

```goose
struct Stats { lo: i32, hi: i32, mean: f64 }

extern fn cube_root(x: f64) -> f64 = "cbrt";    // libm, by its C name
extern fn hypot(x: f64, y: f64) -> f64;
extern fn crc32_bytes(s: const u8[:]) -> u32;   // a slice crosses as { data, len }
extern fn stats_of(xs: i32[:], out: Stats&);    // a struct filled through a pointer
extern fn c_version(out: u8[>..]&);             // a builder C appends to
```

What may cross is exactly what has a plain C shape:

* the integer and float scalars and `bool`;
* any flat fixed-size value (structs, fixed and static-capacity arrays,
  fixed-mode enums), by value as its packed C type;
* `T&` to such a `T`, as a pointer;
* `T[:]` of such a `T`, as a `{ data, len }` struct;
* `T[>..]&` of such a `T`, a builder C may write and append whole elements
  to (`gs_bld_append` appends bytes to a `u8[>..]&`).

Results are one scalar or flat fixed-size value, or nothing. Everything
else (`varint`s, references inside structs, variable or resizable values
by value, optionals) is rejected at the declaration. A parameter is as
writable as its type says, so a read-only argument needs it declared
`const`. C must not keep a borrowed argument beyond the call. The
program's own C goes in a sidecar `foo.build` beside `foo.goose`
(`c_sources = ["impl.c"];`). Headers supplied by `c_headers` in that sidecar
or by `--include header.h` can use Goose's generated types directly; see
[Using Goose types in a C header](packages.md#using-goose-types-in-a-c-header).

### `extern "lib" { … }`: a shared library, bound by name

An `extern` block names a shared library and declares functions bound
from it when the program starts, with no C to compile or link. The
program opens the library and looks the symbols up; a missing library or
symbol is a runtime error that says which name was tried. A member is an
`extern fn` written without the `extern` keyword; its string after `=`,
if any, is its C symbol.

```goose
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32;
    fn SDL_Quit();
    fn SDL_GetTicks64() -> u64;
    fn delay(ms: u32) = "SDL_Delay";
}
```

`"SDL2"` opens `SDL2.dll`, `libSDL2.dylib` or `libSDL2.so`, found by the
loader's search. `"bin/steam_api64"` opens that file, with the same
naming rule applied to its last component, under the program's resource
directory (`resource_dir()`); this is how a program ships a library
beside itself. A dotted name such as `"libSDL2-2.0.so.0"` is taken as
written. A name that differs per platform, or per installation, goes in
the sidecar rather than the source:

<!-- fragment -->
```goose
// myprog.build
dylibs_linux = ["SDL2=libSDL2-2.0.so.0"];     // the soname every distro installs
dylibs_windows = ["SDL2=bin/SDL2.dll"];       // shipped with the program
```

A package's sidecar maps its own names as defaults, and the program's
sidecar overrides them, platform by platform, for the names it mentions.

With no adapter in between, a member declares the library's real
prototype in terms of Goose's shapes:

* **Scalars** are their C types: `i32` is `int`, `u32` `unsigned`, `i64`
  `int64_t`, `f32` `float`, `f64` `double`, `bool` one byte, `size_t`
  `u64`. A C `enum`, including `SDL_bool`, is an `int`, so it is declared
  `i32`, not `bool`. C's `long` is `i64` on Linux and macOS and `i32` on
  Windows, so a function with a `long` needs a per-platform declaration or
  an adapter.
* **Opaque handles**, such as the `SDL_Window *` a library returns and
  takes back, are `u64`: a token Goose passes along and cannot look
  inside.
* **A pointer to something Goose owns** is `T&` for a flat fixed `T`:
  `fn SDL_GetWindowSize(w: u64, x: i32&, y: i32&)`. Where C wants
  `(ptr, count)`, pass `xs[0]` and `xs.len` as two arguments; where it
  wants a NUL-terminated string, append the `\0` and pass the first byte
  by reference:

  ```goose
  extern "SDL2" {
      fn SDL_CreateWindow(title: const u8&, x: i32, y: i32, w: i32, h: i32, flags: u32) -> u64;
  }
  let title = "Goose\0";
  let win = SDL_CreateWindow(title[0], 100, 100, 640, 480, 0);
  ```

* **Structs** cross in Goose's packed layout, which matches C's only when
  the fields already sit where C would place them. `SDL_Rect { int x, y, w,
  h }` matches `struct Rect { x: i32, y: i32, w: i32, h: i32 }`. A `u8`
  before an `i32` needs `pad 3` written where C pads, and a struct whose
  size C rounds up, such as `{ a: i32, b: u8 }`, needs `pad 3` at the end
  to be kept in an array C indexes. A struct with `pad` crosses by
  reference only. Passed by value, its pad is bytes the calling convention
  passes, while C's padding is not passed at all, so
  `{ x: f32, pad 4, y: f64 }` would arrive in the wrong registers. A
  struct passed by reference, `const Rect&`, needs a variable, since a
  literal cannot be a reference target: write
  `let r = Rect { 0, 0, 10, 10 }; SDL_HasIntersection(r, s)` rather than
  putting the literal in the call.
* **Memory the library owns**, such as a `const char *` it returns or an
  array it keeps, also comes back as a `u64`. `ffi::cstr(p)` and
  `ffi::bytes(p, n)` ([stdlib.md](stdlib.md#ffi)) copy what is there into
  Goose storage: `print(ffi::cstr(SDL_GetError()))`.
* **Still an adapter's job**: callbacks (a function pointer the library
  wants) and variadic functions, which cannot be bound at all. Each needs
  a few lines of C in a native part.

Only a member a call reaches is bound, and a library none of whose
members is called is never opened, so a block may declare an interface
wider than any one program uses. A program whose only C is such libraries
runs under the in-process backend with no C toolchain installed. Spec
§7.10, [packages.md](packages.md#the-native-part).

### `export fn`

`export fn` makes a Goose function callable from C: the compiler emits a
C wrapper under the function's name, or the string given. The function
must be top-level and non-generic, with explicit parameter types and at
most one typed result. Defaults, `return … from`, pool parameters and
builders are not supported at the boundary. Scalars and flat fixed-size
values are returned by value, and a `const u8[:]` result comes back as a
pointer plus an extra `int64_t *` length parameter (the bytes are not
NUL-terminated).

```goose
export fn hello_world() -> const u8[:] { return "Hello, world!" }
export "c_add" fn add(a: i32, b: i32) -> i32 { a + b }

fn main() { print(hello_world(), " ", add(20, 22)); }     // runs once, inside goose_init
```

```
Hello, world! 42
```

`goose --standalone --header hello.h -o hello.c hello.goose` builds the C
for a C host. The C has no `main` of its own, and comes with a header
declaring `goose_init(argc, argv)`, which the host calls once before
anything else. That function sets up the runtime and the globals and runs
the Goose `main`, if there is one, so `main` is where a library keeps its
start-up code. Exported functions are not thread-safe: call them from the
thread that called `goose_init`. Spec §7.11.

---

## 25. Builtins

These are the language's own functions. Everything else is in the
standard library, written in Goose under `stdlib/` and documented in
[stdlib.md](stdlib.md).

| Builtin | Meaning | Section |
|---|---|---|
| `print(a, b, …)` | the arguments' text and a newline, as one write | [6](#rendering-text) |
| `str(a, b, …) -> u8[>..]` | the text as a fresh string, built at its destination | [6](#rendering-text) |
| `format(out, a, b, …)` | the text appended to `out`, any growable `u8` array | [6](#rendering-text) |
| `assert(c)` | aborts unless `c`, a `bool` or an optional, which it narrows | [20](#stopping-the-program) |
| `abort(msg)`, `exit(code)` | end the program; neither returns | [20](#stopping-the-program) |
| `copy(x) -> T` | a fresh copy of the stored value `x` names | [4](#copies-are-visible) |
| `default<T>() -> T` | the value a fixed-size `T` has before anything is written to it, declared defaults applied | [8](#field-defaults) |
| `hardware_threads()`, `thread_spawn(worker, args…)`, `thread_spawn_contained(worker, args…)`, `thread_wait(id)`, `thread_wait(id, msg)` | workers | [22](#22-threads) |
| `qput(v)`, `qget<T>()`, `qpoll<T>()`, `qlimit<T>(n)` | the typed queues | [22](#22-threads) |
| `to_bytes(a)`, `to_bytes(a, out)`, `bytes_of(a)`, `from_bytes<T[>..]>(b)` | serialization | [23](#23-serialization) |
| `embed_file(path)`, `embed_shader(…)` | a file's bytes, or a compiled shader, read at compile time | [21](#embed_file) |

The array members, `.len`, `.cap`, `.push`, `.append`, `.pop`, `.pop_n`,
`.resize`, `.clear`, `.index_of`, `.range_index`, `.check_length`, `.move_from` are in
[section 5](#members). The stdlib pool APIs and deprecated builtin operations
`.alloc_index`, `.alloc_ref`, `.free`, `.alloc_slice`, `.realloc_slice`,
`.free_slice` are in [section 14](#14-reusable-pools).
Spec §12.

---

## Appendix A. Keywords and operators

**Keywords**: `import namespace struct enum type fn thread_fn export simd
recursive let var extern const pad reusable if else while for in loop
block guard defer match return break continue as true false null self bool
varint i8 i16 i32 i64 u8 u16 u32 u64 f32 f64`.

**Contextual keywords**, which are ordinary names everywhere else: `from`
(after a `return` value), `upto` (after an unsigned type in a type),
`inline` and `checkfree` (before `fn`, `recursive`, `simd` or
`thread_fn`), `fields` and `types` (in a `const for` header).

**Operators**, tightest first; the precedence is Rust's:

| Level | Operators |
|---|---|
| postfix | `f(…)`, `a[i]`, `a[x..y]`, `.field`, `x as T`, `x as! T` |
| unary | `-`, `!`, `~`, `&` |
| multiplicative | `*`, `/`, `%` |
| additive | `+`, `-` |
| shifts | `<<`, `>>` |
| bitwise and | `&` |
| bitwise xor | `^` |
| bitwise or | `\|` |
| comparison | `<`, `<=`, `>`, `>=` |
| equality | `==`, `!=`, `.==`, `.!=` |
| logical and | `&&` |
| logical or | `\|\|` |

Assignment statements: `=`, `.=`, and the compound form of every binary
operator (`+=`, `<<=`, `&=`, …); `++` and `--` on integer lvalues. A range
`a..b` appears only in slicing brackets, `for` headers and match arms.
Spec §6.1, Appendix D.

## Appendix B. Glossary

* **Balanced call**: a call during which an array is never shorter than
  when the call began, so views taken before it survive it. §13.
* **Candidate**: a variable whose own storage could hold the target of a
  reference read out of a container; such a reference is rooted at its
  candidates. §12.
* **Case function**: one overload per variant type, called with the enum;
  `match` as an overload set. §9.
* **Data stack**: a large address-space reservation with a bump pointer;
  where every non-fixed value lives. §4.
* **Exact root**: a root whose own storage holds the reference's target,
  as opposed to one that only bounds its lifetime. §12.
* **Fixed, variable, resizable**: the size classes: constant size, size
  chosen at construction, size that can change. §4.
* **Flat**: holding no reference, slice or relative reference at any
  depth; what may cross between threads. §4, §22.
* **Function value**: a named function or a block passed as a generic
  parameter; a compile-time entity, never data. §18.
* **Grow-only, grow-shrink**: `T[>..]`, which shrinks only where nothing
  points into it, and `T[>..<]`, whose interior references live in
  variables only. §5, §13.
* **Holder**: a value that contains references: a struct with a reference
  field, an array of slices. §12.
* **Index type, bound type**: `uN in a`, an integer below `a.len`; `uN
  upto a`, one at most `a.len`. §15.
* **Limited array**: `T[..k]` or `T[..]`, a length within an inline
  capacity. §5.
* **Named constant**: an untyped `let` or `const` global with a constant
  initializer, used as a literal is. §2.
* **Narrowing**: the region after an `if`, `while`, `guard` or `assert`
  in which an optional is a plain reference. §10.
* **Pool**: storage for linked or independently reused data. The stdlib's
  `slot_pool` and `slice_pool` bundle a grow-only array with reuse metadata;
  the language's `reusable` modifiers are deprecated. §11, §14.
* **Relative reference**: a link stored as a narrow offset, self-relative
  or measured from a named pool's base. §11.
* **Root**: the variable that bounds the lifetime of a reference's target;
  inferred, never written. §12.
* **Sequential array**: one whose elements vary in size, which can be
  iterated but not indexed. §5.
* **Slot**: a location with a declared type (a field, an element, a
  global, an annotated variable), which says whether what it holds may be
  read-only. §12.
* **Temporary**: a value that lasts for the rest of its statement: a
  literal, a call's result, a `copy`, a control construct's value. §12.
* **Transparent**: the property of a reference that it behaves as its
  target in every value context. §10.
* **Variable mode, fixed mode**: an enum stored as exactly its variant's
  size (`Shape..`), or as a tag plus room for the largest variant
  (`Shape`). §9.
