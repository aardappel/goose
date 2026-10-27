# The Goose Language — Specification (v4 draft)

<details>
<summary>Table of contents</summary>

- [1. Core concepts](#1-core-concepts)
- [2. Lexical structure and syntax style](#2-lexical-structure-and-syntax-style)
- [3. Types](#3-types)
- [4. Values, copying, and assignment](#4-values-copying-and-assignment)
- [5. The resizable/shrink rules](#5-the-resizableshrink-rules)
- [6. Expressions and arithmetic](#6-expressions-and-arithmetic)
- [7. Functions](#7-functions)
- [8. ADTs in use](#8-adts-in-use)
- [9. References, lifetimes, and safety](#9-references-lifetimes-and-safety)
- [10. Compilation model](#10-compilation-model)
- [11. Program structure](#11-program-structure)
- [12. Builtins, the standard library, and what v1 leaves out](#12-builtins-the-standard-library-and-what-v1-leaves-out)
- [Appendix A. Worked examples (informative)](#appendix-a-worked-examples-informative)
- [Appendix B. TODO / open items](#appendix-b-todo--open-items)
- [Appendix C. Representation and calling convention (normative for the C backend)](#appendix-c-representation-and-calling-convention-normative-for-the-c-backend)
- [Appendix D. Grammar sketch (informative)](#appendix-d-grammar-sketch-informative)
- [Appendix E. Implementation notes (informative)](#appendix-e-implementation-notes-informative)

</details>

This working specification defines Goose's syntax, semantics, and compilation
model. It provides the detail needed to implement the language, without the
formal structure of an ISO specification. Appendix B lists unresolved items.
A readable account of the same rules, with an example for each, is
[the language reference](reference.md).

Priorities, in order: **Speed, Safety, Expressiveness.**
Safety here means: no out-of-bounds access, no access to memory at a type
other than the one it was written with, no dangling references into freed or
retyped memory. It does *not* mean Rust-style aliasing control; type-safe
reuse of an element (reading a valid but logically stale value) is permitted
where explicitly noted.

---

## 1. Core concepts

### 1.1 Size classes

Every type belongs to exactly one *size class*:

* **fixed** — byte size is a compile-time constant (after monomorphization).
* **variable** — size is chosen at construction time and never changes
  afterwards.
* **resizable** — size may change after construction. Two flavors:
  *grow-only* and *grow-shrink*.

A compound type's class is the max over its parts (fixed < variable <
resizable), subject to the placement rules in §3.4.

Additionally, a type is **flat** if it contains no references, slices, or
relative references at any depth. Only flat values may cross thread
boundaries (§11.2).

### 1.2 Memory model

A Goose program's memory consists of:

* The **native call stack** (and registers) — holds fixed-size locals and
  temporaries whenever possible, plus variable-size locals via `alloca`-style
  allocation when profitable.
* **N data stacks** — large virtual-address-space reservations (reserved
  up front, committed on use). N is computed statically by the compiler
  (§10.3). Each data stack is a bump pointer. Growth never moves memory;
  references into a data stack are stable for the life of the data beneath
  them.
* **Static data** — string literals and `const` globals with compile-time
  initializers. This read-only storage is shared by all program instances
  (§11.2).

There is no general-purpose heap and no `malloc`. All dynamic allocation is
expressed as values living on data stacks, owned by locals (or globals).
(The runtime itself may allocate internally for thread queues, §11.2.)

Rewinding a data stack makes its storage reusable, while normally keeping
its pages committed. After a large workload, `trim_memory()` from `os`
can ask the operating system to discard unused complete pages of the
calling thread program's stacks. This preserves all live values and the
virtual reservations; subsequent growth remains valid.

### 1.3 The stack invariant

For each data stack:

1. Values are pushed in lifetime order and popped en masse when the owning
   scope exits. Lifetimes on one stack are strictly nested.
2. At most one *resizable* value is live per stack, and for its entire
   lifetime it is the topmost value on that stack. The region from a live
   resizable's first element byte to the stack's current top belongs
   exclusively to its elements; its own length header lives *below* its
   elements or outside the stack entirely (Appendix C). Growth is a pointer
   bump, and no capacity checks are emitted: address space is pre-reserved,
   and guard pages catch pathological overflow and abort safely.
3. Two simultaneously-live resizable values never share a stack. Locals with
   disjoint (sequential) lifetimes reuse stacks.
4. A *variable* value may be buried under later allocations on the same stack
   once its construction completes. While a value is under construction on
   stack S, nothing else may allocate on S, nor may the resizable value it is
   being added to shrink. Nested variable parts are constructed in order, as
   part of the same construction.
5. Fixed-size values are placed on the native stack / in registers whenever
   possible; the length header of an outermost resizable local likewise
   lives in the owning frame, not on the data stack (Appendix C).

The compiler proves all of this statically; there is no runtime bookkeeping
beyond the bump pointers themselves.

---

## 2. Lexical structure and syntax style

The syntax follows C and Rust: `{}` blocks, `//` and `/* */` comments (block comments
nest), semicolon-terminated statements, postfix type annotations (`x: T`).
Identifiers `[A-Za-z_][A-Za-z0-9_]*`. A UTF-8 byte-order mark at the start of
a source file is ignored.

Literals:

* Integer: decimal and `0x` hex; the full unsigned 64-bit range is accepted
  (a value above `i64.max` is a `u64` constant, §6.2). Character
  literals `'a'` are integer constants.
* Float: with `.` and/or a decimal exponent (`1.5`, `2.5e-3`); C99-style hex
  floats with a mandatory binary exponent (`0x1.8p3`). A `.` starts a
  fraction only when a digit follows, so `1..2` lexes as a range.
* String `"..."`, with escapes `\n \t \r \0 \\ \" \' \xNN` (two hex digits).
* Raw string `"""..."""`: no escapes, and the next `"""` ends it. On one
  line, its text is what lies between the delimiters (`"""a "quoted"
  C:\path"""`). For a multiline string, the opening `"""` must end its line. The closing
  `"""` must start its line after any indentation. The content is the lines
  between them, joined by `\n`, with the closing delimiter's indentation
  removed from each line. Every content line must start with that indentation;
  a whitespace-only line may be shorter and then becomes empty. The line
  breaks immediately after the opening delimiter and before the closing
  delimiter are excluded. To include a final newline, add an empty content
  line before the closing delimiter. Source line endings are normalized to
  `\n`.

  ```goose
  let usage = """
      usage: goose [options] file.goose
        -o file.c   write C instead of running it
      """;      // "usage: goose [options] file.goose\n  -o file.c   write C ..."
  ```
* `null` — the empty value of any optional type `T?` (§3.8).
* `self` — inside a struct or variant literal, the value that literal is
  constructing; it exists to initialize non-optional relative-reference
  fields (§3.9, §4.2).

The language is **expression-oriented**: `if`, `match`, `block` and a bare
`{ … }` block are expressions; a block's value is its trailing expression.
Assignment and `++`/`--` are *statements*, not expressions. A trailing
construct that cannot supply a value is a statement rather than the block's
value. Such constructs are an `if` without a final `else` (including an
`else if` chain) or with a branch that cannot supply a value, a `match`
with an arm that cannot, a `loop` none of whose `break`s carries a value
(§6.4), and a block or scope ending in one of those. A function whose
result type is inferred, and a function value's body, may therefore end in
one of them and produce nothing.

**Statement termination.** Expression statements end with `;`. A statement
that *is* a block-ended construct needs no `;`: `if`, `match`, `block`, the
loops, `guard … else { }`, a nested `fn`, or a call with a trailing function
block (§7.6). Such a statement ends at its closing `}`. Operators never
continue it (`foo(1) { it }` followed by `-x;` is two statements), and
using one as an *operand* requires parens
(`let v = (if c { 1 } else { 2 }) + 3;`). A redundant `;` after a block is
harmless. Trailing commas are allowed in every comma-separated bracketed
list: struct/array literals, call arguments, parameter and generic lists,
field lists, match arms.

Type syntax is postfix throughout: `T[k]` array of T, `T&` reference to T,
`T[:]` slice of T, `T?` optional T, `T&<u8>` relative reference,
`T&<u8 in pool>` relative reference measured from a named pool, `Shape..`
variable-mode ADT. The only prefix, `const`, qualifies the first reference
or slice built on the base type: `const T&` and `const T[:]` are a
reference and a slice through which the pointee, or the elements, cannot be
written (§9.5). To qualify an outer reference or slice, parenthesize the
inner type, as in `const (u8[:])&`.

`ns::name` names a declaration of namespace `ns` (§11.1) wherever a
declaration can be named: a type, a call, a global, a `return … from`
target, or a pool. `::name` explicitly names a global declaration. The `::`
token is distinct from `.`, so `image::Shape.Circle` is the variant of a
namespaced enum.

Evaluation order is left-to-right everywhere: operands, arguments, and field
initializers. Named struct literals keep field initializers in construction
order by requiring declaration order (§4.2). Overlapping copies have
memmove semantics.

An earlier by-value operand is read before a later operand executes; a
reference or slice retains its address or view, not a snapshot of its
contents. Indexing and slicing sample the receiver's region and length
before their index or bounds run. Assignment resolves the destination
first; compound assignment also reads its old value before the right-hand
side. Whole-resizable assignment clears before constructing (§4.4).

A grammar sketch and precedence table are in Appendix D.

---

## 3. Types

### 3.1 Scalars

* `i8 i16 i32 i64 u8 u16 u32 u64` — the integer types, two's-complement,
  usable everywhere: fields, array elements, locals, parameters, returns,
  and expression temporaries. Every integer operation computes at its
  operands' exact type and width (§6.2), so a loop over `i32` data runs in
  32-bit registers throughout, which lets backends vectorize it.
  `i64` is the conventional default for indices, sizes, and counts (it is
  what `.len`, integer literals, and the builtins produce); the other integer widths
  are useful for compact data and kernels that require a particular width.
* `varint` — variable-length integer **storage type** (§3.6), the only
  integer type restricted to fields and array elements. Reads decode to
  `i64`, and it is writable only at construction.
* `f32 f64` — IEEE floats, likewise usable everywhere. `f32` arithmetic
  stays 32-bit; `f32` widens to `f64` implicitly (§6.3), never the reverse.
  Every integer converts to either float type implicitly, so where a float
  is expected `2` works as well as `2.0`. Float literals adapt to either
  type, and so does an expression whose float-ness comes only from float
  literals and integers: `n * 0.5` computes in 32 bits where an `f32` is
  expected. Where nothing asks for a type such an expression is an `f64`,
  and so is an untyped `let` bound to one (§6.3).
* `bool` — 1-byte storage, values `true`/`false`. Produced by comparisons;
  required by `if`/`while` conditions (no int-to-bool coercion).

Integer literals are *constants without a committed type*. They adapt to any
integer type that can hold their value: `let x: u8 = 255;` is valid, but `=
256` is a compile error. Without a type constraint, they are `i64`, or `u64`
for values above `i64.max`. A **named constant** is a `let`
or `const` global with no written type whose initializer is an integer
constant expression (`let SZ = 15;`, `let B = SZ * 2;`, `let NEG = -3;`).
It is also a constant without a committed type: each use of it adapts as
its value would if written there, both at a destination (`D { len: SZ }`
with `len: u8`) and as an operand (`b + SZ` with `b: u8` is an 8-bit add).
Where nothing constrains it, it is an `i64`. A global with a written type
(`let SZ: i64 = 15;`), a local `let` and a `var` are values of their type.
(An untyped float `let`, local or global, adapts too, §6.3.) A use that
adapts a named constant relies on the constant keeping its initializer's
value, and a writable reference to it could change that value. So, as for
a compile-time size (§11.1), such a reference is an error, whether it
comes before or after the use. A division by a named constant that is zero
is left to run time, rather than rejected as a constant division by zero
(§6.2). All other conversions are
**implicit when provably value-preserving, and need an explicit cast
otherwise**, with one exception: an integer becomes a float implicitly,
rounding as float arithmetic does (§6.3).

### 3.2 Structs

```goose
struct X { a: i8[3], b: i32, c: i64 = 0 }
```

* Packed by default: a struct of `i8[3]` + `i32` is 7 bytes. Unaligned
  access is assumed cheap; there is no automatic padding.
* `pad n` inserts n bytes explicitly; bare `pad` aligns the *next* real
  field to its scalar storage width, for compatibility with foreign layouts:
  `struct X { a: i8[3], pad, b: i32 }`. Pad bytes are never read and are
  ignored by `==`. Plain references and slices use pointer alignment
  (8 in the C backend); composite fields use alignment 1. No automatic
  tail padding is added. In variable-class layouts (where offsets are dynamic)
  `pad n` still inserts n bytes, but bare `pad` has no defined alignment to
  aim for and inserts nothing.
* Fields are mutable by default; `let` before a field name means the field
  is not assigned after construction (§4.4), and `const f: T` is `let f:
  const T`: its contents are read-only too (§9.5).
* A field may declare a default value (`c: i64 = 0` above, `name: u8[] =
  []`), and constructors may then omit that field (§4.2). A literal ending
  in `..` may also omit a field without a default; that field takes its
  type's default value (for an array, the empty array). Defaults resolve in
  the declaration's namespace and generic environment, can name globals,
  and cannot name the constructor's locals or sibling fields. A used
  default executes at each construction, in field order among explicit
  initializers; it is not a cached value. Its effects obey the same rules
  as an explicit initializer's.
* A default may not lead to a construction that takes the same default
  again, in any instantiation of its type. Such a construction can be in
  the default's own expression (`S { .. }` in
  `struct S { a: i64 = 1, d: i64 = S { .. }.a }`, a `default<T>()`, or a
  slice pool's allocation, §5.4). It can also be reached through the
  defaults those constructions take, through the parameter defaults of the
  calls the default makes (§7.1), or through the blocks and functions
  written in the default. Such a default would run without end. It is an
  error even where a condition would end it at run time, since every
  construction checks the defaults it takes. If the construction that
  takes the default again is in the body of a function declared outside
  the default, which the default calls, it makes that function recursive
  instead (§7.8).
* Layout is declaration order; variable/resizable fields obey §3.4.

Struct and enum declarations introduce **nominal** types. `type Name = T;`
declares an alias. Generic structs: `struct Pair<T> { a: T, b: T }`,
monomorphized like functions.

A generic type's instantiations must be finite, as a function's
specializations must be (§7.7). Each instantiation checks its fields' types
(including a variant's payload) and instantiates the types they name.
Fields that name the declaration again, directly or through other
declarations, with a larger type argument each round would therefore
instantiate without end: `struct P<T> { v: T, next: P<T[]>? }` needs a
`P<T[]>`, which needs a `P<T[][]>`, and so on, whether or not a program
ever follows `next`. As for polymorphic recursion (§7.8), the compiler
rejects an instantiation that would make a chain of more than 16
instantiations of one declaration, each needed by the one before, and the
error names the first few. Fields that name the declaration with the same
arguments (`struct L<T> { v: T, next: L<T>? }`), or with arguments that stop
changing, reach an instantiation that was already made instead. So only a
type that nests one declaration more than 16 deep in its own arguments
reaches the limit.

### 3.3 The array family

All array types share element type `T` and differ in how their size behaves
and is stored. In all of them, the representation is `[metadata][element
data...]`, inline, packed (exact layouts in Appendix C).

| Type | Class | Stored metadata | Notes |
|---|---|---|---|
| `T[k]` | fixed | none | `k` a compile-time constant expression (literals, named constants, a global's fixed `.len`/`.cap`, arithmetic) |
| `T[]` | variable | length (`u32` default) | size chosen at construction |
| `T[u8]` etc. | variable | length of given unsigned int type | explicit length field type: `u8`–`u64` only |
| `T[varint]` | variable | varint length | most compact |
| `T[..k]` | fixed | length only (smallest int type fitting `k`) | capacity `k` is static (constant expression); in-place grow/shrink up to `k` |
| `T[..]` | variable | capacity + length (`u32` default) | capacity chosen at construction; in-place grow/shrink up to it |
| `T[>..]` | resizable (grow-only) | length | element region tops a data stack; grow = bump |
| `T[>..<]` | resizable (grow-shrink) | length | as above, may also shrink |

A size or capacity `k` is fixed at compile time, as is the count `n` of a
fill literal `[v; n]` (§4.2). A name in a size or count is a named constant
(a `let` or `const` global, §11.1), and resolves as any name does,
lexically first. Such an expression may also use `X.len` of a global `X`
whose declaration fixes its length, and `X.cap` of a global whose
declaration fixes its capacity (`const names = ["a", "bc"]; var counts:
i64[names.len];`). A written type `T[k]` fixes the length, and `T[..k]`
the capacity. With no written type, an array, fill or string literal
initializer fixes the length, and an initializer naming another such
global fixes what that global's declaration fixes. Where `X`'s type also
fixes the length or capacity (a written type, or the `T[k]` an array or
fill literal gives), nothing can change it, and `X` may be a `var`. Where
the type does not, as a string literal's `const u8[:]` does not (§3.7), the
length is taken as a named constant's value is: `X` may not be a `var`, and
may not be bound to a writable reference (§11.1), which a `let` holding a
literal's slice never is (§9.5). `X.len` and `X.cap` are `i64` values, as
any `.len` is, and do not adapt as a literal does (§3.1).

A local with the same name as such a global hides the global there and is
not a constant: a parameter, a local variable, or a local around a nested
function or block. So do a type parameter and a nested function of that
name. A size naming any of these is an error, as a match pattern naming
one is (§8.1); a qualified name (`::K`, `ns::K`) reaches the global past
it. A size is also
an error where it names one of a function's or a block's parameters or
type parameters in that function's or block's parameter and result types,
or one of a generic struct's or enum's type parameters in its field
types. A type parameter stands for a type, and there are no size
parameters (`fn f<N>(a: i64[N])` is an error). The names in a type
alias's sizes resolve where the alias is declared.

Indexing is bounds-checked: against `k` for fixed arrays (with checks
statically elided where provable), and against the current length for all
others.

A stored length must hold the actual element count without truncation.
For example, `T[u8]` can hold at most 255 elements. A construction exceeding
its length field's range must be rejected when known statically or abort
when discovered at runtime, including when adapting another array, a call
result or a string. It must never produce an image whose metadata and
element region disagree.

Element restrictions:

* Elements of `T[>..<]`, `T[..k]`, `T[..]` must be **fixed-size** (shrink or
  reuse of storage must never change the type layout of a location).
* Elements of `T[]` and `T[>..]` may be fixed or **variable** (e.g. an array
  of strings, all flat). They may not be resizable.
* An array with variable-size elements is *sequential*: it cannot be indexed
  randomly, only iterated / advanced element-by-element (offsets are data-
  dependent). It also has no `pop`-like operations (the start of the last
  element is not findable). Random access needs an index that the program
  keeps itself: a second array of references to the elements, stored as
  they are pushed (`T&<u32 in pool>`, 4 bytes each, where the array is a
  global pool, §3.9).

Growth operations (`push`, `append`, …) exist only on resizable arrays and on
limited arrays (`[..k]`, `[..]`) up to capacity (exceeding capacity aborts).
Shrink operations (`pop`, `pop_n`, `resize` downward, `clear`) exist on
limited arrays anywhere, on `[>..<]` wherever no live variable refers into it
(§5.2), and on a grow-only `[>..]` exactly where the compiler can see that
no reference or slice into it is live (§5.1). They abort when they would
shrink below empty (`pop` on an empty array, `pop_n` of more elements than
the array holds, `resize` to a negative length).

Built-in members, which exist as the rules above allow:

* `.len` (always), which returns `i64`.
* `.cap` (limited arrays).
* `.push(v)`, which returns a reference to the new element on resizable and
  limited arrays. This is the idiomatic way to link up just-built data.
* `.append(src)`, where src is an array or slice of the element type. Its
  elements are copied: the references they hold must outlive the array
  (§9.2), and none may be self-relative (§3.9). An array literal given as
  src is instead built as a run of such elements (§4.2).
* `.pop()`.
* `.pop_n(n)`, which drops the last `n` elements at once and returns
  nothing. A negative `n` aborts.
* `.resize(n, v)`, which grows with fill value `v` (copied into every slot
  added) or shrinks.
* `.resize(n)`, which only shrinks.
* `.clear()`.
* `.index_of(r) -> i64` (fixed, limited and resizable arrays of fixed-size
  elements): the index of the element `r` refers to,
  `(addr − base) / elemsize`. `r` must be rooted at the array *exactly*
  (§9.2), so the division is exact and the result is in range without a
  runtime check. A root names a variable, not a part of it. So where the
  array is a field or an element of what its root holds, the reference
  counts as rooted at the array only if that storage holds the element type
  nowhere else: not in another field or element, nor inside an element. A
  parameter's storage is checked at each call (§10.2). A reference rooted
  elsewhere is a compile error.

Growth always supplies element values, so no operation can expose
uninitialized slots (§5.3). Per UFCS these are ordinary functions:
`a.push(v)` is `push(a, v)`.

### 3.4 Placement rules (what may contain what)

* **fixed** values: anywhere (locals, globals, fields at any position, array
  elements, ADT payloads, params, returns).
* **variable** values: locals/globals, params, returns, ADT payloads, array
  elements (making the array sequential), and struct fields at *any* position.
  A field placed after a variable-size field is reached by dynamic offset
  computation (fine when iterating, costly for random access; this is a
  performance note, not a rule).
* **resizable** values: locals/globals, by-value params, returns, and the
  **final field** of a struct. This applies recursively: a struct ending in
  a resizable is itself resizable and may only appear where resizables may.
  A resizable may also be the payload of a variable ADT (making that ADT
  resizable, with the same tail rules). It is never an array element.
* **zero-size** values, although they are fixed: zero-length arrays (`T[0]`,
  and `T[..0]` of capacity 0) and variant types with no fields
  (`Shape.Point`, or a variant with only pads that insert nothing, §3.2),
  whose payload takes no bytes behind its ADT's tag. They appear only as
  values of their own (locals/globals, params, returns, and behind
  references), never as a struct field, a payload field, or the element
  type of an array or slice (including one an array literal infers). C has
  no empty arrays or structs, so the C backend gives each zero-size value an
  element slot or a byte (Appendix C.2). Inside another value, that slot or
  byte would take bytes that the value's layout does not count.

At most one resizable per struct (the tail). (Future extension: two
resizables with memmove-on-insert semantics, opt-in.)

No type contains itself by value, either directly or through what it
contains (fields, payloads in either ADT mode, variant types, array
elements), since it would then be part of itself. A type may refer to
itself, since a reference or a slice is fixed-size whatever it points at:
an ADT's payload may link to its own ADT or one of its variants, and a type
behind a reference may contain the type holding the reference
(`enum List { Nil, Cons { v: i64, next: List? } }`, or `struct A { b: B? }`
with `struct B { a: A }`).

### 3.5 Algebraic data types

```goose
enum Shape {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Point,
}
```

ADTs are the only dynamic-polymorphism mechanism in the language: there is
no inheritance, and there are no traits or vtables. An ADT is represented
as an integer tag (the smallest storage type that fits the variant count,
default `u8`) followed by the payload.

Every ADT *type* can be used in two modes, chosen at the use site by the
containing declaration:

* **fixed mode** (`x: Shape`) — usable only if all payloads are fixed-size
  and hold no self-relative references (§3.9). Size = tag + max payload
  size, padded to the largest variant (padding never read, ignored by
  `==`). A fixed-mode value may be **overwritten in place by a different
  variant**, but interior references into its payload may never be
  created. Its payloads therefore bind only by value (§8.1, §8.2), and a
  copy does not keep self-relative references (`in pool` references copy
  fine).
* **variable mode** (`x: Shape..`) — size = tag + the actual variant's
  payload. The class is variable (or resizable, if the stored variant's
  payload contains a resizable tail). A variable-mode value may have
  interior references into the payload, but its variant may **never be
  replaced in place**, except by a whole assignment of a resizable ADT
  (§4.4). A resizable ADT therefore binds its payloads only by value, as
  fixed mode does (§8.1, §8.2). So a payload of a resizable ADT that is not
  itself resizable holds no self-relative references, since a copy does
  not keep those (§3.9); plain or `in pool` references copy fine.

Allowing either variant replacement or interior references, but not both,
preserves the soundness of existential types (Grossman, "Existential Types for Imperative
Languages").

Payload-less ADTs are ordinary C-like enums and are fixed, 1 byte by default.
Variant types (`Shape.Circle`) are themselves nominal struct-like types
(§8.2).

### 3.6 varint fields

`varint` is **LEB128**: little-endian base-128, 7 payload bits per byte,
high bit = continuation; 1–10 bytes; full 64-bit range. Stored encodings
are canonical: the shortest encoding of the unsigned or zigzag-transformed
value. A verified byte image must preserve that invariant (§12).
LEB was chosen because this type is optimized for values that are usually very small but
have occasional outliers, where it outperformed other formats (measurements
summarized in Appendix B, Resolved, item 1).

There is one `varint` type, with two encodings that depend on its position.
The difference is user-visible whenever Goose data is serialized directly:

* As an array **length field type** (`T[varint]`): unsigned ULEB128.
* Everywhere else (struct fields, ADT payloads, self-relative offsets):
  **signed**, zigzag-transformed (`(v << 1) ^ (v >> 63)`), so small
  negatives are as compact as small positives. A pool-relative offset (§3.9)
  is unsigned, so it is ULEB128 like a length. The transform sits on the
  value path, not the length/advance path, so it costs no decode latency.

Because the two encodings differ, varint values are never copied byte-wise
between contexts: any varint-to-varint construction (e.g. a struct varint
field initialized from `arr.len`) goes through `i64`: decode, then
re-encode.

Restrictions: a `varint` field/element is written only at construction of
its containing value; changing it means reconstructing the container. Reads
decode to `i64`; construction stores accept any integer type except `u64`
(a varint holds exactly the `i64` value range). A struct containing
`varint` fields is variable-class.
References to `varint` fields are always read-only (§3.8). Such a field is
still storage of type `varint`. A `varint&` destination binds it without
`&`, as a reference destination binds any lvalue of its pointee type
(§4.1). An `i64&` does not bind it, although it reads as an `i64`. That
`i64` is not storage, so a format overload taking an `i64` by reference is
given it in a read-only temporary (§3.7).

### 3.7 Strings

There is no built-in string type. A "string" is any array-family type with
element `u8`: `u8[]` (flat string with a fixed length), `u8[varint]`
(compact), `u8[>..]` (string builder), etc. String literals are static
constant `u8` data. They are implicitly copyable into any of these
representations and usable directly as slices of type `const u8[:]`
(§9.5). That type makes them read-only, so a literal is stored only in a
slot declared `const`. No encoding is enforced; UTF-8 is a library-level
convention.

**Text.** Every scalar, `bool`, and `u8` array or slice has a text form:

* integers in decimal;
* floats in the shortest form that reads back to the same value of their own
  type. An `f32` 0.1 prints as `0.1`, without the extra digits of its widened
  `f64` value. Whole numbers get `.0` so they still read as floats (`1.0`,
  `-0.0`, `2147483600.0`); exponent forms such as `1e+20` keep that form.
  Infinities print as `inf` and `-inf`, and every NaN as `nan`;
* `true`/`false`;
* a `u8` array's bytes as they are.

Three builtins produce it. Each takes any number of arguments and inserts
nothing between them:

* `print(a, b, …)` writes the forms to standard output followed by a
  newline, as one write, so lines printed by different threads (§11) never
  interleave.
* `format(out, a, b, …)` appends them to `out`, which can be any growable
  `u8` array.
* `str(a, b, …)` builds a fresh `u8[>..]` of them. Like any resizable
  result, it is constructed at its destination (§7.3), so
  `words.push(str("item", i))` writes directly into the element.

A character literal is an integer (§2), so `str('x')` is `"120"`; a byte is
appended with `push`.

Every other value type renders structurally, so `print(v)` shows any value:
an array or slice of other elements as `[1, 2, 3]`, a struct or ADT variant
as its positional literal with the type name (`vec3<f32> { 1.0, 2.0, 3.0 }`,
`Circle { 1.5 }`, a payload-less variant as `Shape.Dot`), a reference as
its pointee and a null optional as `null`. A `u8` array nested inside an
aggregate is quoted and escaped. References are followed as deep as they
go, including into values of the same type as the one holding them. A
value whose references lead back into itself therefore never finishes
rendering: its thread runs out of native stack, which is a safe abort
(§9.3).

A user overload `fn format(out: u8[>..]&, v: T)` (taking `T` by value or by
reference) is used instead to render a `T`, wherever a `T` occurs in an
argument, so a type can choose its own text once for all three builtins.
A `T` that is not fixed-size is never copied implicitly (§4.1), so its
overload takes it by reference. An overload that takes the `T` by reference is given the storage the `T`
lies in, as a reference parameter is (§4.1): the variable, field or element
holding it, or what a reference argument names, and it is as writable as
that storage is (§9.5). A `T` that is not storage lies in a read-only
temporary. That is the case for a call's, an operator's or a cast's result,
a literal, a `.len` or `.cap`, and the value of an `if`, `match` or block,
which is a copy of what the branch taken gives, even where that names a
variable (§4.1). The overload is looked up in the namespace `T` is
declared in, then globally (§11.1): rendering follows the type, not the
namespace of the code printing it. A type that reaches itself through
references renders structurally only where no overload renders any part of
it, at any depth. Otherwise the type needs an overload of its own, which may
render its parts through their overloads.

The three builtins evaluate and render their arguments in order, evaluating
each one just before rendering its text. They call the overload in the
middle of rendering the argument that holds the `T`. So neither the
variables named by the arguments after that one, nor the rest of that
argument, may refer into what the overload shrinks (§5.1).

An array's elements, and a variable-mode ADT's payload (§3.5), are rendered
where they lie, using the count and the variant read when their rendering
begins. So an overload called for one of their parts may not shrink the
storage they lie in. Such a shrink either frees the elements or, as a whole
assignment does (§4.4), rebuilds every variable-size part of the value
holding them. A reference to a whole resizable value is no exception.

A slice's elements are rendered from the view the slice had when their
rendering began, and a value behind a reference from where the reference
pointed at that time. An overload that re-points the slice or rebinds the
reference in the meantime does not change how the rest is rendered (§2). A
fixed-mode ADT's payload is rendered from a copy taken once its tag is
read, since an overload may overwrite the ADT with another variant and
nothing may refer into its payload (§3.5). The rest of the payload is
therefore still the variant the rendering began with, and an overload that
takes a part of it by reference is given that part of the copy, read-only.
The payload renders as its variant's literal even where there is an
overload for the variant type; that overload renders values of the variant
type.

### 3.8 References

`T&` is a reference to a `T`. It is one machine address, with no pointer
arithmetic and no casts, and it never dangles.

**References are transparent** (like C++ references): an expression that
denotes a reference behaves as its pointee in every value context. Reads,
arithmetic, comparisons (`r1 == r2` compares the pointees), passing to a
by-value parameter, and `print(r)` all operate on the target. There is no
dereference operator; the load is implicit (`copy(r)` names a copy of the
pointee where §4.1 asks for one).

* Created with `&lvalue`. Lvalues are: local/global names, fields, indexed
  elements, dereferenced references, and (for reference creation) variable
  elements reached by iteration. `&` of an rvalue (temporary) is an error.
  `&` of a location that itself holds a reference yields the *stored*
  reference (there are no references to references).
* Writes: `r = v` (and `r += v`, `r++`, …) write the pointee, subject to
  writability (§9.5) and the target's own rules. A `const T&` does not allow
  writes to its pointee.
* Rebinding: the special assignment `r .= &x` updates the reference *value*
  itself, rather than writing through it. `.=` applies to any
  reference-typed location: variables (subject to their `let`/`var`),
  fields and elements. On non-reference locations `.=` is an error, except
  in generic code, where it assigns as `=` does. Generic code here means the
  body of a function with type parameters or untyped parameters, and the
  nested functions and function values written in one. Generic code thus
  stores a whole value of a type parameter's type with `.=`: where the
  parameter is bound to a reference type, `.=` moves the reference itself,
  while `=` would write through it. It keeps such a value in a local of
  that type, where an inferred `let` would copy the pointee (below). For
  example, std's `swap_at` is
  `let t: T = xs[i]; xs[i] .= xs[j]; xs[j] .= t;`. It swaps the elements of
  an array of references, including relative ones (the local holds the
  plain reference, §3.9), as it swaps those of any other array. The
  declaration form `let r .= e;` / `var r .= e;` binds `r` to `e` by
  reference whatever `e` is: an lvalue of any size class
  (`var cur .= pool[0];` names the element, without `&`), a
  reference-returning call (`let e .= pool.push(v);`), or a narrowed
  optional. This is how a reference variable is declared without writing
  its type: the `.=` says the binding is a reference, as it does in the
  rebind. `let` on a reference variable means it does not rebind, not that
  its pointee is const: `let r .= xs[i]; r = 5;` writes the element (§9.5).
* Identity: `r1 .== r2` and `r1 .!= r2` compare the references themselves,
  that is, whether they have the same address (`==` compares the pointees).
  Both operands are references to the same pointee type, or `null` for an
  optional. Storage (a variable, field or element) is taken by reference,
  as `.=` takes it, so `n .== pool[head]` asks whether `n` names that
  element.
* Binding contexts keep the reference rather than loading through it. An
  initializer, argument or field whose *declared type* is a reference type
  binds the reference value, and binds an lvalue of the pointee type by
  reference without `&` (§4.1; writing the `&` warns). Where a *variable's*
  type is inferred (`let x = r;`), a reference to a fixed-size value decays
  to a copy of the pointee. The exception is an explicit `&lvalue`
  initializer, which infers the reference type; a construct choosing one,
  such as `if c { &a } else { &b }`, does not, and its value is a copy
  (§4.1). A non-fixed lvalue, by contrast, binds by reference
  (`let w = words[0];` names the element), and so does a reference to a
  non-fixed value (`let a = c.args;`, with `args: u8[>..]&`, binds what the
  field refers to, as `.=` does). To bind a reference-returning call to a
  fixed-size value, use `.=` (`let e .= pool.push(v);`) or annotate
  (`let e: T& = pool.push(v);`). A value holding self-relative references
  cannot be copied at all (§3.9). A function whose result type is inferred
  takes it from a return in the same way (§7.1). An untyped parameter is an
  anonymous type variable and binds the argument's exact type, reference or
  not, exactly as an explicit `<T>` does (§7.7). For a fixed value, `f(&x)`
  hands `f` a reference and `f(x)` a copy; a non-fixed lvalue is a
  reference either way.
* References to `varint` fields are always read-only (varints are written
  only at construction, §3.6).

**Optionals.** References are non-nullable by default. `T?` is an *optional
T*: it is represented as a nullable reference to T (null = address 0, so it
takes no extra space), with all reference semantics and restrictions
(roots, lifetimes, writability). This composes with any type: pass `i64?`
for an optional integer. Applied to a type that is already a reference,
`?` makes that reference nullable (`T&?` ≡ `T?`). The same holds for a type
argument that is a reference: `T?` of `Node&` is `Node?`, and of
`Node&<u32 in pool>` it is the optional relative reference. `T&` of a
reference type argument is that reference itself, just as `&` of a
location holding a reference gives the stored reference (loaded, if
relative). The literal `null` is the empty value of any optional type; an
optional struct field with no declared default defaults to null. Optionals
are *not* transparent: access requires narrowing via
`if`/`while`/`guard`/`assert(r)`/`== null`/`!= null` tests. Flow typing:
inside the guarded region the optional *is* a `T&` in every respect. It is
transparent, can be written through, and is bound by `.=`
(`if n { let m .= n; … }`). A `while` condition narrows its body in the
same way even when the body rebinds the variable, since the test runs
again before each iteration (`while n { …; n .= n.next; }`). Elsewhere,
rebinding the variable ends the narrowing. `o .= &x` / `o .= null` rebind
an optional. A rebind to a plain reference narrows it, and a rebind to
anything that may be null un-narrows it.

**What references may point to.** Anything except the interior of a
fixed-mode ADT payload (§3.5). A reference into a grow-shrink array `[>..<]`
is held only in a variable, and must no longer be live when the array next
shrinks (§5.2).

References into a grow-only resizable `[>..]` remain valid for as long as
they can be named: grown memory never moves, and the array shrinks only
where the compiler can see that no such reference or slice is live (§5.1).
References into limited arrays `[..k]`/`[..]` are also allowed and stay
type-valid across pop/push reuse (§5.3).

### 3.9 Relative references

A relative reference is a storage form of reference, stored as a narrow
offset instead of an address. It is written `T&<u8>`, `T&<u16>`,
`T&<u32>`, `T&<u64>` or `T&<varint>`; widths are written as unsigned types
(or `varint`), like array length field types. It comes in two forms,
differing only in what the offset is measured from: **self-relative**, the
default, measured from the offset field itself; and **pool-relative**,
written `T&<u32 in pool>`, measured from a named pool's base.

Both forms can be made optional, written `T&<u8>?`, which reserves offset 0
for null. A pool-relative offset is biased by one. A self-relative field can
share its address with an enclosing value, so a non-null store must not
encode as the optional's null. Such a store aborts, and a collision that is
known statically may be rejected during compilation. A non-optional
self-relative reference has no null sentinel: offset 0 denotes the address
of its own field, as with `self` in a value's first field. Null is
therefore representable in any relative location, whatever the location's
root, and a value known to be null stores into an optional relative slot
without a root check of its own. The values known to be null are:

* the `null` literal;
* a variable bound to nothing but null so far (§9.5), a parameter given
  null, and a call whose every `return` gives null, none of which has a
  root;
* an optional reference whose only root is static data, where static data
  holds nothing but null: a writable one (§9.5), or one whose pointee type
  no literal supplies, since a read-only `u8` reference may point into a
  string literal.

As one alternative of a merged value (§9.2), a null names no array:
`if c { null } else { &pool[i] }` is a link into `pool`. So a
sentinel-ended chain does not force plain links on the whole structure.

**Self-relative.** Constrained to point within the *same enclosing
array/pool* as the location storing it.

* Stored as a signed offset of the given width (`varint` offsets use the
  signed zigzag encoding, §3.6). Loading one yields an ordinary `T&` (base =
  address of the offset field itself). Storing one requires the compiler to
  see that both the reference and the destination location derive from the
  same root array: the two roots must be the same variable *and* both exact
  (§9.2), since a root that only bounds a lifetime does not say which array
  the offset would span. A reference read out of a container qualifies
  exactly when the read-back rule of §9.5 names a single candidate.
  Otherwise, the error names the candidates the rule could not choose
  between. The offset is range-checked at the store (aborting on overflow
  of the width). Both ends lie in one root array, so the offset cannot
  exceed that array's span. The check therefore exists only where a root
  can be wider than the width's signed range. A root on a data stack spans
  at most the reservation (§10.4), so a reservation of 2 GB or less needs no
  check for `u32`, and one of 32 KB or less needs none for `u16`.
  Varint-width relative references are written only at construction, like
  varint fields (re-encoding could change the byte length); fixed widths may
  be re-stored with `.=`/`=`.
* Copying a *value that contains* self-relative references is a compile
  error, since the copied offsets would still be measured from the source
  location. This prohibits assignment from an lvalue, by-value arguments, by-value match
  binders or case function parameters (§8.1, §8.2), and element copies. It
  also prohibits `append` except with an array literal, by-value inputs to a
  `format` overload (§3.7), `resize` fill values, and `copy(x)`. A `resize`
  fill value is copied into every added slot, even when it is a literal.

  What a reference names is copied in the same way wherever a
  value destination meets the reference (§3.8, §4.1). For an `f` returning
  `Node&`, `let n = f();` copies the node out of the array its links point
  within. So do a `var`, an annotated declaration, a by-value argument, a
  return (including one whose result type is inferred), an assignment, a
  literal's field or element, a pushed element, and each reference result
  of a multi-value binding, when given such a reference. Where a
  declaration was likely meant to bind a reference, the error suggests the
  binding `let n .= f();` (§3.8). Nor may a link be read out of a copy
  that a temporary holds, out of a popped element, or out of a control
  construct's value, which copies the value of its branch even where that
  names storage (§4.1). The fields or elements of such a value that hold
  self-relative references are not read, sliced or iterated; a `match`
  does not bind its payload by reference; and `print`, which renders a
  reference as its pointee, does not render the value. A scalar field of
  such a value reads as usual (`a.pop().v`).

  Construct such values in
  place, with literals. A field that a literal's `..` fills takes its
  type's default, whose links are null and whose limited arrays are empty,
  and is not a copy. Bind their match payloads by reference, and have case
  functions take their variants by reference and `format` overloads take
  them by reference. An ADT whose payloads hold self-relative references is
  used in variable mode, whose payloads bind by reference where the ADT is
  not resizable (§3.5).

  A call's by-value result is not a copy either,
  since it is built where it is received (§4.3, §7.3); this includes a
  fixed-size aggregate and a trailing block's fresh result. Links to
  existing elements require the receiving storage and those elements to
  share an exact root, checked at every call, including forwarded and
  recursive calls. An argument slot, discarded result, or operand temporary
  does not inherit the enclosing result's root. A result relying on that
  root cannot be a resize fill, and cannot undergo a representation
  conversion that stages or moves its bytes; a variant may be constructed
  behind its ADT's tag.

  A function's named result (§7.3) is not a copy
  either: it is a local that every return of the function gives, built at
  the destination from its declaration. A named result that is a struct
  ending in a resizable needs the fields before its resizable tail to be
  free of self-relative references, and of anything that a self-relative
  reference in the tail points at, since those fields reach the caller as
  a copy (C.3). Nor is a default value (a literal's `..`, `default<T>()`) a
  copy: its links are nulls, or `self` where a field's default says so.
  (TODO 16: track the region a relative reference ranges over, so provably
  whole-region copies can be allowed.)
* Because they are position-independent, structures linked by self-relative
  references can be serialized or mapped directly: `to_bytes(a)` writes an
  array's image out, `bytes_of(a)` views it without copying, and
  `from_bytes<T[>..]>(bytes)` verifies one back in (§12,
  `design/serialization.md`). Without verification, the arriving bytes
  would be the one place where a reference could enter the program
  unproven (§9.4). Verification is also why a `bytes_of` view is never
  writable: bytes written through it would be links the checker never
  proved.

**Pool-relative (`in pool`).** `pool` names a *global* `var` (or `reusable
var`) of a grow-only resizable type (`[>..]`) whose storage can hold a `T`
by value: as an element, or as a by-value field of an element,
transitively (the candidate notion of §9.5). Anything else is a compile
error. For a local or parameter pool, the error points to the
self-relative form, since the pool is named where the field is declared,
and a local's name means nothing there.

* The pool is part of the type's identity: `Node&<u32 in pool>` is neither
  `Node&<u32>` nor `Node&<u32 in spare>`. Loading either form yields an
  ordinary `Node&`, and storing re-encodes.
* The stored value is `(target − base(pool)) + 1`, unsigned, so 0 is the
  null of the optional form and a `uN` width covers a pool of up to
  2^N − 1 bytes; `varint` is the unsigned LEB form (§3.6). Loading is
  `base(pool) + stored − 1`, and the result is a `T&` rooted at `pool`
  *exactly* (§9.2) whatever it was read out of.
* Storing requires the value's root to be exactly `pool`. Otherwise the
  error names the value's root or, for an inexact read-back, the candidates
  §9.5 could not choose between. The destination may be anywhere (another
  global, a local, a parameter's pointee), since the offset does not depend
  on where it is stored. This permits links between arrays: a slot array can
  hold 4-byte links into the pool. Since the pool is global, its lifetime
  satisfies the §9.2 store rule.
* A width bounds the *pool*, not the distance between the two ends, so the
  store is range-checked exactly where the pool's reservation can exceed it
  (§10.4): `u32` at a 2 GB reservation needs no check, and `u16` at any
  realistic reservation does. `base(pool)` never moves: a grow-only
  global's element region starts at its stack's reservation, and growth
  only bumps the top.
* A value whose relative fields are *all* `in pool` copies like any other
  (assignment, by-value arguments, element copies, `pop`): its offsets do
  not depend on where it sits. A value mixing both forms does not.
* Whether a *parameter* points into `pool` is settled at each call site, so
  a function that relinks (`fn front(head: Node&, n: Node&)`) is specialized
  per pool like every other root class (§10.2) and needs nothing passed to
  it.

**Values.** A relative reference is storage (a field, an element, or a
variable or parameter whose declaration writes the type or an alias of
it), never a value. Loading one yields the plain reference, and the plain
reference is what anything else receives. A function's result may not be
declared relative; declare the plain `T&` or `T?` instead, which a
relative slot receiving it encodes. A result that a type argument makes
relative, such as the `-> T` of `fn get<T>(a: T[>..]&, i: i64)` over an
array of links, is the plain reference, as indexing gives it. So is a
variable or parameter that a type argument makes relative: `let t: T` and
`v: T` over an array of links hold the plain reference. This is how generic
code that moves elements through a `T` local (std's `swap_at`, §3.8) moves
links as it moves plain references. Each store back into an element
encodes the reference again, so a self-relative reference must still point
within that element's root, and an `in pool` reference into its pool. A
field or element of type `T` (`T[4]`, `struct Box<T> { x: T }`) is a link.
`default<T>()` of an optional relative reference is likewise the plain
null, at any width.

**`self`.** A non-optional relative reference has no null, so a value whose
links point back at itself (the sentinel of a circular list, the first node
of a pool) could not be constructed at all: there is nothing yet for the
links to point at. `self`, written as the entire initializer of such a
field in a struct or variant literal, denotes the value that literal is
constructing: `Node { key: -1, prev: self, next: self }`. It is legal only
there, and only when the field's type is a non-optional relative reference
whose pointee is the type of the value the literal constructs (for a
variant literal in either enum mode, §3.5, that is the enum, tag included).
`self` in a nested literal names the literal it is written in, never an
enclosing one. That type must not be resizable: an offset alone cannot
reach a resizable value's header. For the same reason, the same-root rule
keeps every *other* relative reference away from a resizable value. In a
self-relative field, the stored offset is minus the field's own byte offset
within the value, so it is the only relative reference whose meaning does
not depend on where the value is stored. In an `in pool` field, the stored
offset is the value's own offset in the pool, which only a literal being
built *inside* `pool` has: in a `push` or `append`, an
`alloc_index`/`alloc_ref`, or an element store into the pool. Anywhere
else, `self` in an `in pool` field is a compile error. This allows a
structure to use non-optional links throughout. A sentinel eliminates the
need for null links, and loading a non-optional relative reference requires
only an addition, without a null test.

**Which form.** Use self-relative references for position-independent
blobs, such as a compact tree that is saved, mapped or moved whole
(single-byte links to nearby nodes, see A.2), and for structures stored in
a local or parameter pool, which has no name to write. Use `in pool` for
relink-heavy structures in a global pool, where a store is a subtraction
from a base already in a register rather than from the field's own
address. Use it also for links *into* that pool from other arrays, which
self-relative references cannot express at all, and wherever the offset is
wanted as an index, since `&pool[i]` encodes as `i * sizeof(T) + 1` and
`index_of` (§3.3) reads it back.

### 3.10 Slices

`T[:]` is a reference plus an element count, referring to a contiguous run
of `T`s inside some root. Slices are the universal "process a range"
parameter type, unifying all array representations. Like references,
slices carry a root and participate fully in the lifetime system (§9),
which prevents dangling slices. They are the intended *read* path, and
mutation idiomatically goes through references, but writes through a slice
are legal when its provenance is writable (§9.5).

* Created by slicing any array-family value or slice: `a[x..y]` (x inclusive,
  y exclusive; omit for 0 / len; `^k` means "len − k"), or allocated from a
  slice pool (§5.4). An array, or a reference to an array, that reaches a
  destination whose type is a slice of its element type implicitly becomes
  a whole-array slice of that array itself, as `a[..]` would. This conversion
  applies to parameters, though an exact-type overload takes
  precedence. It also applies to annotated `let`/`var` declarations and
  globals; assignments to slice variables, fields, or elements; struct or
  variant literal fields and field defaults; and array literal elements. Other
  destinations include `push`, `return`, a body's final expression, and a
  branch whose enclosing construct has a slice value (§6.4). The slice is
  `const` where the array is read-only (§9.5), and the
  lifetime rules treat it as any other slice (§9.2):
  `let s: const u8[:] = item.name;` views `item.name`, while
  `let s: const u8[:] = make();` binds a view of a temporary and is an
  error. (An array literal of variable-size elements is not such an array:
  it has no temporary to be sliced, §4.2.)
* Slices of fixed-element arrays index and iterate, bounds-checked against
  the slice's own length. Slices never grow; shrinking a slice (re-slicing)
  is always safe.
* Slices of variable-element arrays iterate only (no indexing); the count is
  an element count.
* Slices obey the same restrictions as interior references (§3.8): a slice
  of a grow-shrink array is held only by variables and must no longer be
  live when the array next shrinks (§5.2).
* A slice of a grow-only resizable taken before growth remains valid (it
  does not include the new elements).

### 3.11 Index and bound types

An *index type* `uN in a` is an unsigned integer that is below `a.len`
wherever it can be observed. A *bound type* `uN upto a` is an unsigned
integer that is at most `a.len`: the end of a range. Indexing `a` with an
index of `a` needs no check, and an index stored in data keeps that
guarantee wherever it is copied to.
This is how an index loaded out of one array (a graph's edge targets, a
mesh's index buffer) is checked once, where it enters the program, rather
than wherever it is used (`docs/design/index_types.md`).

```goose
struct Node { lo: u32 upto edges, hi: u32 upto edges }
var nodes: Node[>..] = [];
type NodeId = u32 in nodes;
var edges: NodeId[>..] = [];
```

* `uN` is `u8`, `u16`, `u32` or `u64`. `a` names a global `var` (or
  `reusable var`) of a grow-only type `T[>..]` with fixed-size elements,
  resolved where the type is written, as a pool's name is (§3.9). Anything
  else is a compile error: a local or parameter has no name where a type
  is declared, any other array kind can shrink, and a sequential array
  cannot be indexed.
* The array is part of the type's identity: `u32 in nodes` is neither
  `u32`, `u16 in nodes`, `u32 upto nodes` nor `u32 in edges`. The value is
  the plain integer; the C type is that of `uN`.
* An index type takes no postfix of its own: an array of them is
  `(u32 in nodes)[>..]`, or `NodeId[>..]` through an alias. A `for`
  binder's written type is followed by the loop's `in`, so an index type
  written there is parenthesized too: `for w: (u32 in nodes) in ...`.
* **Making one.** `x as (uN in a)`, for `x` of any integer type, aborts in
  every build unless `0 <= x < a.len`, with the error `a[x]` would give. It
  is a bounds check, and the bounds-check pass elides it where it would
  elide `a[x]` (§10.5). `x as (uN upto a)` is the same with
  `x <= a.len`. A value that `uN` cannot hold then converts as any `as`
  does (§6.3), and what it wraps to is still in range. `as!` cannot make an
  index or bound type. Converting to a wider width of the same array's type
  is implicit (`u16 in a` to `u32 in a`), and so is converting an index
  type to the bound type of its array; the other directions take an `as`,
  which checks. A constant is never of an index type and never adapts to
  one, since nothing about `a.len` is known at compile time.
* **Loops.** `for i in lo..hi` whose bounds are both of index or bound types
  of one array `a` gives `i` the index type of `a` at their joined width,
  since `lo <= i < hi <= a.len`. A count loop `for i in n` with n of an
  index or bound type does the same.
* **As a number.** A value converts implicitly to its plain integer type,
  and from there as that type does (§6.3). Operators compute with the plain
  value, so `w + 1`, `w << 3`, `-w` and `w & m` are plain integers.
  Compound assignment and `++`/`--` on a place of an index type are errors.
* **Passing through.** Declarations without a written type, parameters,
  results, fields, ADT payloads and elements carry the type, and a store
  into any of these takes only a value of that type. A branch, `match` or
  block value has an array's index or bound type where every branch has an
  index or bound type of that array (at the widest width, and a bound type
  if any branch is one). Otherwise it has the join of the plain integers
  (`if c { w } else { 0 }` is a `u32`). A reference
  or slice to index-typed storage is `(u32 in a)&` or `(u32 in a)[:]`,
  never a `u32&` or `u32[:]`, which could write any `u32`. A type argument
  may be an index type, and each instantiation is checked with it (§7.7).
* **No default.** An index type has no default value: `default<T>()`, a
  literal's `..`, a field default and a global without an initializer all
  need one, and an empty array has no index at all.
* **Never shrunk.** An array an index or bound type names is never shrunk:
  `pop`, `pop_n`, `resize`, `clear` and a whole assignment of it are errors
  anywhere, directly or through a reference parameter, as is a shrink
  through a reference that may only bound such an array's storage (§9.5).
  Its length only grows, so a value once in range stays in range.
* **What stays out.** `format` and `to_bytes(a, out)` do not write into an
  array of `u8 in x`, which is not a byte array. When `from_bytes` verifies
  an image, it checks each index in the image against its array's length,
  and rejects the image if any index is out of range. The arguments of
  `qput`, `qget`, `qpoll`, `qlimit` and `thread_spawn` take no type holding
  an index type: each thread program's copy of the array grows on its own
  (§11.2). An `extern fn` or `export fn` signature holds none either, since
  C code is not checked against the array.

---

## 4. Values, copying, and assignment

### 4.1 Everything is a value; passing by size class

Goose has **value semantics**: a variable, field or element owns its value
inline, and a destination (an assignment, an argument, a return, a push, a
field initializer) receives a value of its own. How a value gets there
depends on its size class (§1.1):

* **Fixed-size values connect by value.** Scalars, structs, fixed arrays,
  references and slices are copied. Copying a reference or slice copies the
  reference or slice itself, never the pointee. Large fixed values are
  copied silently; there is no size threshold. A fixed array whose
  destination is a slice of its element type is not copied but viewed, as
  any array is (§3.10).
* **Non-fixed values are never copied implicitly.** A variable- or
  resizable-class *lvalue* (a variable, field or element) binds by reference
  when its destination has a reference or slice type, or is an untyped
  parameter (§7.7), an un-annotated `let`/`var`, a `for` binding, or a
  result whose type is inferred (§7.1). At a value-typed destination such an
  lvalue is an error.
  A value-typed destination takes an **rvalue** (a literal, a call's result,
  a constructing expression, built in place per §4.3) or an explicit
  **`copy(x)`**, which is a fresh copy of the stored value, constructed at
  the destination like any other rvalue. A function's own local is *moved*
  by `return`, or as its body's final expression (§7.3).

So `f(xs)` passes a `u8[][>..]` variable by reference to each of
`fn f(xs: u8[][:])`, `fn f(xs: u8[][>..]&)` and `fn f(xs)`. `out.push(w)`
needs to be written `out.push(copy(w))` when `w` names storage. `x.f(a)`
(§7.1) reads as `f(x, a)` whatever `x`'s size class. At an untyped
destination, a reference to a *fixed* value is still written `&x`
(`let r = &n;`, or equivalently `let r .= n;`, §3.8; `f(&n)` into an untyped
parameter). Wherever the destination's own type is a reference, the `&` is
redundant, and a redundant `&` is a warning.

These destinations give an `if`, `match`, `block`, `loop` or bare `{ }` no
destination type: an un-annotated `let`/`var`, a result whose type is
inferred (§7.1), an untyped parameter, an operand, or the value a `for`,
`[..]` or builtin works on. There, the construct's value is a copy of what
the branch taken gives, unless its branches join as a slice (§6.4). A
reference to a fixed-size value (`&x` included) is copied as its pointee
there, as it is wherever a reference meets a value (§3.8).
`let r = if c { &a } else { &b };` makes `r` a copy of `a` or `b`, so the
`&`s are redundant and warn. A branch naming non-fixed storage, or a
reference to it, is an error there, as it is at a value-typed destination;
`let x = if c { copy(a) } else { copy(b) };` writes the copies out
explicitly.

At a reference-typed destination (an annotated `let`/`var`, a reference
parameter), each branch binds by reference instead, whatever its size class.
`let r: i64& = if c { a } else { b };` binds `a` or `b` itself.
`f(if c { a } else { b })` passes `a` or `b` itself to
`fn f(xs: i64[>..]&)`, as `g(if c { i } else { j })` passes the `i64` `i` or
`j` to `fn g(x: i64&)`. Given `if c { s } else { t }`, `fn h(p: u8[:]&)`
re-points the slice variable the branch taken names. Calling a function
value is a call, not one of these constructs: its value is a temporary even
where its body's value names a variable (§9.2).

There is no ownership transfer beyond the return move, no destructors, no
`Drop`, no reference counting. The only deallocation is scope exit resetting
stack pointers. A `defer` (§6.6) runs code the program writes at a block's
exit; it is tied to the block, not to a value, and releases no memory
itself. (A `move` operation for resizable arrays, which assigns and leaves
the source empty, is anticipated but not in v1.)

Copying a variable or resizable value costs O(size). The explicit `copy`
marks that cost in the source.

Shadowing: an inner scope may re-declare a name (a distinct variable).

### 4.2 Construction contexts

Variable and resizable values come into existence only at these points, each
of which provides fresh storage in a statically known place:

* `let`/`var` initialization of a local or global;
* a by-value argument slot (§7.2);
* a `return`ed value (§7.3);
* `push`/`append` into a resizable (the new element region);
* a field/element inside a larger value under construction.

Construction writes metadata first, then elements or fields in order, as
required by invariant §1.3(4). Construction of a limited array writes only
its metadata and any provided elements; the remaining capacity is reserved
but **uninitialized**. This is safe because no read path to uninitialized
slots exists (§5.3), and cheap because the runtime commits skipped address
ranges explicitly (Appendix C.4).

While a value is being built in place in an array, nothing may grow that
array, neither the expression being built nor a function it calls (§1.3(4)).
Such a value is an element `push`ed into or allocated in the array that is
variable-size or holds relative references, a call's array result `append`ed
to it, an `append`ed array literal of such elements, or the new contents of
a whole assignment (§4.4). The compiler rejects a growth it cannot show to
be of a different array. A whole assignment builds its new contents over the
old ones, so its right-hand side may not use the array at all (§4.4).

Literal forms usable in any construction context:

* array literals `[1, 2, 3]`. An empty `[]` can be used where the element
  type is known from context, or as the whole initializer of a `var` local.
  The context is the array type of the variable, parameter, field, element
  or result the `[]` constructs, or the slice type of one it is a temporary
  for (below). It is never the type of an operand, branch or element beside
  the `[]`: `x == []`, `if c { [] } else { x }` with no destination type,
  and `[[], [1]]` are errors. As the whole initializer of a `var` local
  (`var out = [];`), `[]` makes the local a grow-only `T[>..]` whose `T` is
  fixed by the first `push`, `append`, `format` or whole assignment into it
  (a string literal pushed into one makes it a `u8[][>..]`). `T` must be
  fixed before the local is otherwise used or its scope ends. The `[v; n]`
  fill form takes a non-negative compile-time element count (a constant
  expression, as a size is: §3.3). `v` is evaluated once, including when `n`
  is zero, and its value is repeated. Relative links retain their target at
  every destination; `self` in a literal refers to each constructed element.
  A literal whose destination names no array type is a `T[k]`, or a `T[]`
  when its elements are not fixed-size (§3.3). A `T[k]` literal may also be
  a temporary that a slice destination, a `for`, `[..]` or `bytes_of` views
  (`f([])` views an empty `i64[0]` for `fn f(xs: const i64[:])`). A `T[]`
  literal is a variable value and exists only in a construction context, so
  viewing it requires binding a variable to it first. `==` and `!=` compare
  a literal of either kind with any array or slice of its element type
  (§4.5), since their result holds no view of it.
  A literal whose elements, at any depth of nesting, are all integer
  constants or all floats of literals adapts to a parameter as its elements
  would (§3.1, §6.3). It is a value of an array or slice parameter of its
  shape whose element type its elements fit: `take([0, 0, 0, 7])` for
  `fn take(c: u8[4])`, and `[1, 2, 250]` for a `const u8[:]`. In overload
  resolution this counts as a coercion (§7.1). The same holds for a
  construct whose branches are all such literals of one type.
  An `append`ed literal is the run of elements it adds. It is a `T[k]` for
  the array's element type `T`, or a `T[]` when `T` is not fixed-size, and
  its elements are constructed as the array's own (`bytes.append([1, 2])`
  for a `u8[>..]`; `[]` adds nothing; a `str()` among them is built in its
  element). Elements that are variable-size or hold relative references are
  built where they stay, so a relative reference among them may point into
  the array. Other elements are evaluated before the run is added, as a
  pushed fixed-size element is;
* struct literals `X { a: 1, b: 2 }` (named) or `X { 1, 2 }` (positional, in
  declaration order; no mixing). Named initializers may be written out of
  declaration order only when every pair reversed by that order is proven
  free of observable effects, runtime failures and nonlocal exits. A reversed
  pair may not span an effectful omitted default either. Evaluation and
  construction always proceed in declaration order, directly into each field;
  no temporaries or copies are introduced to preserve source order. The
  check is conservative. It admits reads, nontrapping computations and calls
  whose checked bodies prove safe. Unknown/native calls, recursion,
  mutation, indexing, loops, checked arithmetic/conversions and potentially
  checked destination adaptations (capacities, length prefixes, relative
  offsets) prevent a pair from being reversed. The check runs before
  optimization and includes debug checks in every build, so acceptance does
  not depend on optimization or runtime debug mode.
  The diagnostic lists the supplied fields in declaration order;
  omitted defaults and padding do not need to be added. If side effects
  must run in a different order, perform them before constructing the literal.
  Fields with declared defaults (§3.2) may be omitted: trailing ones in the
  positional form, any of them in the named form. An omitted optional field
  without a declared default is null; other omitted fields are errors,
  unless the literal ends in `..` (`Entity { kind: k, .. }`). Each of those
  fields then takes its type's default value (below), `[]` for an array
  field, and a field whose type has no default value is still an error;
* `[..cap]` — an empty limited array `T[..]` with the given construction-time
  capacity (`cap` a runtime expression); the reserved slots stay
  uninitialized (§5.3, C.4). An array or string literal constructing a
  limited array of static capacity must fit it (a compile error otherwise);
* variant literals `Shape.Circle { r: 1.0 }`, whose fields follow a struct
  literal's rules, `..` included;
* `self`, inside a struct or variant literal only, as the initializer of a
  non-optional relative-reference field pointing at the value being
  constructed (`Node { prev: self, next: self }`, §3.9). This is the only
  way to give such a field a value before anything else it could point at
  exists;
* string literals (§3.7);
* `copy(x)`: a fresh copy of the stored value `x` names (or the pointee of a
  reference), constructed at the destination like any rvalue. It is the only
  way a non-fixed value can reach a value-typed destination from storage
  (§4.1). Copying a temporary is an error, since it is fresh already.
* `default<T>()`, for any fixed-size `T`: the value a `T` has before
  anything is written to it. Numbers are 0, bools `false`, optionals null,
  slices and limited arrays empty, and an ADT is its variant 0. Declared
  field defaults (§3.2) are applied wherever a struct or payload declares
  them, so an invariant that a declaration encodes as a default survives
  being filled in by generic code (a `sum`'s accumulator, a hash table's
  empty slots). A type that contains a non-optional reference without a
  declared default has no default value, and `default<T>()` for it is a
  compile error. (The zero value a missed `qpoll` or `qget<T>(ms)` yields,
  §11.2, is all zeros; the two agree except where a field declares a
  non-zero default.) A type that is not fixed-size has a default value too.
  A literal's `..` gives that default value to a field, but `default<T>()`
  does not provide it. An array other than a fixed one is empty (`[]`; a
  `T[..]` then has no capacity), a `varint` is 0, and a struct, a variant or
  an ADT's first variant is built from its fields' declared defaults and
  default values the same way (`S { .. }`).

### 4.3 The in-place construction guarantee

A constructed nonfixed value is built **directly in its receiving
storage**, including through calls and the selected branch of an
`if`/`match`/case-dispatch. Passing through such an expression is not a
reason to materialize a separate result and then copy it into place.

The same guarantee applies to fresh fixed-size aggregates containing
relative references when the receiver takes their representation. Their
result channels name the receiving storage,
including through multiple and nonlocal returns; correctness does not
depend on inlining or C compiler optimization. Other fixed-size results
may be passed as ordinary C values.

The packed byte layout may require relocation within the receiving
storage: a length prefix whose width becomes known after constructing the
elements can grow or shrink, and a value-form result delivered as an
element run can lose its header and unused capacity. Moving the element
bytes for these layout changes is permitted. The final representation,
reference targets and evaluation order must remain the same; an initializer
must not be re-executed to discover its size.

A limited-capacity receiver must establish that the complete live result
fits before writing outside its available slots. When the result's length
is not available before evaluating it, temporary construction for this
capacity check is permitted. This exception concerns a bounded receiver;
it does not permit staging an ordinary fresh result for a resizable one.

Copying an *already-constructed* value (an explicit `copy`, a by-value
argument from fixed storage, or the repeated value of a fill) is an ordinary
copy. The guarantee concerns fresh construction; §7.3 extends it to
returned locals and specifies the copies associated with different returned
locals and exits that abandon a partial destination.

### 4.4 Assignability

An lvalue may be assigned (`=`) after construction iff its size cannot change
or it can absorb the change (for reference-typed lvalues these rules govern
the *pointee* write that `=` performs; the reference value itself is updated
only by `.=`, §3.8):

* fixed-size lvalues declared `var`: assignable (plain overwrite copy);
* whole resizable arrays (top of their stack), and values holding one as
  their tail (§3.4): assignable. Semantically this is clear-then-construct:
  the bump pointer resets to the array's element start and the new contents
  are built in place (from an rvalue or `copy(x)`, §4.1), so the right-hand
  side may not use the array (below);
* limited arrays `[..k]`/`[..]`: assignable if the new length fits capacity;
* fixed-mode ADT lvalues: assignable, including with a different variant;
* **not** assignable: variable-class lvalues (`T[]` locals/fields/elements,
  variable-mode ADT lvalues, `varint` fields). These are frozen at
  construction; rebuild the container instead.

The right-hand side of a whole assignment of a resizable runs after the old
contents are gone, while the new contents are being built in their place. So
it may not use the array: it may not name the array (including its length),
or a reference to the array or to the value holding it. Neither may a
function it calls, through a global or captured variable that function
names. `x = [x[1], x[0]]`, `x = f(x)` and `g = rebuilt()` for a `rebuilt`
that reads `g` are errors. As with growth (§4.2), a use the compiler cannot
show to be of a different array is an error too. Examples are a use of a
reference parameter that a caller may bind to the global being assigned, and
a use through a call into a recursive cycle still being checked, which
counts as using every global and every variable in scope.

Where the assignment names the array as a field (`t.chars = …`), the
right-hand side may still name those other fields of the holding value that
cannot lead back to the array. These are flat fields (§1.1), and fields
whose references, through any number of steps, lead only to storage of a
type that cannot be the assigned location, cannot be a resizable on the way
from that location to the array, and cannot hold the value the location is a
field of. `t.chars = render(t.font)` is fine. So is `s.path = find(s.start)`
through `s: Walk&` for `struct Walk { start: Cell&, path: Pos[>..] }`, and
so is a reference of such a type that is bound to a variable first. A field
`last: Walk&` or `other: Pos[>..]&` may refer to `s` or to `s.path`, so
naming such a field is an error.

An assignment through a reference field (`d.r = …` for `r: u8[>..<]&`)
writes the array the field refers to. That array may lie anywhere an array
of its type can, including in the holding value's other fields. So a field
that is, or holds by value, an array of that type may be the one assigned
(`d.r .= d.own`), and so may what another field refers to. Naming either is
therefore an error, except for a local whose record shows where the
reference field may point.

Slices and references into the old elements are checked by the shrink rule
(§5.1). The assignment starts with a shrink and the right-hand side runs
after it, so a slice or reference that the right-hand side uses is live at
the shrink. New contents computed from the old are built in a variable of
their own and assigned as `copy()` of it, with the single copy written out
explicitly at the point where it happens (§4.3).

`let` forbids only assigning that name or field as a whole. The *contents*
of a `let` array or struct are as writable as their type says
(`let xs = [1, 2, 3]; xs[0] = 9;` is fine), and so is what a reference to it
reaches. Contents are made read-only by the type, `const T` (§9.5), and
`const x = e;` declares a `let` of type `const T`, which can be neither
reassigned nor written into. A `var` of type `const T` is the converse: it
can be reassigned (`=`, compound assignment, `++`) but never written into.
Definite assignment is enforced: no reads of uninitialized locals; every
declaration either has an initializer or is provably assigned on all paths
before use. A `let` declared without an initializer gets its value once.
Assigning it, or binding it with `.=` (§3.8), is an error if any path may
already have given it a value. That includes a joining branch, an earlier
iteration of a loop outside which it was declared, or a function called along
the way. Calls that compute the assigned value also count (§7.5). A
recursive cycle (§7.8) cannot assign such a `let` declared outside the
cycle, since all the cycle's activations share it. Fixed arrays require full
initialization (every slot is indexable); the `[v; n]` fill literal makes
large ones cheap.

### 4.5 Equality and comparison

`==`/`!=` are **structural** and require both operands to have the same
type. They compare:

* scalars and bools by value;
* structs and fixed arrays memberwise, excluding pad and ADT padding bytes.
  Comparison is semantic, per member: memcmp is a valid optimization only
  for gap-free layouts without floats, since a float compares by IEEE value
  wherever it sits (`-0.0 == 0.0`, and NaN is unequal to itself);
* array-family values and slices by length, then elements (a sequential walk
  for variable elements). Two operands of *different* array kinds compare
  too, as slices, whenever their slices would have one type (§3.10): a
  `u8[>..]` field against a string literal, or a `u8[..32]` against a
  `u8[:]`, with no `[..]` needed;
* ADTs by tag, then payload.

References and slices follow a *top-level rule*. As the direct operands of
`==` they have value-like semantics: a reference compares its pointee
(transparency, §3.8), and a slice compares length, then elements. As
*members* of a compared composite they compare by identity (the reference
address, the slice's address+length): recursing through them would turn
`==` into an unbounded pointer traversal. Optionals compare as nullable
references (`o == null` is the null test); `.==`/`.!=` compare two references by
address rather than by pointee (§3.8). Ordering `< <= > >=` exists on the numeric
types only, with operands unified per §6.1.

---

## 5. The resizable/shrink rules

### 5.1 Grow-only `[>..]`

A grow-only array can shrink, but **never while a reference or slice into it can
be live.** Memory below the top never moves and is never reused while the owner
lives. Every interior reference and slice therefore stays valid for as long as
it can be named, and a bound on the length holds across every `push`.

* `push`/`append` bump the stack.
* May contain variable-size elements (build strings/ADTs in place, §7.3).
* Shrinking (`pop`, `pop_n`, `resize` downward, `clear`) is legal exactly
  where the compiler can see that nothing is rooted in the array, under the
  conditions below. `pop`, `pop_n` and `resize` additionally need fixed-size
  elements, since a sequential array cannot find its last element (§3.3).

Use grow-only arrays for arenas, pools, string builders, tree storage, and
scratch buffers that are refilled or popped between phases.

**When a grow-only array may shrink.** The receiver is the array's variable,
a reference variable or parameter bound to the whole array, a global, or the
tail field of a struct reached through any of those (`s.items.pop()` for
`s: Stack&`). Shrinking a tail field shrinks the storage the struct lies in,
as a reference to the field would. The receiver is not a temporary, and not
a `reusable` pool (§5.4: its freelist keeps every slot live). The shrink may
be anywhere in an expression (an operand, an argument, a condition, the
value of a block, `if`, `match` or loop), so `print(a.pop())` and
`if !load(s) { … }` are fine.

At the shrink, nothing *live* may refer into the array. This rules out
anything that the shrink's statement evaluated before the shrink and uses
after it (§2): an earlier argument or operand that is, or holds, a reference
or slice; an earlier field or element of a literal being built; the array an
indexing or slicing reads; the destination of an assignment. So
`use(a[0], a.pop())`, `a[a.pop()]`, `S { r: a[0], n: a.pop() }` and
`a[0] = a.pop()` are errors, while `a[0] + a.pop()` reads its left operand
first and is not. Nor may a value be under construction in the array's
storage (§1.3): an element that `push` builds in place (§4.3), a literal run
that `append` builds, or a call's result being appended.

It also rules out any reference or slice variable rooted at the array. This
includes a `var` reference that the same-depth rebinding rule (§9.2) could
retarget into the array, and one that is not bound yet but is bound further
down a loop body. And it rules out any value that *holds* a reference into
the array: a struct with a reference field, an array of slices, a `let` copy
of an element of such, and the struct whose tail the array is, through its
other fields. Values hold references only where a store put them, and every
store the checker has seen is on record (§9.2). So this half of the test is
exact to the store: the error names the holder and the line where a
reference into the array was stored into it.

A value is never a holder if its type cannot hold a reference to anything
the array's elements contain by value, through any number of references. Nor
is a value linked by relative references alone (a node pool), since relative
references point within their own root. A reference to a whole array or
table is not a reference into its elements, and a shrink leaves it pointing
at the array or table:
`struct Walk { start: Cell&, text: u8[>..]&, path: Pos[>..] }` holds nothing
a shrink of `path` frees, and `w.path.clear()` through `w: Walk&` may be
followed by any use of `w`. A view of the elements taken through such a
reference is stored, if at all, where the holder's type has room for one.
The same holds after a whole assignment: `s.r = [1, 2]` for `r: u8[>..]&`
may be followed by any use of `s`. (The right-hand side of a whole
assignment is different: it runs while the array is rebuilt, and may not
reach the array at all, §4.4. A variable that the right-hand side takes a
reference out of counts as holding what that reference leads to, but a
variable of which it reads only flat fields does not.) A reference to a
slice variable or to a holder (including the storage whose tail shrinks)
counts as what it refers to: while the reference is live, so is whatever
that slice or holder may refer into.

A whole assignment of a value that holds no references (`refs = []`,
`s.refs = []`, a struct literal with none in it) *empties* the part of the
holder it writes: what was stored there before is gone. The part is a
variable's own storage, or the pointee of a `let` reference or of a
reference parameter (which no rebinding moves), together with the fields
named after it. Where each slot of a holder that can hold a view into the
array lies, on every path to the shrink, in a part emptied this way (the
whole holder, or its fields one by one), the holder holds only what was
stored into its storage since then. This stops being true once a store into
that storage is made that the record cannot show, such as one moving the
holder's own references within it. It also stops once a call runs that may
write into the part, since a callee's stores are never on record in their
order. These calls may write into the part:

* a call handed a reference that may point at the holder's storage, with
  room for the part or lying in it, or handed anything whose references may
  lead to storage of the part's type, whatever it points at;
* a call of a nested function;
* a call handed a block or any other function value;
* a block's own call.

A call handed a struct of the function's own, built from the holder's
reference fields, whose references lead only to what those fields do, is not
such a call. So
`for i in 3 { refs = []; pool = []; refs.push(pool.push(i)); }` passes: the
store at the end of one iteration is gone by the shrink at the start of the
next. Emptying `refs` on some paths only, after a store that reaches the
shrink, or only in a copy of it, does not pass.
`s.refs = []; s.pool = []; s.refs.push(s.pool.push(i));` through `s: S&`,
`struct S { pool: i64[>..]&, refs: (i64&)[>..] }` also passes: the path to
the pool is no reference into it, and a view taken through it is stored, if
at all, in `refs`.

A shrink must also preserve the storage that an enclosing `for` loop walks
(§6.5). This includes every reference or slice on the path to the sequence,
since the loop reloads that path on each iteration. It also includes the
sequence iterated in place, such as a slice, element, or field. A resizable
array is the exception: the loop reads its length again on each iteration.

A variable is live at the shrink when it can be read again afterwards, that
is, when it is named:

* later in the shrink's statement: in an operand evaluated after the shrink,
  or, where the shrink is in the condition of an `if` or `guard`, the
  scrutinee of a `match` or the sequence of a `for`, in the parts that the
  condition, scrutinee or sequence leads to;
* later in its own block or in an enclosing block it was declared in;
* anywhere in a loop that contains the shrink and that the variable was
  declared outside of, since the next iteration runs the rest of the body
  again.

But suppose a statement of the shrink's own block after the shrink's
statement is a `return` or a `break`, and neither the shrink's statement nor
any statement between them has a `continue` that could lead back into a
loop. Then nothing that the exit leaves runs again from the shrink. The exit
leaves the rest of that block; a `return` also leaves the rest of the
function's body, and a `break` the rest of the loop or `block` it ends,
including that loop's next iteration. So a decoder may
`result.clear(); return result;` inside its loop while a dictionary of views
into `result` that the loop fills is still in scope. The exit's own
statement is read after the shrink like any other.

A function value's body runs in the middle of a statement of the function
calling it (§7.6), so that function's variables count as well. That function
may also call the function value again, so a variable declared outside the
body counts wherever the body names it, as in a loop. Only code that can
name the variable counts (the code of its own function, and of the nested
functions and function values written in it), so the same name in another
function is a different variable. "Named" is syntactic: any mention counts,
including a call of a nested function that mentions the variable, so the
test never depends on what the optimizer proved.

A reference whose last use is before the shrink is dead, even if its block
has not ended: `let w = line[..5]; print(w); line.clear();` is fine. A
scratch buffer refilled per iteration, or a stack popped between phases, can
hand out slices of itself freely, so "reusable scratch" and "structure I can
point into" are the same type. The operations themselves are the grow-shrink
ones: a stack-top move and a length store.

Assigning the array whole (`a = …`), or assigning a value holding it,
replaces its elements. It is a shrink under the same rule wherever its
location could be the receiver of a `clear`: for `s: Stack&`,
`s.items = []`, `s.items = load(f)` and `s = fresh()` are shrinks of the
storage `s` names, as `s.items.clear()` is. The right-hand side runs after
the shrink, so a variable that side names is used after it. `print`, `str`
and `format` evaluate and render their arguments in order (§3.7). So a
variable that a later argument names is used after a shrink made by an
earlier argument, whether by a call in that argument or through a `format`
overload rendering it.

A shrink through a reference or of a global cannot see the callers'
variables, so it is checked at every level. Each function specialization
records which globals, and which parameters' pointees, it shrinks (directly
or through its own callees), and what its calls stored into those pointees.
A call is checked as if it performed those shrinks and stores in the caller,
with each grow-only shrink checked where it stands in the body:

* against the stores the body made before it (in a loop, any the body
  makes), not those it makes after;
* with the parts of reference parameters' pointees that the body had emptied
  before it on every path counted as empty in the caller's holders too,
  where the argument names such a part of storage that no nested function or
  function value names. Such a part is the caller's variable, a field path
  in it, or the pointee of the caller's own `let` reference or reference
  parameter, which the caller's own callers judge the same way.

A function doing `s.refs = []; s.pool = []; s.refs.push(s.pool.push(i));`
may be called again and again on the same holder, in a loop or through a
function taking the holder by reference. The pointees of a parameter that
holds references by value (a struct with a reference field, an array of
slices) are what those references point to: a shrink or a store through one
of them is of the caller's storage, not of the callee's copy of the holder.

When a global receiver shrinks, the other globals are live, and what is
stored into them may come from functions checked after the shrink. So they
are judged once the whole program has been checked. By then every store into
a global, and every binding of a global reference or slice, is on record,
wherever in the program it is made. The shrink is an error where another
global may hold a reference into the array; the error names that global and
the line of the store. A call into a recursive cycle still being checked
counts as shrinking every array a function of the cycle textually shrinks.

A user `format` overload (§3.7) is a call at the `print`, `str` or `format`
that renders a value through it. When a `format` call renders the value, the
text is appended to its receiver, and that receiver is the overload call's
builder. The rest of the argument holding that value is rendered around the
call, so it is still in use there (where the argument lies, the elements of
the arrays and slices it renders, and what its references lead to), unless
the overload takes the whole argument: the value itself or a reference to
it.

A reference whose root is inexact (§9.2), such as a value chosen between
arrays (`if c { a } else { b }`) or one read back out of a container, may
point at any array of its type that the root bounds. A shrink through it
counts as a shrink of each of those arrays: those in variables at the root's
scope or outside it, and the caller's arrays that a parameter's references
lead to, for which the callers check the shrink. The same holds where a call
shrinks what an inexactly rooted argument points at.

A reference read out of the storage behind a reference parameter
(`s.text = …` for `s: S&`) may point at anything the callers or the function
stored there. The function judges its own variables against every array of
the type that outlives the call. Its callers each judge the shrink, by their
argument, as a shrink of an array that the storage leads to. A struct of the
caller's leads to the arrays its record says it refers to that are of the
type shrunk, or that hold or lead to one: `s.text = copy(t)` in a callee
frees no global `u8[>..]` the caller views, where the caller's struct refers
to a local text and a node pool. A struct built from the caller's own struct
parameter's reference fields leads where those fields do, which the caller's
callers judge in turn.

Nor can a shrink inside a function tell what its parameters point into. A
parameter's pointee is whatever each call passes, which may be a global, a
variable of an enclosing function, or what another parameter points into; an
inexactly rooted argument may be any array its root bounds. Where a
reference, slice or holder parameter, or a variable bound from one, is still
used after a shrink of a global, of such a variable or of another
parameter's pointee, the specialization records the pair. It does the same
where a view of a global or of such a variable is still used after a shrink
of a parameter's pointee. Each call judges the pair by the arguments it
passes. A call that may make the two one array is an error, as a still-used
variable of the caller would be, and a call that passes its own parameters
on records the pair for its own callers. A call into a recursive cycle still
being checked is judged again against the pairs the cycle records, once the
whole cycle has been checked.

### 5.2 Grow-shrink `[>..<]`

* Fixed-size elements only.
* `pop()` returns the element by value. A reference or slice element is
  returned as the reference or slice it is, pointing where it did and as
  writable as its slot, as reading the element would give it (§9.5), since
  what a temporary holds is not rooted at the temporary (§9.2).
  `pop_n`/`resize`/`clear` are allowed from anywhere: on a local, through a
  reference, on a global, on a struct's tail. Assigning the array whole is a
  shrink too.
* References and slices into it are created like any other (§3.8, §3.10),
  and are **held by variables only**: bound to a local, passed down, or
  returned, but never stored into a field, element, or global. Rationale:
  after a pop, that stack-top memory can later be reused by *different
  types* (other locals, other pushes), so no reference into it may outlive
  the next shrink. Keeping such references out of storage means the next
  rule only has to scan the variables in scope.
  A literal's fields and elements are storage wherever the literal ends up,
  so no struct, variant or array literal holds such a reference, even where
  the literal itself is passed down or returned. `f(S { a[..] })` is an
  error, as `let s = S { a[..] };` is. So is binding a global reference or
  slice variable to one, since a global is storage as well.
  What counts is where a reference *may* point, whatever its root says. A
  value that may be any of several (§9.2) is stored only where each of them
  could be. A parameter given a value that may point into the array is not
  stored either, nor is a function's result where one of its returns may
  point into it, nor a reference to a slice variable whose slice may. Where
  a value's root only bounds where it points (§9.5), because the value was
  read out of the storage behind a parameter or out of a value holding
  references, or a callee returned such a read, the value may point into any
  grow-shrink array that the storage leads to through references. With
  `t: Top&` and a field `a: Cell[>..<]&` of `Top`, `t` and `t.a` may be
  stored, but not `t.a[0]`, nor the bytes of `t.a`.
  A reference or slice read out of a field, an element or a global can never
  point into a grow-shrink array, since it was stored there, whatever
  storage bounds it (§9.5). Neither can a slice that such a slot holds,
  loaded through a reference to the slot. Nor does a value holding
  references that is read out of a field or an element, popped, or copied
  out of one by a `for` or a `match`, hold a reference that may point into
  one: what it holds lies in that slot, and it is stored wherever what holds
  it outlives the destination. A variable that points into no grow-shrink
  array (one bound to such a read, for example) may not be rebound to a
  value that may point into one: code that read the variable earlier through
  a reference to it may have stored it.
  The rule is about references that can point *into* the array. A reference
  merely rooted at a value that holds one, whose pointee type the array's
  elements cannot contain (a slice key read back out of a dictionary's
  slots), can be stored like any other, whether or not it was passed to a
  parameter. A `keys: u8[>..]&` read out of a struct with a `u8[>..<]` tail,
  and a slice a callee makes of the buffer it refers to, can be stored as
  dictionary keys freely.
* **A shrink is an error while any live variable or value may refer into the
  array**. This is the test a grow-only shrink applies (§5.1), including the
  values its statement still uses, its liveness rule, and its call summaries
  for a shrink through a reference or of a global, but without the store
  record. References into a grow-shrink array live only in variables and in
  the values of the statement being run, so checking those still in use (a
  reference to a slice variable among them) is sufficient.
  For the same reason, a plain reference or slice read out of a field, an
  element or a global never refers into a grow-shrink array, since none is
  ever stored there. The shrink does not consider such a reference or slice,
  nor a slice of such a slice or a reference into what it views, whatever
  storage the read-back rule (§9.5) bounds it by, nor what a value read out
  of such a slot holds. This stops at a reference: what a reference read out
  of a field leads to may be a whole grow-shrink array, or a variable
  holding a view into one. And a `var` bound to such reads still counts
  where a rebind could retarget it into the array: a same-depth rebind
  (§5.1), or, inside a loop the variable was declared outside of, a rebind
  later in the body to a value whose root only bounds the array (§9.2).
  The error is at the shrink and names the variable and where it was bound,
  so either end can be changed: use the slice for the last time before the
  shrink, or move the shrink. A call into a recursive cycle still being
  checked counts as shrinking every grow-shrink array it can reach,
  including through the references its arguments hold. Function values run
  inline, so a shrink inside a block is checked against the block's own
  enclosing scopes, and against the function calling the block, since the
  block runs in the middle of one of that function's statements.
* **Balanced calls.** A call is *balanced* for an array when the array is
  never shorter, while the call runs, than it was when the call began. Such
  a call frees nothing that a view taken before it points into. A live view
  lies within the array's length at the call, since every shrink after the
  view was taken was checked against it. A view that a reference to a slice
  variable reaches is no newer, since the callee can only return a view,
  never store one where the caller would read it. A balanced call is
  therefore not a shrink where it is made: no view is checked against it
  there, or recorded with it for the callers to judge (§5.1), since a
  parameter's view was also taken before the call. It still counts for the
  callers, as a balanced call where the caller is balanced too.

  A function is balanced for an array X if every shrink of X is either a
  balanced call for X or a qualifying `X.resize(m)`, with or without a fill
  value. For the resize to qualify, `m` must be a `let` in the same
  activation, initialized to exactly `X.len` on the same path X. The array may
  be a reference parameter's pointee, a global, or a captured variable. The same
  path means one variable that
  cannot be rebound (a `var` reference can be), followed by the same fields,
  none of them a reference. `pop`, `pop_n`, `clear`, a resize to anything
  else, and assigning X or a value holding it whole are not balanced.
  Neither is a resize to an `m` that is bound to a writable reference
  (§4.4), by `&`, `.=`, or a reference parameter, field or binding that is
  not `const`, in a nested function too, since a write through that
  reference can lower `m`. Binding `m` to such a reference after a resize
  back to it is an error: that resize has already been judged balanced,
  while a loop can run the write before it the next time round. Every length
  X has during such an activation is at least its length on entry, so every
  mark is too, and so is every resize back to one.
  A call is balanced for an array of the caller's when every shrink it may
  make, of any array that can be that array, is balanced. The caller's roots
  decide which arrays can be that array, and a parameter's array can be a
  global, a captured variable, or another parameter's array.

  Grow-only
  arrays have no balanced calls (§5.1): a callee can store references to its
  new elements into the caller's holders before shrinking them away.

  A call into a recursive cycle still being checked cannot be judged by the
  part of the callee checked so far, so it is taken to be balanced for every
  grow-shrink array it can reach until the cycle has been checked. Once it
  has, the call is balanced if no function that such a call enters makes an
  unbalanced shrink of a grow-shrink array when such calls are counted as
  balanced. By induction on how deeply a run nests its calls, no run of the
  cycle then shortens an array. Otherwise the call, and every call judged
  balanced because of it, counts as a shrink after all: it is checked
  against the views live across it, and recorded with them for the callers
  to judge.
* `push` returns a reference to the new element, and `index_of` works, as on
  grow-only arrays.
* Iterating the array with `for` uses indices under the hood and reads its
  length on every iteration, so changing the length inside a by-value
  `for x` loop over it is legal and only a logic hazard (bounds checks keep
  it safe). The `&x` binding is a variable in scope, so a shrink inside such
  a loop is an error. So is a shrink inside a loop over a slice, an element
  or a field of the array, which the loop walks in place, or over a sequence
  it reaches through a reference or slice stored in the array (§5.1, §6.5).

### 5.3 Limited arrays `[..k]` / `[..]`

Limited arrays grow and shrink freely within a fixed capacity. They are not
bound to a stack top (their capacity is reserved at construction), so they
can live anywhere fixed or variable values live (`[..k]` is fixed-class,
`[..]` variable-class). A `T[..]` constructs either from element contents
(capacity = initial length) or with the `[..cap]` literal (capacity `cap`,
length 0).

Interior references and slices **are allowed**, including surviving pops.
Because capacity and element type are fixed, every slot within capacity,
once first written, remains a valid value of the element type forever;
pop-then-push reuses memory at the same type. Never-written slots are
unreachable: indexing checks `len`, references/slices can only be created
into `[0, len)`, and every growth operation supplies values (§3.3). A stale
reference reads a valid, possibly-different value ("type-safe reuse", §9.4),
never corrupt memory. Indexing through the array checks against current
length; access through retained references does not (they were validly
created).

### 5.4 `reusable` arrays (safe allocation escape hatch)

A grow-only resizable local of fixed-size elements may be declared
`reusable` (the keyword prefixes the declaration:
`reusable var pool: Slot[>..] = [];`, also valid on globals). The compiler
pairs it with a hidden freelist (itself a small resizable of indices, on its
own stack, counted in N):

* `a.alloc_index(v) -> i64` — index of a slot: a freelist slot if available,
  else a fresh `push`.
* `a.alloc_ref(v) -> T&` — same, returning `&a[i]`.
* `a.free(i)` — records slot `i` for reuse. The element remains a valid,
  live value of its type forever. The index must lie in `[0, a.len)`;
  otherwise the operation aborts with the ordinary array-index out-of-bounds
  diagnostic, in every build, before changing the freelist. The index
  expression is evaluated once. Where the code holds references rather
  than indices, `a.free(a.index_of(r))` (§3.3) turns a reference back into
  an index.

This is part of the semantics, not just the implementation: *all elements
remain valid at all times*. A reference to a freed-then-reused slot reads a
different (same-typed) value. This is type-safe reuse, never memory
corruption. This is the language's answer to tree-mutation workloads that
would otherwise need an allocator.

**Slice pools.** Declared `reusable[]` (`reusable[] var pool: T[>..] = [];`,
also valid on globals), the array hands out runs of elements instead. Its
hidden freelist holds (index, count) spans sorted by index, and a freed run
merges with the spans it touches:

* `a.alloc_slice(n) -> T[:]` — `n` elements from the front of the first free
  span, in index order, that holds them; failing that, from the end of the
  array, which grows by `n` less the elements of a free span that reaches
  its end (the run then starts with those elements).
* `a.free_slice(s)` — records `s`'s elements for reuse.
* `a.realloc_slice(s, n) -> T[:]` — `s` resized to `n` elements. Shrinking
  frees the elements past `n` and keeps the front. Growing a non-empty `s`
  keeps its elements in place when `s` ends the array, which grows, or when a
  free span starts where `s` ends and either holds the difference or reaches
  the end of the array, which grows by the rest. Otherwise, and always for an
  empty `s`, the slice's elements are freed and copied to where
  `alloc_slice(n)` then places the run, which may be their old place merged
  with free elements around it.

Every element an allocation or a growth adds is a default value (§4.2), so
those two need an element type that has one. The length is evaluated once,
and a negative length, or one no data stack could hold, aborts before the
pool changes. The slice handed to `free_slice` or `realloc_slice` must be
one of the pool's runs: one the pool handed out, a re-slice of it, or a
slice of the pool itself. Where the checker sees that, because the slice is
rooted at the pool exactly (§9.2), nothing is checked when the call runs.
Where the pool is a global or a variable of the calling function, or a pool
reference that can only be one of several such (**Pool references** below),
a slice rooted exactly at a global or a variable of the calling function
that the pool cannot be is a compile error. Any other slice, such as one
read out of storage where runs of other arrays could be kept as well, is
checked when the call runs: unless it is empty, its elements must lie inside
the pool's length starting on an element boundary, or the call aborts before
the pool changes. An empty slice frees nothing and grows like a new run,
wherever it points. A grown slice may be copied, which a value holding
self-relative references cannot survive (§3.9), so `realloc_slice` is a
compile error for such an element type; `in pool` references copy fine, and
the other two operations never move an element. The single-slot operations
do not exist on a slice pool, nor these on a `reusable` one; `push` and
`append` add at the end of either without consulting the freelist.

The guarantee is the same: a slice still naming freed elements reads whatever
their next owner wrote, or the default values an allocation put there. Freeing
a run twice is a logic error of the same kind: the freelist can then hand the
same elements out twice, but never anything outside the array.

**Pool references.** A `T[>..]&` variable or parameter bound to a pool is a
*pool reference*: the pool's operations work through it as on the pool,
since it carries the pool's freelist beside the address. So is a variable or
parameter bound to a value that chooses among pools of one kind, or among
pool references to them (an `if`, a `match` or a block giving one wherever
it gives a value). It carries the freelist of the pool chosen. A choice
between the two kinds of pool, or between a pool and another array, binds a
plain reference. A variable is a pool reference from its first binding on,
and `.=` rebinds it only to a pool of the same kind, or to a pool reference
to one; pointing it at any other array needs a new variable. A variable
first bound to anything else stays a plain reference, which `.=` may point
at a pool, as at any array, but which offers none of the pool's operations.
Only a plain reference variable or parameter is a pool reference: an
optional one, a field, an element and a function's result are not, even
where they point at a pool.

---

## 6. Expressions and arithmetic

### 6.1 Operators

The operators follow C and Rust:

* Arithmetic: `+ - * / %`. Integer `%` is Euclidean (§6.2); float `%` is C
  `fmod`.

* Comparisons. The logical operators `! && ||` accept `bool` only; `&&` and `||`
  short-circuit.

* Bitwise `~ & | ^ << >>` on integers.

* Assignment statements on assignable lvalues: `=`, `+=`, and the compound form
  of every other binary operator, including shifts.

* Reference rebinding `.=` and reference identity `.==`/`.!=` (§3.8).

* `++`/`--` as statements on integer lvalues, with no expression form.

Precedence follows Rust (Appendix D). Bitwise operators bind tighter than
comparisons, so `x & mask != 0` means `(x & mask) != 0`. Shifts bind tighter
than `&`. Range expressions `a..b` appear only in slicing brackets, `for`
headers, and match arms.

**Operand unification.** A binary numeric operator's operands must reach
*one common type*, which is also the result type. It is found as follows:

* equal types stay as they are;
* a constant adapts to the other operand's type (a compile error if its
  value does not fit);
* otherwise, if exactly one operand implicitly widens into the other's type
  (§6.3), the wider type is the common type;
* an integer operand meeting a float one converts to the float's type, so
  `i32 * f32` is an `f32` multiply and `i64 + f64` an `f64` add.

A float that takes its type from float literals (the literal itself, or
`n * 0.5` built from literals and integers) adapts to the other operand's
float type as a literal does (`x * (n * 0.5)` with `x: f32` computes in 32
bits throughout), and is an `f64` where neither operand gives it a type.
Anything else (same-width signed/unsigned, `u64` with anything signed) is a
compile error asking for a cast. Nothing here invents a type absent from the
expression: `u8 + i64` is an `i64` add, but `u32 + i32` does not become
64-bit arithmetic implicitly.

Exceptions: shifts take the *left* operand's type as the result (the count
is any integer type, masked per §6.2), and `==`/`!=`/orderings unify the
same way but produce `bool`. So a constant shifted by a count that is not a
constant keeps the constant's freedom to adapt. `1 << k` is not a constant,
but it takes the integer type of its destination or other operand, as a
constant does, where the constant fits that type (`let m: u64 = 1 << k;`,
`m | (1 << k)`, a `u8` field), and is an `i64` where nothing gives it a
type. An integer operation over such shifts and constants does the same
(`(1 << k) - 1`, `~(1 << k)`), and is computed at that type throughout; a
typed operand, unary `-` or a cast gives it a type of its own. So does `~c`
of a constant `c >= 0`, whose value depends on the width it is computed at.
It adapts where `c` fits, so `let a: u8 = ~4;` is 251 and `flags & ~F_ROAD`
clears a flag in a `u8`, while `~4` is -5 at a signed type or where nothing
gives it a type. `-~c` is the constant `c + 1`, and a named constant `~c`
(§3.1) adapts as `~c` does. Unary `-` requires a signed (or float) operand;
`~` any integer, keeping its type.

**Comparisons and `u64`.** A comparison produces `bool`, so it does not need a common numeric result
type. However, comparing signed and unsigned operands must preserve their
values. `u64` is the only unsigned type with no signed supertype
(`u8`–`u32` widen into `i64`, §6.3). A comparison between `u64` and a signed type
is therefore allowed **when the signed operand is known non-negative**: it
converts to `u64` without changing value, and the comparison is a single
unsigned one, no wider than either operand and never a hidden branch.

"Known non-negative" is determined by syntax and is limited to a non-negative
integer literal, a `.len` or `.cap` (non-negative by §10.4), or a `let` bound
to one of those. A `let` is never reassigned, but a writable reference can still
change its
value (§4.4). A comparison therefore cannot rely on a `let` if it, or a `let`
it was bound to, is bound to a writable reference anywhere. This includes
bindings through `&`, `.=`, or a reference parameter, field, or binding that
is not `const`. Bindings in nested functions count too. Either order is an
error. Whether a comparison compiles thus
depends only on what is written, never on how much the optimizer managed to
prove. Other cases require an explicit cast because conversion may change the
value. A cast does not, however, preserve every comparison: `x as! i64`
on a `u64` above `i64.max` silently compares as negative, and the checked
`x as i64` aborts in debug on a value that was perfectly legitimate to
compare. When the rule permits it, use the direct comparison to preserve the values
without conversion overhead.

**Elementwise math**: the arithmetic operators apply memberwise to any two
values of the *same* struct/fixed-array type whose scalar leaves are all
integers or all floats, none of an index or bound type (§3.11); the
result has that same type. The compound forms
`+= -= *= /= %=` apply to these same types, resolving the destination once
and reading its old value before the right-hand side runs (§2). Unary `-`
negates such a value memberwise where no leaf is an unsigned integer
(`-v`, with the same result type; a float member negates exactly, so `0.0`
becomes `-0.0`, and an integer one overflows as a scalar `-` does). This
covers vector math without an operator-overloading feature. The standard
library supplies `float3` and friends; named vector ops (`dot`, `cross`,
`normalize`, …) are ordinary stdlib overloads per math type, not language
builtins.

`*` and `/` also accept one scalar operand, in either order, when every
numeric leaf of the struct/fixed array has exactly the same type. The
scalar converts implicitly to that leaf type (§6.3): `float3 * 2.0`,
`2 * float3`, and `float3 / f32` produce a `float3`; `2.0 / float3` divides
the scalar by each component. The aggregate's type never widens: an `f64`
variable scaling a `float3` needs `as f32`, and an integer scalar must fit
or widen into the integer leaves. Nested structs and fixed arrays work the
same way; aggregates with mixed leaf widths or kinds, or no numeric leaves,
do not support scalar operands. `+`, `-`, and `%` still require two matching
aggregates. The compound forms `aggregate *= scalar` and
`aggregate /= scalar` follow the same rule. Both operands are evaluated
once, left to right, before any result member is written, including when
the scalar is a member of the destination.

### 6.2 Integer semantics

* Every integer operation computes **at its operands' type**: `u8 + u8` is
  an 8-bit add, `i32 * i32` a 32-bit multiply. There is no promotion:
  operands reach a common type only by the unification rule of §6.1, which
  never widens both sides behind the programmer's back.
* Signedness is part of the type: `/`, `>>`, and the ordering comparisons are
  unsigned operations on unsigned types (`>>` shifts in zeroes) and signed on
  signed ones (`>>` replicates the sign bit). Integer `/` truncates toward
  zero: `-7 / 3 == -2`, while `-7 >> 1 == -4`.
* **`%` is Euclidean** at every integer type: the result lies in `[0, |b|)`
  and is never negative, whatever the dividend's sign. So `x % n` is a valid
  index into a length-`n` array for *any* `x`. The reduction idiom means
  what it looks like, and the optimizer can drop the resulting bounds check
  (§10.5) instead of requiring a guard the programmer knows is redundant.
  On unsigned types this is ordinary remainder; on signed ones it costs one
  predictable conditional add over C's truncating `%`, on an operation that
  is already a division. Consequently `i64.min % -1` is `0`, not a trap.
  Float `%` stays C `fmod`, so `-7.0 % 3.0 == -1.0`, while `-7 % 3 == 2`.
* **Division pairs.** Integer `/` and `%` use different conventions, so
  `b * (a / b) + a % b == a` does not hold in general. With `import std;`,
  `rem(a, b)` is the truncating remainder (zero or the dividend's sign),
  and `div_euclid(a, b)` is the Euclidean quotient. The matching identities
  are `b * (a / b) + rem(a, b) == a` and
  `b * div_euclid(a, b) + a % b == a`, as mathematical identities when the
  quotient is representable; evaluating their intermediate products can
  overflow. Both functions accept all eight integer types, returning the
  operands' type. `div_euclid` rounds down for a positive divisor and up
  for a negative one, and has the same zero-divisor and quotient-overflow
  behavior as `/`. `rem` aborts on a zero divisor and returns zero for
  `MIN, -1` at every signed width. For unsigned integers `div_euclid` is
  ordinary `/` and `rem` is `%`.
* **Signed overflow** (at the operation's width): aborts in debug builds;
  wraps two's-complement (defined) in release. The debug abort is per
  operation *as it executes*, not per operation as written: the optimizer
  may regroup an associative chain or drop an operation altogether, and
  each check stays with the operation it belongs to. So a debug build is
  a bug-finding tool, not a promise about every intermediate result written
  in the source; what a release build computes never depends on any of it.
  **Unsigned arithmetic wraps modulo 2^width by definition, in every
  build**. Modular arithmetic is what hashing, PRNGs, and bit manipulation
  mean by unsigned math, and it is why those kernels are written with
  unsigned types. Division/modulo by zero: aborts always. `i64.min / -1`
  aborts in every build (it would trap in hardware); at narrower signed
  widths the same case is an ordinary overflow: debug abort, release wrap.
  Shift counts are masked to `0..width-1` (defined).
* Integer literals above `i64.max` (up to `u64.max`) are `u64` constants
  (§2, §3.1); negating one is a compile error (except `-(2^63)`, which is
  exactly `i64.min`).

### 6.3 Conversions

Within a kind, a conversion the machine can prove value-preserving is
implicit, and anything that could lose a bit or flip a sign needs a cast.
Between the kinds, an integer becomes a float implicitly, and
a float becomes an integer only through a cast: dropping the fraction is the
real loss of information, while an integer too large for the float's
mantissa rounds as every float result does.

* **Implicit conversions** apply wherever a value meets a differently typed
  destination or operand. An integer widens to the same signedness
  (`i8→i16/i32/i64`, `u8→u16/u32/u64`), and an unsigned integer converts to a
  strictly wider signed type (`u8→i16..i64`, `u32→i64`). Other implicit
  conversions are `f32 → f64`, constants to any type that holds their value
  (§3.1), and every integer type to either float type. The last rule includes
  `i64`, `u64`, and literals: `2` works where a float is expected, just as `2.0`
  does. Any integer type except `u64` also converts implicitly into a `varint`
  store (§3.6).
* An integer becoming a float takes the float's nearest value where it has
  no exact one (beyond 2^24 in magnitude for `f32`, 2^53 for `f64`). No
  conversion to a float traps, in a debug build either.
* **Float literal types.** A float literal has no committed type: it adapts
  to `f32` or `f64`, whichever its destination or other operand has, and is
  an `f64` where nothing gives it a type. So does every float whose
  float-ness comes only from float literals and the integers they meet:
  `n * 0.5`, `-(i + 0.25)`, `(n - 1) * 2.5` with integer `n` and `i`, and a
  construct whose branches are all such (§6.4). Such
  an expression is computed at the type it adapts to, throughout (an `f32`
  argument, field or operand gets `(n as f32) * 0.5` in 32 bits), and is an
  `f64` where nothing gives it a type. A variable, parameter, call result or
  explicit `as` commits a type; §7.7 describes the limited case where an
  `f32` destination chooses a generic call's computation width first.
  A constant part of it is folded at full
  precision and rounds once to the type the whole adapts to, as a constant
  does anywhere: beyond `f32`'s range (about ±3.4e38) to an infinity, as
  `as f32` rounds.
* **Float literal `let`s.** A `let` (or `const`) with no written type, local
  or global, whose initializer is such a float
  (`let a0 = i * 6.2831853 / n;`, `let K = 0.25;`, std's `PI`) is
  literal-like as well. It holds its value as an `f64`, computed once, and
  each use adapts as a float literal there would: at an `f32` destination,
  beside an `f32` operand (`a0 * r` with `r: f32` is an `f32` multiply), in
  a construct's branches, and as a generic argument beside an `f32`. Such a
  use takes that `f64` rounded once to `f32`; where nothing gives it a type
  it is the `f64`. So `let h = n * 0.1;` stored in an `f32` is `n * 0.1`
  computed in 64 bits and then rounded, which can differ in the last bit
  from the same expression written at the `f32` destination, computed in 32
  bits throughout. A use that adapts it relies on it keeping its value, and
  a writable reference to it could change that value to any `f64`. So, as
  for an integer named constant (§3.1), such a reference is an error in
  either order; a reference to it is an `f64&`. A `var`, and a `let` with a
  written type, are values of their type. Unlike for an integer named
  constant, this holds for local `let`s too: adapting a float changes only
  how it rounds, as writing the literal there would, whereas adapting an
  integer local would change the width at which its arithmetic wraps.
* **Never implicit**: narrowing; same-width signedness changes (`i32 ↔ u32`);
  anything signed into any unsigned type (a negative value can hide in any
  signed operand, so `u32→i64` is silent but `i32→u64` is not); `u64` into
  any signed type; `f64 → f32`; and float → int.
* `x as T` — explicit conversion between any two numeric types,
  **range-checked in debug** (abort on value change), truncates/wraps/
  rounds-toward-zero silently in release. A conversion to a float is never
  checked, in any build: from an integer it rounds as the implicit one
  does, from an `f64` to the nearest `f32`, as float arithmetic rounds every
  result it computes, and a value beyond `f32`'s range (about ±3.4e38)
  becomes an infinity. None of these is something a debug abort would
  usefully catch.
* `x as! T` — always-unchecked wrap/truncate, for when losing bits is what
  is intended, even in debug. To a float it is the same conversion as `as`.
* Into an index or bound type (§3.11), `as` first checks the value against
  the array in every build, and `as!` is an error.
* float → int (both forms in release, `as!` always) is defined exactly:
  truncate toward zero, then wrap modulo 2^64 into the target's width; NaN
  yields 0. The common in-range case is a range test and a hardware
  conversion; only the out-of-range tail pays for the defined wrap.

**Redundant casts.** An `as` or `as!` that changes nothing is a warning.
Such a cast is one whose operand has its type already, or converts to it
implicitly where the cast stands, so that deleting the cast leaves every
type, operation, value and call of the program as it is. `n as i32` of an
`i32 n` warns, as do `17 as u32` passed to a `u32` parameter, `b as i64` of
a `u8 b` stored in an `i64`, and `w as f32` in `(w as f32) * 0.5` passed to
an `f32` parameter, where `w * 0.5` is an `f32` multiply too. A cast that
decides anything stays silent: a type nothing else gives
(`let k = 5 as u32;`, `print(b as u8)`), an operation's width or signedness
(`(a as i64) * b` with `i32` operands), a float's rounding
(`(n as f32) * 0.1` stored in an `f64`), a call's overload or a generic's
instantiation. Of two casts that cannot both be removed, as in
`(a as f32) * (b as f32)` of two integers, the right one warns. Of a cast
directly inside another, where both could be removed, the outer one warns.
Casts in a generic function's body, in a function value's body, and in what
is declared in either are exempt from this warning: another instantiation
may need the cast. The warning comes once per cast as written, where every
check of it agrees.

### 6.4 Control expressions

```goose
if c { e } else { e }        // expression; both arms required when used as value
while c { s }
for v in e { s }             // see §6.5
loop { s }                   // infinite; exit via break
block { s }                  // early-out construct: break E exits with value E
{ s }                        // a plain scope with a value; not a break target
guard c else { s } …         // if c { … } else { s }: … is the rest of the block
guard c; …                   // if c { … }
match e { ... }              // §8
return e? (from f)?          // §7.3, §7.9
break e?                     // exits innermost loop/block, optionally with value
continue
```

`block { }` exists to promote early-out style anywhere, not just at function
top level. `break` binds to the innermost `loop`/`while`/`for`/`block`;
labels are not in v1. For definite assignment (§4.4) and narrowing (§3.8),
what holds after one of these is what holds at every way out of it: each
`break`, and the end of a `block`'s body, a `while`'s condition found false
or a `for`'s last iteration; a `loop` has only its breaks. All `break E` of one construct must agree on E's
type: a later break's value constructs into the type an earlier one gave
the construct.

A construct's branches (its arms, its tail and its breaks) join into one
type. A literal branch adapts to the others' type, as at any typed
destination (§3.1). An integer constant adapts to an integer type it fits
(`if c { x } else { 0 }` has `x`'s type, a `u8` for an `x: u8`), and a float
literal, or a float of literals and integers (§6.3), adapts to an `f32`
(`if c { x } else { 0.62 }` is an `f32` for an `x: f32`). An integer branch
beside a float one converts to the float's type (§6.3):
`if c { n } else { 0.5 }` is an `f64` for an integer `n`. A `[]` branch
adapts to no other branch: it takes an element type only from a destination
(§4.2).

A construct whose branches are all literals is itself literal-like wherever
it meets a type, as at a typed destination:

* integer constants adapt to any integer type every one of them fits
  (`g(if c { 1 } else { 2 })` passes an `i32` to `fn g(x: i32)`, and
  `(if c { 200 } else { 100 }) + b` is a `u8` add for a `b: u8`), and are an
  `i64` where nothing gives them a type;
* float literals, and floats of literals and the integers among them, adapt
  to either float width (`k(if c { 0.5 } else { 0.25 })` passes an `f32` to
  `fn k(x: f32)`), and are an `f64` where nothing gives them a type;
* branches that are all `[]` take the array type they meet
  (`h(if c { [] } else { [] })` passes an `i64[]` to `fn h(x: i64[])`), and
  are an error where nothing gives them one.

A break with such a number settles no type for the later ones:
`loop { if c { break 1; } break 2.5; }` is an `f64`. A bare `{ … }` in
expression position (a match arm of several statements, for example) is only
a scope: `break` inside it still leaves the enclosing loop.

Arrays and slices of one element type join as a slice. This happens where
one branch's value is a slice (a string literal, for example) and another's
is an array of the same element type, or where the construct's destination
type is such a slice (an annotated variable, a declared result, a parameter
declared as a slice). An array that a branch gives is then a whole-array
slice of that array itself (§3.10) rather than a copy of it (§4.1):
`let name = if item.count == 0 { "none" } else { item.name };` makes `name`
a `const u8[:]`, which views the `u8[..48]` `item.name` where that branch is
taken. The slice is read-only where any branch is: a `const` slice (a string
literal among them), or an array a view of which would be (§9.5). Like any
slice it must not outlive what it views (§9.2): an array declared in the
branch, or a temporary made there (a call's array result, an array literal),
is an error. Arrays of two different types join this way only where a slice
joins them as well, or where the destination is a slice; otherwise they do
not agree. A `break` agrees with the first: after a slice, a break's array
is a slice of itself, while an array that comes first is what a later slice
constructs, where it can.

`guard` guards the rest of the block it is in, whatever kind of block that
is: a function's body, a loop's body, a branch, a `block`, a plain scope, or
a function value's body (§7.6). So `{ s1; guard c; s2 }` is
`{ s1; if c { s2 } }`, and `{ s1; guard c else { s3 } s2 }` is
`{ s1; if c { s2 } else { s3 } }`. The code after a guard runs only where
`c` holds, and is checked on that basis, including flow-narrowing of `T?`
(`guard r else { return; }` leaves `r: T&`). A guard does not jump anywhere:
`guard c;` skips the rest of its block. Directly in a function's body this
returns from the function, in a loop's body it goes on to the next
iteration, and in a function value's body it ends that call of the value,
not the function running it. Leaving more than the block is the `else`
block's job: `guard c else { break; }` leaves the loop,
`guard c else { return; }` the function. An `else` block that does not leave
runs instead of the rest of the block, which then ends. Where the rest of
the block ends in a value, the `if` is the block's value
(`block { guard ok else { break 0; } n * 2 }`); a guard is a statement of
its block, never an operand. What is declared after a guard is declared in
the `if`'s block, so a nested function declared before the guard cannot call
one declared after it (§7.5).

### 6.5 `for`

Built-in iteration only (no iterator protocol):

* `for i in a..b` — integer range, half-open `[a..b)`; the bounds unify per
  §6.1 and `i` runs at that type (an `i32` range gives a 32-bit loop variable).
* `for i: T in a..b` — the same range with `i` of the integer type `T`: each
  bound is a value of `T`. A constant bound adapts as at any typed
  destination (§3.1), and a typed bound converts only by implicit widening
  (§6.3), so with an `n: i64`, `for i: i32 in 0..n` is an error asking for
  `n as i32`. The end may be one past `T`'s largest value, where `T` is
  narrower than 64 bits: `for i: u8 in 0..256` visits every `u8`, and its
  counter never overflows. Such an end is written with literals alone
  (`256`, `255 + 1`): an end naming a constant is a value of `T` as any
  bound is, computed at run time as the same expression is anywhere (§6.2).
* `for i in n` — sugar for `0..n`; `i` has `n`'s type. `for i: T in n` is
  `for i: T in 0..n`.
* `for x in arr` — element copies for fixed-size elements, at the element's
  type; element references for non-fixed ones (§4.1), whose walk is
  sequential. The copy cannot be written to (`x.f = 1` is an error naming
  `&x`), since the write would update only the copy. An element that *is* a
  relative reference (§3.9) binds as the
  loaded plain reference, exactly as indexing it gives; an element with
  relative references *inside* it cannot be copied out of its root at all,
  and needs the `&x` form.
* `for &x in arr` — element references (the mutation form for fixed-size
  elements; redundant, and a warning, for non-fixed ones). Over a `[>..<]`,
  the binding is a reference in scope: no shrink inside the loop (§5.2).
  An element that is itself a reference binds as the reference it holds
  (loaded if relative; there are no references to references), pointing
  where the element does by the read-back rule (§9.5), not into the array.
  `&x` makes that binding one that writes through, and a varint-width
  relative element, which is non-fixed, binds that way without the `&`.
* `for x, i in arr` / `for &x, i in arr` — with index (`i: i64`).
  `for x, i: T in arr` gives the index the integer type `T` where `T` holds
  every index `arr` can have. A fixed-size or static-capacity array's length
  bounds its indices, while any other array's or slice's index can reach
  2^48 − 1 (§10.4), which no type narrower than 64 bits holds, so such an
  index is converted with `as` in the body. A range's or count's index
  counts iterations as an `i64` and takes no type; an element binder has its
  element's type and takes none either.

All array-family types and slices are iterable. Custom access patterns are
provided by HOFs taking static function values (§7.6), which compile to
plain loops.

Range/count bounds are evaluated once, before the first iteration; an
empty or reversed range performs no iterations. `continue` advances to the
next index or sequential element just as reaching the end of the body does.

A loop iterates its sequence in place, not a copy of it. Traversal of a
resizable array tests the current length before each iteration, so
permitted growth can add elements to the traversal and a shrink ends it
early; its elements never move. A slice keeps its own length: growing its
source does not extend that view. A slice, and anything else the loop walks
in place (a fixed or limited array, an element or a field of a larger
value), is live for the whole body, as a slice variable would be: a shrink
of a resizable array it lies in or views is an error, however the body
reaches it (a call, a function value; §5.1, §5.2). An index on the path to
the sequence is evaluated once, but the references and slices on it
(`refs[i].items`) are loaded again on every iteration, so the arrays they
lie in may not shrink either. Iterating a copy (`let row = a[i]; for x in
row` over fixed-size rows), or a reference read out first (`let r .=
refs[i]; for x in r`), leaves the array free.

**Specialization-time expansion.** `const for` specializes a fragment once
per entry of a compiler-known sequence. Its v1 sources are `fields<T>` with
two bindings (field name string and field accessor), `variants<E>` with two
bindings (variant name string and variant), `types<T1, T2, ...>` with one
type binding, and `[c1, c2, ...]` with one constant binding. `fields`,
`variants` and `types` are contextual identifiers in this header, not
keywords or ordinary function calls. Bindings use required angle brackets:

```goose
fn visit_fields<T, F>(value: T&) {
    const for <N, A> in fields<T> { F(N, &value.A); }
}
fn transform_fields<T, F>(value: T&) -> T {
    T { const for <N, A> in fields<T> { A: F(N, value.A), } }
}
fn each_case<T, F>(value: T) {
    match value { const for <N, V> in variants<T> { V c => F(N, c), } }
}
```

`fields<T>` visits direct, non-padding fields of a concrete struct or
variant type (a variant's payload) in declaration order. Its accessor
identifies the field of that particular struct instantiation: another struct
with the same field name does not match. A variant's accessor reaches a
value of that variant type only, not the ADT before it is matched. It can
appear in a member access or initializer label. `variants<E>` visits every
variant of a concrete ADT in declaration order, with or without payload; its
variant binding is written where a variant's name is: a type, a constructor,
a `match` pattern with or without a binder, and, for a variant without
payload only, a value. It is not stored, passed or turned into a number, and
its name string is not a persistence identifier. Bindings follow lexical
scope and shadowing, including in nested functions. Types and constants may
be heterogeneous; all three sources may be empty. Constants are scalar or
string literals, named global constants, and the integer constant
expressions supported for array sizes, including enclosing expansion
bindings. There is no arbitrary compile-time execution.

A statement fragment is a separate scope for each entry. A constructor
fragment contributes comma-separated initializer entries, flattened before
ordinary default, completeness, ordering and destination checks. A `match`
fragment contributes arms, flattened before the checks of §8.1: every
variant once, `_` last, and the reference-binder and lifetime rules for each
generated arm. A generated arm may bind a variant without payload, to an
empty value of its variant type. An
expansion produces no runtime iterator or reflection metadata. Its body
contains ordinary runtime code, with the usual reference, lifetime and
construction rules. Returns retain their lexical targets; a `break` or
`continue` cannot cross an expansion boundary. The full v1 rules and
implementation limit are in [the `const for` design](design/const_for.md).

### 6.6 `defer`

```goose
defer stmt;                  // an assignment, an increment, or an expression statement
defer { … }                  // a block
```

A `defer` statement's body runs when the block the statement is written in
ends, by any exit: the end of the block's text, `break`, `continue`,
`return`, a function value's `return` (§7.6) through the call running it,
or a `return … from` (§7.9) passing through the function. It runs once per
exit of the block, so a `defer` in a loop's body runs at the end of every
iteration. An exit runs the bodies of the `defer` statements it has run
past in the blocks it leaves, innermost block first and latest first within
a block, so a body runs before the storage declared before it is released
and may read it. The body is evaluated at the exit, not where it is
written: it reads variables as they are then. At a `return`, the returned
value is constructed at its destination first (§7.3), then the bodies run,
then the block's storage is released.

Restrictions, all checked statically:

* The body cannot leave: no `return`, `break` or `continue` in it may
  target anything outside it, a `return` in a function value written in it
  included.
* The body uses no long-distance returns: it holds no `return … from`, and
  calls no function that performs or propagates one, catches one, or calls
  one that does, transitively. The body may run while such a return is in
  flight (§7.9), and a second one, a contained throw and catch included,
  would disturb the first's state.
* The body does not name a local the function returns by name: that local
  is built at its result destination (§7.3), and is complete when the body
  runs.
* The body is checked where it is written: it names what is declared
  before it, and a `var` optional narrowed before it is not narrowed in it,
  since an assignment before an exit may have undone that. What the body
  assigns is not assigned after the statement, since the body runs at the
  exits.
* A `defer` is a statement of a function's body, or of a block in one; a
  global initializer has no scope exit.

A `defer` body does not run where no exit happens: `abort`, a failed
`assert` or bounds check, `exit`, a contained worker's abort (§11.2), and
workers still running when `main` returns. The process or the worker's end
reclaims what the body would have released.

---

## 7. Functions

### 7.1 Declarations and generics

```goose
fn name(a: i64, var b: f64, xs: i32[:]) -> i64 { ... }
fn generic<T>(a: T, b: T) -> T { ... }
fn also_generic(a, b) { ... }        // untyped params are generic
fn scaled(a: i32, b: i32 = 0, c: f32 = 1.0) -> f32 { ... }   // scaled(2) is scaled(2, 0, 1.0)
```

* There are only free functions: no methods and no impl blocks. UFCS:
  `x.f(a)` passes `x` as `f(x, a)` does. Resolution tries fields and
  built-in members first, then functions, which it looks for in the
  namespace of `x`'s type first (§11.1).
* Parameters are `let` bindings by default: they are not reassigned, and
  their contents are as writable as their type says (§4.4). `var` makes the
  (by-value) parameter a reassignable local.
* A parameter with no type annotation is generic (Lobster-style). Explicit
  `<T>` parameters express same-type constraints and let signatures name
  types. Return types may be omitted where they can be inferred (they are
  required across recursive cycles, §7.8). An omitted result type is taken
  from the first return checked, the way an un-annotated `let` takes its
  type from its initializer (§3.8, §4.1). The result is a reference if that
  return gives a reference to a non-fixed value, a non-fixed lvalue other
  than the function's own local (which the return moves, §7.3), or an
  explicit `&x`. `fn args(c: Ctx&) { c.args }` returns what the field
  refers to, just as `c.args` would, and `copy(args(c))` copies it. A
  reference to a fixed-size value is returned as its pointee. A part of the
  function's own local, or of a temporary, dies with that local or
  temporary, so a reference to it cannot be returned in either case: write
  `return copy(x)`. An omitted result type cannot be taken from a `[]`,
  which has no element type of its own (§4.2).
* A trailing parameter may declare a **default value**, as in
  `c: f32 = 1.0`. The parameters after one with a default have defaults
  too, and a parameter with a default has a type (an untyped parameter's
  type is its argument's). A call may leave out any number of the trailing
  parameters that have defaults. Each parameter left out takes its default,
  which is evaluated at that call, after the written arguments, in
  parameter order, and again on every call: `scaled(2)` above is
  `scaled(2, 0, 1.0)`. The default is a value of its parameter's type, as
  the written arguments and any explicit type arguments determine that
  type, and it adapts as an argument would: `fn scale<T>(x: T, by: T = 2)`
  doubles a `u8` and an `f64` alike. A default is an error at a call where
  it does not fit: `fn back<T>(x: T, by: T = -1)` does not accept a `u8`.
  A default does not bind a type variable itself: to leave `b` out of
  `fn f<T>(a: i64, b: T = 0)`, the call is written `f<i32>(1)`. Each call
  that leaves a default out checks the default there, as if it were the
  argument written in its place. So a default may not lead to a call that
  takes the same default again, just as a field default may not lead to a
  construction that takes it again (§3.2): `fn f(a: i64 = g())` beside
  `fn g(b: i64 = f())` is an error. A named function passed as a static
  function value keeps its defaults (§7.6). The parameters of a block
  literal and of a `thread_fn` (§11.2) have no defaults, and neither does
  a tag-dispatched call (§8.2).
* A default can name what a top-level declaration could name, in the
  declaration's namespace: globals, constants, top-level functions and
  types, and the type parameters that the call binds to types. Those are
  the function's own and, for a nested function, those of the functions
  around it (§11.1). A default never refers to a local of the code calling
  the function, whatever its name. A name that means something else in the
  declaration's own scope is an error in a default; it does not refer to
  the global of that name instead. Such names are a parameter of the
  function (`::x` names the global `x`), a type parameter bound to a
  function value (§7.6), and, for a nested function (§7.5), a variable or
  nested function around its declaration, or a type parameter bound to a
  function value of a function around it.
* Overloading by parameter types is allowed, and resolution is static. The
  unique concrete exact match is chosen first, then a generic exact match,
  then a match requiring coercion (array→slice §3.10; literal fit §3.1,
  which includes an array literal's constants, §4.2; implicit widening
  §6.3). A candidate's rank is its worst argument rank. Ties are errors;
  there is no further tiebreaking by specificity or declaration order.
  A default does not count as an argument: when a call leaves some of a
  candidate's parameters to their defaults, the candidate ranks by the
  written arguments alone. So `f(a, b = 0)` beside `f(a)` makes every
  one-argument call of `f` ambiguous.
  Only if no ordinary candidate matches is tag dispatch (§8.2) tried.
  A candidate that converts an integer argument to a float (§6.3) is the
  last resort, after tag dispatch and after a builtin of the same name:
  `f(2)` calls `f(i32)` beside `f(f64)`, and `abs(n)` calls std's
  `abs<T>`, not `abs(f64)`. An integer converts to either float type
  equally well, so two such candidates are ambiguous: with an integer `n`,
  `sqrt(n)` has to be written `sqrt(n as f64)`, while `sqrt(n * 0.5)` calls
  `sqrt(f64)`, as `sqrt(0.5)` does.
  The expected result type never selects an overload. An `f32` destination
  may refine an otherwise unconstrained floating generic computation (§7.7),
  provided resolution still selects that same overload.
* Multiple return values are declared as `fn f() -> A, B`. They are
  received as `let a, b = f();`, or assigned to existing places as
  `x, xs[i] = f();`. In such an assignment the call runs first, then each
  target is assigned its result in order, as `x = f()` would assign one
  result (a non-fixed result is built at its target; a reference to a
  non-fixed value needs a `let` and an explicit `copy`, §4.1). Only `=`
  takes several targets.
  There is no tuple *type*: multiple returns are a calling convention, and
  structs are the way to keep data together. (Function *types* with
  multiple returns need parentheses, as in `fn(i64) -> (A, B)`, to
  disambiguate the comma; declaration headers do not.) Nonfixed types are
  allowed in any return position; each nonfixed result gets its own
  destination per §7.3 (possibly on distinct stacks).
  An ordinary value context takes only the first result; a call statement
  discards them all. Only `return` and calls carry several values: a block
  or branch cannot end in `a, b`, so `return if c { 1, 2 } else { 3, 4 };`
  is an error, and each branch has to return its values itself. A
  multi-name binding requires exactly that many results. `return f()`
  forwards all of a multi-result call's results, adapting each to its
  corresponding declared return type.

Declarations at top level are order-independent (whole-program compile);
only global initializers have ordered semantics (§11.1).

### 7.2 Parameter passing

Parameter passing works like `=` (§4.1): the parameter's type is the
destination, and the argument connects to it by size class. A fixed-size
argument is copied into a by-value parameter and bound by a reference-typed
one (`fn inc(n: i64&)` is called as `inc(n)`; `inc(&n)` means the same and
draws a warning). A non-fixed lvalue argument binds by reference to a
reference, slice or untyped parameter, and is an error for a by-value one.
`f(copy(xs))` writes the copy out explicitly, and `f(make())` constructs
the result in the parameter's slot (§4.3). Array→slice conversion at call
sites (§3.10) is the only change of shape; it is cheap and never copies.
For a by-value non-fixed parameter, the call site reserves the statically
assigned stack region for the value and constructs the value there, and
the callee owns it like any local. A parameter that the call leaves to its
default (§7.1) receives the default in the same way, as if it were the
argument written in its place: `xs: T[>..] = []` builds an empty array in
the slot, and `r: i64& = counter` binds the global.

### 7.3 Return values and result placement

**Normal completion.** The checker infers whether each function
specialization can resume its immediate caller. A body whose paths all
terminate, loop without an exit, or `return ... from` an outer function
does not resume that caller; this propagates through ordinary calls and
static function values. A variant-dispatched call has this property only
when every case does. A return targeting the function itself, including
one taken by a nested callee, resumes its caller normally.

A function declared to return values needs no result on a path ending in
such a call. For example, `fn fail() { abort("failed"); }` permits
`fn value() -> i64 { fail(); }`. This is a control-flow fact, not a value
type: `fail()` still produces no value to pass as an `i64` argument or to
forward in an explicit `return fail();`.

After a proven exit, the remaining statements and trailing expression in
that block are neither typechecked nor emitted. They still must parse.
This does not make checking depend on constant folding: both branches of
`if false { ... } else { ... }` are checked. Unknown recursive calls are
assumed to return, so inference may miss a recursive function that never
returns; it does not skip a continuation merely because the recursive
callee has not yet been checked.

Fixed-size returns use registers/native stack or a caller-owned data-stack
slot for large values (C.3). A variable or resizable return value is
**constructed directly in its destination** (§4.3). The callee is compiled
knowing the destination stack (statically or as a hidden argument), writes
element data there, and returns the value's metadata (lengths) outside the
element run. Where a result is first built with its packed value header,
the header is removed in place under §4.3's relocation exception
(Appendix C.3):

* `let x = f();` — a fresh stack region (statically assigned to `x`).
* `v.push(f());` / `v.append(f());` — the top of `v`'s stack; on return,
  the push only adjusts `v.len` (the data is already in place).
* `g(f())` — the argument slot of `g` (its statically reserved stack).
* `x = f();` (resizable `x`) — `x`'s stack, replacing its contents, which
  `f` may therefore not use (§4.4).

**Named results (guaranteed NRVO).** When every `return` of a nonfixed
result returns the same local variable (and those returns are its last
uses), that local is allocated at the return destination from its
declaration, so `return x` costs the same as returning the constructing
expression directly. The same holds where the other returns produce values
whose construction does not read or write that local, such as `return []`
or `return str("none")` beside `return x`. Such a value is an exit's value
as described under **Exits** below: it is built behind the local and moved
down over it. When different locals are returned on different paths, only
one of them can be placed at the destination, and the others are copied on
return. A returned local that another return's value may read or write is
copied as well. The compiler warns where a return keeps a returned local
out of its destination in this way. This is in addition to §4.3's layout
relocation.

**Exits.** A `return` or `break` may be taken while its destination already
holds part of a value: for example inside an element of a literal being
built there, or, for a `return from` (§7.9), below a named result built in
the destination the target handed down. Such an exit abandons that part.
The exit's own value is constructed behind the abandoned part and moved
down over it; only such an exit needs this copy. The `defer` bodies of the
blocks an exit leaves (§6.6) run after the exit's value is constructed and
before the blocks' storage is released.

**Destination requests.** The construct-here information handed to a callee
(or any constructing expression) comes in one of two forms, chosen by the
receiver:

* *one value of type R*, for receivers that store a value (`let`, params,
  `=`, `push` of one element). R's nested metadata, if any, is part of the
  constructed bytes.
* *a run of elements of type T*, for receivers that splice (`append`). The
  callee emits raw elements and returns the count.

The request kind is part of the specialization signature (§10.2); the same
function may be compiled in both forms for different callers. A callee that
merely constructs its result passes the destination through. A callee that
must first operate on its result as a whole array may make one copy into
the requested element run, just before returning. Where its packed value
can occupy the receiving storage, removing the header is instead the
layout relocation of §4.3 and may be performed by the callee or receiver.
A local resizable can use metadata outside the element region
(Appendix C.2), with its elements already in place.

As a result, returning a built-up value costs the same as out-parameter
style, and the idiomatic way to build a variable element "inside" a
container is a function call in argument or push position.

### 7.4 Growing through a reference

Passing `&v` where `v` is resizable lets the callee push or append to it.
For each call site the compiler either specializes on the identity of the
stack that `v` tops (§10.2), or, when one body serves multiple stacks,
passes the stack as a hidden argument or fat reference (Appendix E).
No surface syntax distinguishes these.

An in-scope "current pool" mechanism (allocation without naming the array,
compile-time bound to whichever suitable array is in scope) is deferred
(TODO).

### 7.5 Nested functions and free variables

Functions may be declared inside functions. A nested function may read and
write the enclosing function's locals ("free variables"), subject to those
variables' normal rules (writability §9.5, roots §9.2, shrink rules §5).
For definite assignment (§4.4), a call assigns a free variable only if
every way out of the nested function's body assigns it. The ways out are
each `return` (including those of a function value's body and a callee's
`return … from`, §7.6, §7.9) and the end of the body. Where any of them may
have assigned the variable, a `let` declared without an initializer cannot
be given a value after the call.
A function value's body (§7.6) encloses what is written in it in the same
way: a function declared there, or a function value written there, may also
name the value's parameters and the body's locals.

Names in a nested function resolve where the function is declared, whatever
surrounds a call of it. A free variable is the variable in scope at the
declaration. It is not a variable of the same name that a scope around the
call declares, or that is declared further on, and it is never a variable
that one of the function's own type parameters hides (§11.1). The
functions a nested function can call are those in scope at its declaration
and every function declared in the blocks around its declaration, before or
after it. So nested functions may call each other in either order, as the
functions of a recursive descent parser do. Calling one of them before the
enclosing code has reached its declaration is an error. The enclosing code
can name a nested function from its declaration to the end of its scope,
and may pass the name directly as a static function argument (§7.6).

Nested functions with the same name declared in one block overload each
other as top-level functions do (§7.1), so case functions (§8.2) may be
nested and use free variables. A name refers to the overload set of the
innermost block that declares it. For the enclosing code, that set holds
the members declared so far; for a nested function's body, it holds all of
them, declared before or after the body. A later nested function with the
same parameter types as an earlier one shadows it instead of overloading
it, as a later local of the same name does. The enclosing code then calls
the latest one declared so far, and a nested function's body calls the
latest one declared at or before it, or else the first one after it. A set
in an inner block entirely hides the set of the same name in an outer
block, and the top-level one as well, as a namespace's set hides the
global one (§11.1).

Implementation model: free variables become hidden reference parameters of
the nested function. A nested function's value never outlives the function
that declares it (function values do not escape, §7.6), and a call made
where a variable the function names is out of scope is rejected, so every
call can pass the free variables. A nested function is specialized by how a
call finds its free variables, as it is by its parameters (§10.2): where
each one points, what it holds, whether it is assigned, and, for a `let`,
whether it may already have been assigned. A function that calls the
nested function is specialized in the same way, and so is a function that
calls a function value naming such a variable. A call that shares a
specialization with an earlier call leaves the free variables as the
earlier call's check did: rebound, assigned, and holding what the body
stored.
When a nested function is passed as a static function value and inlined
into its HOF, which is the expected, common case, the hidden parameters
disappear; un-inlined (debug) builds keep them as real arguments.

### 7.6 Static function values

```goose
fn foo<F: fn(i64)>(a: i64) { F(a); }
foo(1) { print(it) }                  // trailing-block sugar, implicit `it`
foo(1) { x => print(x) }              // named block params
fn h(v: i64) { print(v) }  foo(1, h)  // or pass a named function
```

Function values are compile-time entities passed as generic parameters, not
runtime data. The `: fn(...)` bound is optional documentation, and a bare
`<F>` works the same: the call `F(a)` is typechecked per instantiation like
everything else (passing a non-function produces the error at that call,
reported with the instantiation chain). Wherever `F` is in scope it names
the function value, hiding a variable of that name around a nested
function's declaration, and a global or function of that name (§11.1). It
is called as `F(a)`; a member call `a.F()` also names it, and is an error.
A block's value is checked without the destination **type** that its
call's value goes to, so it cannot be a `[]` (nor a construct whose
branches are all `[]`), which would have no element type (§4.2).

Its receiving storage is supplied independently of that type inference:
relative links in a fresh result are checked against where it will be
constructed, including when the block is called from another function.

A fresh block result follows the same construction rules as a named
function's result, including literals containing self-relative references.
Selecting a fresh result through `if`, `match`, `loop`, or `block` preserves
that property. A tail that instead names existing storage (or a reference
to it) is still a copy and must obey §3.9's relative-reference restrictions.

A function argument must be a function name (including a bound generic
function parameter) or a block literal. A name with several overloads
passes all of them, and each call of the value resolves among them as a
call by that name would. So `each(xs, sort)` with
`fn each<F>(xs: i64[:]) { F(xs); }` calls std's one-argument `sort<T>`, and
one value may reach different overloads at different calls. Runtime
expressions producing a function value, such as a call, `if`, `match`,
`block`, or `loop`, are rejected. Evaluate runtime work in ordinary
statements before the call; use locals when its ordering with other
arguments matters. A named function passed as a value keeps its parameter
defaults (§7.1): `F(x)` may leave out the arguments it declares defaults
for. A block's parameters have no defaults. A function value passed as an
argument may come after fewer arguments than the callee has parameters,
when the remaining parameters have defaults: `each_step(5, show)` for
`fn each_step<F>(n: i64, step: i64 = 1)`.

Function values capture enclosing locals per §7.5, and cannot escape:
storing them, returning them, or putting them in data is a compile error.
Every call is direct and inlinable; HOFs compile to hardcoded loops
(Lobster-style guarantee). There are no closures-as-objects and no runtime
function pointers in v1.

A plain `return` inside a function value returns from the **lexically
enclosing named function**, not from the HOF that calls the value, so a
HOF-based iterator has the same structuring power as `for`. This is
implemented with the `return from` mechanism (§7.9), which unwinds the
HOF's frames transparently.

### 7.7 Generics and monomorphization

Generics use monomorphic specialization, C++/Lobster-style. Type
parameters are unconstrained, and the body is re-typechecked per unique
combination of (type arguments, reference roots (§9.2), writability
provenances (§9.5), static function values, destination stacks). Errors are
reported at the offending instantiation **with the compile-time call
chain**: because the compiler sees the whole program and checks it in
call-graph order, it can always show which call path produced the failing
instantiation. Every instantiation the program reaches is checked, so there
must be finitely many: a recursive call may not keep making new ones
(§7.8), nor may a generic type's fields (§3.2).

Type variables are never checked abstractly. A generic body is checked only
at an instantiation where every type is concrete. That includes the result
of every call the body makes on a function-value parameter, since that
value's body is checked inline against the concrete argument types at that
point (§7.6). So a HOF never needs to state what its function value
returns: `map`'s result element type is the type of `F(x)` in that
instantiation, even when the block is `{ generic(it) }` and that type
depends on the instantiation.

**Call-site type arguments.** Type arguments are inferred from the argument
types whenever they appear in the parameter list: `fn foo<T>(x: T)` can
be called as `foo(1)` without an explicit type argument. The typechecker
must support inference for both `<T>`-style and untyped (implicitly generic)
parameters. Where several arguments mention one type variable, the typed
arguments bind it and a literal then adapts (`max(n, 0)` with `n: u32` is
the `u32` instantiation). So an `i64` literal never fixes the type by
coming first, and neither does an array literal of constants (§4.2). Among
literals, a float literal binds before an integer literal, which converts
to the float type (§6.3): `max(1, 0.5)` is the `f64` instantiation. A float
expression of literals and integers (`n * 0.5`) binds as a float literal
does. A `[]` or `null` binds nothing. It takes the type that the other
arguments, literals included, give its parameter, wherever it stands among
them: `both([], xs)` for `fn both<T>(x: T[], y: T[])` is the same
instantiation as `both(xs, [])`. An explicit list `f<i64>(x)` is allowed.
It is *needed* when no argument mentions the parameter (e.g. `qget<i64>()`,
or `zero<f64>()` for `fn zero<T>() -> T`), or when only a `[]` or `null`
does (`g<i64>([])` for `fn g<T>(xs: T[])`). It binds the leading type
parameters in order, and the rest are inferred. Syntactically, `f<` commits
to a type argument list only when the `<…>` is immediately followed by `(`,
by `{` for a struct literal (`Pair<i64> { … }`), or by `.ident {` for a
variant literal (`Opt<i64>.Some { … }`); otherwise `<` is the comparison
operator.

**Floating computation context.** After ordinary argument inference and
overload resolution, an `f32` destination can refine a call's inferred
`f64` type parameter to `f32`. This applies when the declaration returns
that bare type parameter (`-> T`) and every parameter mentioning it is a
written, bare value parameter (`x: T`). Its arguments must be adaptable
float literals or expressions (§6.3), adaptable integer constants, or directly
nested calls satisfying this same rule. Matching those arguments as `f32`
must select the same overload with all other bindings unchanged; ambiguity
or a different selected overload prevents the refinement. Normal resolution
must succeed first: result context never rescues a failed or ambiguous call.

With integer `width` and `height`, the destination below therefore chooses
`f32` for both calls and their argument arithmetic:

```goose
let scale: f32 = max(0.5, min(width / 960.0, height / 600.0));
```

An explicit type argument for `T`, an already typed numeric argument, a reference or
container parameter mentioning `T`, or a default supplying such a parameter
commits its type. The rule does not infer a type parameter from the result
alone, change an integer computation into floating arithmetic, or extend
through an intervening operator or stored call result. Overload selection
still sees the call's ordinary inferred type. Without an `f32` destination,
calls continue to default to `f64`; an inferred binding such as
`let saved = min(width / 960.0, height / 600.0);` stores an `f64` result.

This is not a general change to mixed-argument unification. With
`var count: i32 = 1;`, `let value: f32 = min(max(0.25, 0.5), count);`
still errs: `count` is a typed integer argument, so result context does
not refine that call. Reversing the arguments does not make it valid.
Write `min<f32>(max<f32>(0.25, 0.5), count)` to choose floating arithmetic
explicitly for that mixture. In contrast, `count * 0.5` is an adaptable
floating expression and can participate in the contextual rule above.

An explicit inner `min<f32>(...)` also chooses `f32`. Alternatively,
`max(0.5, min(width / 960.0, height / 600.0)) as f32` computes in `f64` and
then rounds the result once: a cast does not pass its target type into its
operand. These are different choices:
`min<f32>(n * 0.1, 1.0)` with integer `n = 9` computes `0.90000004`, while
`min(n * 0.1, 1.0) as f32` computes `0.9`. A typed intermediate is also
valid, but is not needed just to choose a generic call's type.

**Literal arguments.** Where a literal is the only thing binding a type
variable (the parameter is untyped, or its type is a bare `T` that no typed
argument mentions), the variable takes the literal's default type (§3.1).
Inside the specialization the parameter is then a *literal parameter*: a
constant of unknown value that adapts wherever the bare parameter meets a
type, as the literal would have. So `push_n(flags, 1, n)` with
`fn push_n<A, T>(xs: A&, v: T, n: i64)` pushes a `u8` into a `u8[>..]`,
and `fill(small, -7, 2)` pushes an `i16` into an `i16[..4]`, with no
suffix or cast at the call. The value is not part of the specialization:
every call with a literal shares one body, which is checked once with the
value unknown. The body records each type the parameter adapted to: in the
body itself, in a callee it passed the parameter on to as a literal, and
in a block it handed the parameter to. Once the whole program is checked
and the records are complete, each call's literal is checked against every
recorded type: `push_n(flags, 300, n)` is an error naming the `push` that
takes a `u8`. What the body cannot do with an unknown value, it cannot do
with a literal parameter either: `[0; n]` needs a constant, and `v + 1` is
an ordinary `i64` expression, just as `let y = v;` commits `y` to `i64`,
so a recursive call on `v + 1` passes a typed value and terminates. `var`
parameters and `extern` functions take literals as ordinary values.

Passing arrays by reference is the generic way to write mutating range
algorithms (each array-family type instantiates its own copy); slices are
the uniform non-mutating way.

### 7.8 Recursion

Non-recursive call graphs are the default and have no special
requirements. Recursion is opt-in and annotated:

* The entry function of every recursive cycle is marked `recursive fn`.
  The keyword is all that is written: roots and destinations follow from
  the entry call as for any specialization (§7.7).
* All functions in the cycle need fully explicit signatures, since there is
  no inference across the cycle's back edge. A result inferred as a
  reference (§7.1) is an error in any of them, since the back edges need
  its roots before its returns are checked.
* Most importantly, **no function in the cycle may call into the cycle
  while one of its nonfixed locals is in scope**.

A call into the cycle is any call that can lead back to the caller: the
back edge, and every other call reaching a member of the cycle. A nonfixed
local is a variable- or resizable-class `let` or `var`, or a `match` arm's
by-value copy of such a payload. It holds a data stack (§10.3) from its
declaration to the end of its block. Its initializer builds it in place
(§4.3), so it also counts at a call in its own initializer. If a nonfixed
local were in scope at a call into the cycle, every activation would hold a
stack of its own. So a scratch buffer declared in a block or a loop body
that ends before the recursive call adds no stacks to the recursion: its
stack is free again before any deeper activation starts. A scratch buffer
declared before a loop whose body recurses is still in scope at that call,
and is an error there, reported with the local. A by-value nonfixed
*parameter* is in scope for the whole body, so no function in a cycle can
have one: it takes a reference or a slice instead. A function value's body
is part of the function it is written in, so the locals the body declares
count where it calls into the cycle, as do the locals of the function
calling it. A nested function in the cycle is a member like any other,
while the locals of an enclosing function outside the cycle are owned
outside the cycle (see below). Growable data that must outlive a recursive
call is owned outside the cycle and passed in (references, slices, reusable
pools, or one struct of references to several tables, A.6). The compiler
checks every call into a cycle in call-graph order; recursion depth then
only consumes native call stack.

**Temporaries.** The same holds for the unnamed nonfixed values of the
statement that makes the call: **at a call into the cycle, no nonfixed
value of the enclosing statement may be live**. Each such value lives on a
data stack of the activation (§10.3), a temporary until its statement ends
(§9.2) and a value under construction until it is complete, so it would
otherwise need a stack per activation, as a local in scope does. That
forbids:

* (a) a nonfixed temporary as an argument of the call, which the callee's
  view keeps alive across the call: a call's result, a `copy`, a `str`, a
  literal of nonfixed class, or the copy a by-value nonfixed parameter
  takes of its argument (§4.1);
* (b) the call nested inside a nonfixed value under construction: in an
  element or field initializer of an array or struct literal of nonfixed
  class, or in an argument of `print` or `str`, whose text is being
  rendered (`format` is not such a value: its text goes into the storage it
  is given);
* (c) a nonfixed temporary evaluated earlier in the same statement, whether
  or not something has consumed it since. A temporary lives to the end of
  its statement. So does a temporary built by the head of a construct the
  call is inside (an `if` condition, a `match` scrutinee, a `while`
  condition, a `for` sequence), since the head runs in the scope around the
  construct;
* (d) the call's own nonfixed result anywhere except in these two
  positions:
  * as the whole value of a `return` of the same result type (including
    when it reaches the `return` through the branches of an `if` or
    `match` and a block's tail, or is returned by `return … from`), where
    it is built in the caller's destination;
  * pushed or appended in place into resizable storage owned outside the
    cycle, where it is built at that storage's top. §1.3(4) then rejects
    this wherever a function of the cycle grows that storage, as every
    function appending into it does.

  Anywhere else, such as an argument, an operand, a dropped statement
  value, an element of a literal, or a value assigned (which §4.4 rejects
  as well), the result needs a destination temporary taken before the call
  and held across it.

The compiler reports each case with the call and the value. The
alternatives are to evaluate the value in a statement of its own before the
call (`let width = str(n).len;`, where the binding is fixed-size), to pass
scratch in from outside the cycle (the `mark` / `resize(mark)` pattern
below), or to take an output buffer by reference and build into it in place
instead of returning a growable value. Fixed-size values and temporaries
are never affected: a slice of a fixed literal, a struct of fixed fields
and a large fixed value (§10.3) all use no data stack at the call. So a
program's stack count is a compile-time constant, and the number of worker
threads (§11.2) is the only dynamic factor.

Scratch passed down this way can be viewed across the recursive calls. An
activation that takes `let mark = scratch.len` of a grow-shrink scratch
array, pushes onto it, views its own part and ends with
`scratch.resize(mark)` leaves the array as long as it found it, so its calls
back into the cycle are balanced (§5.2) and the view stays in use across
them:

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
```

**Polymorphic recursion.** A recursive call may instantiate its callee with
different type arguments from those of the call it is in (§7.7):
`flip<A, B>` calling `flip<B, A>` is back at `flip<A, B>` one round later,
and that call is the cycle's back edge. The instantiations a recursion
reaches must still be finite. Every branch of every specialization is
checked, whatever values reach it at run time, so a call whose types grow
with each round (`g(a, n - 1)` with `let a: T[1] = [x]` inside `g<T>`, or
`grow<T[2]>(n - 1)` inside `grow<T>`) would instantiate without end, even
where `n` bounds the recursion at run time. The compiler rejects such a
call once it would put more than 16 specializations of one function in
progress on one compile-time call path, similar to how C++ bounds template
instantiation depth, and reports it with the chain of instantiations that
led there. A finite recursion has one specialization in progress per
instantiation on its cycle, or per level of a nested type it descends
through, so only an unusually deep recursion reaches the limit.

**Cycle store rule.** Within a recursive cycle, distinct activations of the
same local are statically indistinguishable, so the §9.2 depth check is not
sufficient there. Therefore a reference whose root is a local of a cycle
function may be passed *down* as an argument, but may not be stored into
any location, nor returned. A function is in the cycle for its whole body,
not just from its call into the cycle on: the stores it makes before that
call are inside the cycle too. Rebinding one of the activation's own
reference variables (`cur .= cur.next`) is not such a store: the variable
dies with the activation, and what it is bound to came from an activation
that outlives it.
References rooted outside the cycle are unrestricted. In particular, this
covers a pool handed to the cycle by reference: a parameter whose pointee
is resizable-class (no cycle function can hand such a value on to the
cycle, since it holds none across a call into the cycle). So a recursive
builder can push into a caller's local pool and link what it pushed. A
local of an enclosing function outside the cycle (a free variable, §7.5,
of a nested `recursive fn`) also outlives every activation. So a parser
keeps its pool, its key table and its input as locals of a non-recursive
`parse`, and the nested recursive functions freely store, link and return
references rooted at them. Because the cycle's functions are checked
against the entry call's roots, every recursive call must pass such a pool
by the same reference the entry call did: swapping two pools, or passing a
different one, at a back edge is a compile error.

A reference or slice parameter that the cycle passes on is rooted outside
the cycle in the same way, whatever the size of its pointee. Suppose that:

* the call that first reached the function rooted the parameter's argument
  exactly, outside every recursive cycle (at a local of a function that is
  neither recursive nor in a cycle, or at a global), and
* every call back into the cycle passes the parameter on: the parameter
  itself, a subslice or field of it, or a parameter of the calling
  activation passed on in the same way.

The parameter then points at the same storage in every activation. References
rooted at it or read through it may be stored inside the cycle, provided the
destination storage is also the same in every activation. This can be a local, a
pool, or storage pointed to or reached through another parameter with the same
guarantee. It cannot be storage pointed to or reached through a parameter that
the cycle passes a different value. For example, a recursive walk over a context
of borrowed tables (A.6) can store views read through the context into tables
reached through that context.

A parameter whose argument on every call is a reference or slice read
through one such parameter, or through a pool, also points outside the
cycle in every activation, though not at the same storage. So what is
rooted at that parameter may be stored in the same way, but the parameter
is not itself storage to store into. For example, a builder hands the node
read through the context to the function that links it under a new node
(`rest(c, c.nodes.push(…))`). Nothing requires a recursive call to pass
such a parameter on, so the parameter is restricted only once a store
relies on it. A later call into the cycle that passes it anything else is a
compile error reported at the store, and such a call before any store makes
the store the error. (Further refinements are future work, TODO.)

**Cycle return roots.** A back edge reaches a function whose own returns may
not have been checked yet, so the roots of its result cannot be taken from
those returns right away. Instead, **a cycle is checked in rounds**:

* In the first round, a back edge's result points nowhere yet. Nothing
  checks or stores it, and the back edge does not shrink, grow, store or
  rebind anything.
* Each later round gives every back edge what the round before recorded of
  its callee: the union of the roots its returns give, one root for each
  way a return can go, mapped through the back edge's own arguments, and
  read-only where any return is. A back edge reuses the body whatever it
  passes, so it maps a parameter's root through the arguments it gives the
  parameters that the entry call gave that root (it may give two of them
  two arrays where the entry call gave one), and merges the lot as a call
  merges its callee's returns (§9.2).
* The errors that stand are those of the round after the last round that
  changed anything. That includes a variable outside the cycle, which is
  one variable at every level of a nested function's recursion (§7.5): a
  round that rebinds it, or stores into it, has read it for the next level
  as it was before.

A cycle function may therefore `return` the result of a back-edge call, and
a parenthesised subexpression in a recursive-descent parser needs no
wrapper node (`bench/goose/calc_noparen.goose`). A cycle function that
returns either of two pools it is handed can store the result where both
pools outlive the store. A cycle function that returns a view of a
grow-shrink array on any path has a result that no store may keep (§5.2). A
result that *holds* references, such as a struct with slice fields, is
checked the same way, by the roots of what it holds (§9.2). So a recursive
builder may return views into an arena that outlives the cycle, built from
its children's views, with the recursive call either before or after the
base case's return:

```goose
struct Fragment { text: const u8[:] }
var arena: u8[>..] = [];

recursive fn nest(n: i64) -> Fragment {
    if n == 0 { arena.append("x"); return Fragment { text: arena[..] }; }
    let child = nest(n - 1);
    return Fragment { text: child.text };
}
```

### 7.9 `return … from` (long-distance return)

```goose
return E from f     // f = a named function on every compile-time call path
                    //     from f to this statement
```

`f` is resolved where the returning function is written: it names a nested
function in scope, or else the name's overload set by the rules of §11.1
(`from image::parse` is explicit). The target is the innermost enclosing
call of a function in that set. An unrelated function that merely shares the
leaf name never catches the return.

`return E from f` returns `E` as the result of the innermost active call of
`f`, unwinding all frames in between. It is the language's opt-in,
lightweight "exception" mechanism (e.g. a deep parser/loader error doing
`return err from load_level`), and the mechanism behind function-value
`return` (§7.6).

Semantics and implementation:

* Validity is checked statically in call-graph order: every call site of the
  returning function must lie within the dynamic extent of an `f` call.
* Each function on a path between `f` and the `return from` gains a hidden
  return discriminant: "result is for my caller" vs "propagate for target
  `f`" (an enum when multiple targets are possible). Callers check it and
  immediately return in the propagate case. Normal epilogues run, so all
  intermediate data-stack watermarks restore correctly, and the `defer`
  bodies of the frames left run in them (§6.6); a body uses no
  long-distance return itself, so the one in flight is undisturbed. No
  unwinder or unwind tables are needed, and there are no destructors to
  run.
* `E`'s destination follows §7.3 for `f`'s call site. No special stack rule
  is needed: a pending nonfixed return's destination stack is in the in-use
  watermark from `f`'s call site through `f`'s entire dynamic extent
  (§10.3), so no function between `f` and the `return from` can own data on
  it, and unwinding intermediate frames cannot disturb the in-flight value.
  What they had built there toward `f`'s result is abandoned, as for any
  exit (§7.3).
* Multiple `return from` sites and multiple targets compose; agreement with
  `f`'s return type applies as usual.

Error handling has **no required error-value convention**: an application
may use a bool, enum, string, i64, or another type. By convention, the error
is the last of multiple return values. Use ordinary multiple returns for
local propagation and `return from` to return across several calls.

---

### 7.10 `extern fn`

```goose
extern fn sqrt(x: f64) -> f64;
extern fn sqrt(x: f32) -> f32 = "sqrtf";
extern fn read_file(path: u8[:], out: u8[>..]&) -> bool = "gs_os_read_file";
extern fn gl_vertex3(v: float3&);
```

A declaration without a body binds a Goose signature to a C function. A
string after `=`, where a body would go, is the C symbol (by default, the
symbol is the Goose name). The older syntax with the string after `extern`,
`extern "sqrtf" fn sqrt(x: f32) -> f32;`, is deprecated. It binds the same
way and draws a warning naming the `= "sqrtf"` form (except in a package
fetched from a git source, since it is up to that package's authors to
change it). A declaration giving both strings is an error. An `extern fn`
is top-level only, fully typed, not generic, and joins overload resolution
like any function. A call compiles to a direct C call with no
calling-convention extras, and a prototype is emitted from the declaration
unless the runtime declares the symbol (the `os` library's `gs_os_*`
functions, declared in `src/runtime/runtime_ext.h` and defined in
`src/runtime/runtime_os.h`). User C code is added to the program with
`--include <header>`, which is emitted after the generated type
declarations so that a header can implement shims against the generated
typedefs. The types in every `extern fn` signature are emitted even when
the function is unused, including the complete types behind reference and
slice parameters. An unused extern does not require a C implementation to
link.

Every name that comes from the program appears in the C with a `_g`
suffix: a struct `Stats` with a field `lo` becomes `Stats_g` with `lo_g`.
This keeps the generated file clear of the C keywords and of whatever the
platform's headers declare, without depending on a list of names to avoid.
A namespaced declaration `ns::x` (§11.1) becomes `ns_x_g` followed by the
namespace's length (`image::Pixel` is `image_Pixel_g5`), so `a::b_c` and
`a_b::c` stay distinct and no namespaced name coincides with a global one.
The exception is an `extern fn`'s own symbol, which is the C name the
declaration gives, unchanged.

An `extern fn`'s trailing parameters may have defaults (§7.1). Each call
passes them, so the C function always receives every argument.

These values can cross the boundary (with layouts per C.2):

* the integer and float scalars and `bool`;
* any flat fixed-size value (including structs, fixed and static-capacity
  limited arrays, and fixed-mode ADTs), by value as its packed C type;
* `T&` to such a `T`, as a pointer;
* `T[:]` of such a `T`, as the slice struct (`{ data, len }`);
* `u8[>..]&`, as the resizable's header plus its stack (`gs_rref`), which
  C appends to through `gs_bld_append(ref, p, n)`. C compiled on its own,
  such as a package's C, finds both in `goose.h` (docs/packages.md).

A return is one scalar or flat fixed-size value by value, or nothing;
references, slices and builders can only be parameters. Everything else
(varints, references inside structs, variable and resizable values by
value, slices of variable elements, optionals) is rejected at the
declaration. A parameter of reference or slice type is as writable as its
type says (§9.5): passing a read-only argument requires the parameter to be
declared `const`.

C must preserve the types and lifetimes of borrowed storage, must not
retain a borrowed argument beyond the call, and may only append to a
builder. The compiler assumes an extern may write through its arguments
but does not mutate unrelated Goose globals; C code is responsible for
honoring those obligations.

**Binding from a shared library.** An `extern` block names a shared
library and declares functions that are bound from it by name when the
program starts, without any C code of the program's own and without
linking:

```goose
extern "SDL2" {
    fn SDL_Init(flags: u32) -> i32;
    fn SDL_Quit();
    fn SDL_GetTicks64() -> u64;
    fn delay(ms: u32) = "SDL_Delay";
}
```

A member is an `extern fn` in every respect above. It is fully typed,
non-generic, and has no body. It accepts the same shapes,
with one exception: a struct holding a `pad` by value must cross by reference.
The library's struct has padding where Goose's generated C has a byte array,
and the calling convention may pass these differently. A string after `=` gives
its C symbol, which is otherwise its Goose leaf name. Its name is a
`declname`, so a namespaced member is written as it would be outside the
block. A member is written without `extern`, since the block's `extern`
covers it. `export`, `thread_fn` and the qualifiers an `extern fn` cannot
carry are errors inside the block, with the same messages as on an
`extern fn`. A block is top-level only, may not nest, and may be empty.
Two declarations may bind one symbol under different Goose signatures,
each through a pointer of its own. A member that no call reaches is never
looked up, and a library none of whose members is called is never opened.

The string is a bare name or a file name, interpreted for the platform the
program runs on:

| string | Windows | macOS | Linux |
|---|---|---|---|
| `"SDL2"` (no separator, no dot) | `SDL2.dll` | `libSDL2.dylib` | `libSDL2.so` |
| `"libfoo.so.3"`, `"foo.dll"` (a dot) | as written | as written | as written |
| `"bin/steam_api64"`, `"./x.so"` (a separator) | relative to the program's resource directory (`resource_dir()`, docs/stdlib.md) unless absolute; the bare-name rule applies to the last component |

A bare name is passed to the platform's loader as a bare name, and the
loader searches for it in its usual way: the DLL search order on Windows,
`DYLD_LIBRARY_PATH` and the usual places on macOS, and `LD_LIBRARY_PATH`,
the rpath and the cache on Linux. A dotted file name is taken literally
and is therefore specific to one platform; it belongs in the sidecar
rather than the source, unless the program targets only one platform. The
sidecar's `dylibs` list (which takes the `_windows`, `_linux` and `_macos`
suffixes, as every list does) maps a name that a block uses to the
spelling to open, as `name=spelling`. A package's sidecar maps its own
names as defaults, and the program's sidecar can map a name for any package
it uses. For a platform that the program's entry says anything about, the
program's entry takes precedence. A name that no block declares is an
error at the sidecar (docs/packages.md).

The libraries are opened and the symbols resolved on the main thread when
the program starts, after the runtime starts and before the globals'
initializers run. The pointers are written once and only read after that.
A library that cannot be opened, or a symbol it lacks, is a runtime error
naming the spelling tried, and the program does not start:

```
cannot load SDL2 (libSDL2.so): libSDL2.so: cannot open shared object file: No such file or directory
SDL2 (libSDL2.so) has no symbol SDL_Foo
```

A library is never closed. A package's `main_thread_only` restriction
(docs/packages.md) applies to the members of its blocks as it does to its
other externs. A variadic C function cannot be bound: the declaration has
no way to say so, and calling one through a fixed-arity pointer is
undefined. The generated C is the same on every platform, since the
runtime chooses the spelling, and a program whose only C is such libraries
runs under the in-process backend with no C toolchain installed.
Conventions for declaring a real library's functions under these shapes
are in docs/reference.md §24.

### 7.11 Exporting Goose functions to C

```goose
export fn hello_world() -> const u8[:] {
    return "Hello, world!"
}

export "c_sum" fn sum(a: i32, b: i32) -> i32 { a + b }
```

An `export fn` defines a normal Goose function and emits a C-callable wrapper.
The optional string names its C symbol; otherwise the function's leaf name is
used. Exported functions must be top-level, non-generic, have explicit
parameter types, and have at most one explicitly typed return value. Default
arguments, `return ... from`, reusable-pool parameters, and string-builder
parameters are not supported at the C boundary. Parameters otherwise use the
same fixed-size C shapes as §7.10.

Scalars and flat fixed-size returns are returned by value. A read-only
`const u8[:]` return is exposed as `const uint8_t *` and an additional
`int64_t *out_len` parameter; the returned bytes are not NUL-terminated, and
their storage remains borrowed according to the Goose function's lifetime.
Other references, slices, and variable-size returns are rejected.

Generated C retains its normal `main` unless the program is compiled with
`--header out.h`, which builds it for a C host: the generated C has no `main`
and a source without a Goose `fn main()` is accepted. The header declares
`goose_init(argc, argv)`, which a C host must call before any exported
function. It initializes the runtime and the globals and then runs the Goose
`fn main()`, if there is one, so `main` is where a library keeps its own
start-up code; exported functions may be called from it. A second call does
nothing. Initialization is idempotent on the initializing thread; exported
calls and mutable Goose globals are not thread-safe and must remain on that
thread. `--header` requires generated C output and at least one export, and
writes declarations and the required packed C type definitions for the
exported API.

An exported C symbol is emitted as written, as an `extern` one is, and must be a
C identifier that is not a C keyword and that the program does not otherwise
reserve (`main`, `goose_init`, or a name beginning with `gs_`).

The wrapper calls the normal generated Goose specialization, not its hidden
internal C signature. This keeps Goose's stack argument and other compiler
implementation details out of the function's public C signature.

### 7.12 `simd fn`: versions per instruction set

```goose
simd fn encode(out: u8[>..]&, src: u8[:]) { ... }
```

`simd` asks the implementation to build the function once for the
baseline instruction set of its target and once more for each wider one it
supports (AVX2 and AVX-512 on x86-64), and, every time the function is
called, to run the widest version that the CPU running the program
supports. It is meant for a function containing a hot loop that the C
compiler can vectorize, but only poorly or not at all with the baseline's
instruction set, such as byte transforms, codecs and kernels over arrays.

`simd` has no meaning of its own: every version computes exactly what the
baseline does, so a program's output, aborts and exit status never depend
on which version runs, or on whether there are versions at all. Integers
already compute at their exact widths (§6.2), checks abort where they
would, and float operations round as the baseline's do, one rounding per
operation: no version fuses a multiply and an add into one rounding where
the baseline would not. An implementation may ignore `simd` entirely, as
the C backend does under compilers other than clang on x86-64 and under
the JIT.

The one exception is the standard library's `mul_add(x, y, z)` (`math`),
which asks for `x * y + z` rounded once where the hardware can do so and
accepts it rounded twice where it cannot. Which one a call gets is up to
the implementation. It may differ between targets, between builds for one
target, and between a function's versions. Because the product is not
rounded on its own, the two can differ by much more than the last bit:
where `x * y` and `z` nearly cancel, including a zero against a nonzero,
and where `x * y` overflows but the sum does not. The C backend rounds once
on arm64, in x86-64 code built for AVX2 or later, and in a `simd`
function's AVX2 and AVX-512 versions. It rounds twice everywhere else,
under MSVC and under the JIT. A `*` followed by a `+` is never fused, `mul_add` or not.

Versions are per function: each version contains everything the function
contains, including the callees inlined into it. The function itself stays
out of line, since a caller it was inlined into would run the baseline's
code. Each call to it performs one test of the CPU's level, so `simd` suits
a function that runs a loop rather than a function called from inside a
loop. The cost is code size: the function's body appears once per version.

`simd` comes after `export` and before `recursive`. It applies to
functions with bodies, including nested and generic ones (each
specialization has its own versions), and `extern simd fn` is an error.

### 7.13 `inline fn`: inlined whatever its size

```goose
inline fn project(cam: Camera, p: double3) -> f64, f64, bool { ... }
```

`inline` asks for the function to be inlined at every call, however big
it is and however many calls there are. Without it, the implementation
decides from the function's size and its number of calls, and a function
called from a hot loop can stay a call because it is called from several
places.

`inline` has no meaning of its own: a program computes the same with or
without it. It is a request the implementation may not be able to grant
at every call. Where Goose cannot inline a function itself (one returning
several values, §7.3), the C backend asks the C compiler to inline it
instead. Where inlining is off (`-O0`), every call stays a call.

`inline` is a keyword only directly before `fn`, `recursive` or
`thread_fn`, so it stays usable as a name. It is an error together with
`extern` (no body to copy), `export` (its C wrapper calls it), `simd` (a
caller would run the baseline only), `recursive` and `thread_fn`.

### 7.14 `checkfree fn`: no bounds check left

```goose
checkfree fn bfs(dist: i32[>..]&, q: NodeId[>..<]&, src: NodeId) -> i64 { ... }
```

`checkfree` asks the compiler to reject the program if a bounds check is
left anywhere in the function's text after bounds-check elimination
(§10.5): in each specialization of it, each `simd` version and each copy
inlined into a caller. It changes nothing about what the program computes
or how it is compiled. A check belongs to the text it is written in, so a
caller's argument expression is the caller's, and a nested function or
block written inside a `checkfree` function is checked as part of it.

* **What counts.** Only the index and slice checks of §9.3 and conversions
  into index and bound types (§3.11) count. The other aborts of §9.3 are
  still allowed, `assert` in particular, which is how such a function
  states its preconditions once, outside its loops.
* **What it calls.** Every call in a `checkfree` function is to a
  `checkfree` function (directly, through a case function's dispatch or a
  function value naming one, or a `format` overload a `print` reaches), a
  builtin or an `extern fn`; a block it calls must be written in a
  `checkfree` function. `inline` does not exempt a callee: a call that
  stays a call runs the callee's own checks.
* **What it may rely on.** It may rely on the analysis's facts, including
  those from call sites (§10.5). So a new caller that does not establish
  what an entry relied on can make a `checkfree` function fail to compile;
  the error names the check and why it remained. Index types in the
  signature and `assert`s at entry make the function independent of its
  callers. Inside a `checkfree` function, an implementation must prove at
  least the following:
  * a constant index into a fixed array in range;
  * an index into a fixed array whose type's range fits the array;
  * an index of `a`'s index type into `a`;
  * an index of `a`'s index type into `b` where `b.len >= a.len` was
    asserted, with no growth of `a` and no shrink of `b` since;
  * a `for` loop's index into the array whose length is the loop's count
    or range end;
  * `a[i]` where a condition established `0 <= i < a.len` and neither `i`
    nor `a` has changed since;
  * a repeat of a check already passed on the same path.

  Anything else it proves is implementation-defined.
* `checkfree` is a keyword only directly before `inline`, `simd`,
  `recursive`, `fn` or `thread_fn`. It comes after `export`, and is an
  error with `extern`. A nested function inherits it. Under `--no-bce`
  nothing is verified, and the compiler warns about this.

## 8. ADTs in use

### 8.1 `match`

```goose
match shape {
    Circle c => c.r * c.r * 3.14159,   // payload by value (a copy)
    Rect r   => r.w * r.h,
    Point    => 0.0,
}
match sexp {
    Sym &s   => use(&s.name),          // payload by reference (&-binder)
    List &l  => walk(l.kids),
}
match n { 0 => "zero", 1..10 => "small", _ => "big" }
match t { GROUND => "ground", world::GATE => "gate", LO..HI => "mid", _ => "other" }
match d { 0, 6 => "weekend", 1..6 => "weekday", _ => "no day" }
match dir { North, South => "vertical", East, West => "horizontal" }
```

* Over ADTs: a match is exhaustive over the variants. A `_ =>` wildcard is
  allowed, but never in a list. An arm names a variant and may bind its
  payload (`c: Shape.Circle` above), or lists several variants
  (`North, South =>`) and binds no payload, since their payloads differ in
  type. No variant is named twice.
* Over integers (any integer type): an arm is a constant, a half-open
  range, or a comma-separated list of them (`0, 6`, `1, 3..5, LAST`) that
  matches when any one of them does. `_` is required, and never in a
  list. Every pattern value must fit the scrutinee's type. The first arm
  that matches is taken. A constant or a range bound is an integer literal
  or a named constant, either one optionally negated: `GROUND`,
  `world::GATE`, `::GATE`, `LO..HI`, `-LIMIT..0`. A named constant is a
  `let` or `const` global whose initializer is a constant expression,
  taken at that value; an array size may name the same kind of constant
  (§11.1). The name resolves as any other does (§11.1): a local, type
  parameter or nested function of that name hides the global and is not a
  constant, and a `var` global is not a constant either. A bare name is a
  variant when the scrutinee is an ADT, and a constant when it is an
  integer; as a constant it binds nothing.
* Every pattern must be able to select its arm: `_` is the last arm, and a
  value or range that earlier arms already match completely is an error
  (`1..5 => a, 3 => b`). Overlap is fine as long as each pattern still has
  something left to match: `0 => "none", 0..10 => "few"` sends 1 to 9 to
  its second arm. Within one arm, a pattern listed twice and literals that
  overlap are errors, but differently named constants may overlap, since
  they may be aliases of one value (`EAGAIN, EWOULDBLOCK`, which have the
  same number on Linux).
* Arm binders are explicit about copy vs reference, like the rest of the
  language. `Circle c =>` binds the payload *by value*: a copy, potentially
  a large one for variable-size payloads, which may not be written to, just
  as a `for` copy may not (§6.5). `Circle &c =>` binds it *by reference*,
  in the same form as `for &x` (`Circle& c` is the same tokens).
* `&`-binders are legal only on **variable-mode** payloads (whether the
  scrutinee is a value or a reference). A fixed-mode value may be
  overwritten with another variant, including inside the arm, so a
  reference into its payload is exactly what §3.5 forbids, and matching a
  fixed-mode ADT (even through a reference) offers by-value binding only.
  So does matching a resizable ADT, since a whole assignment can give it
  another variant in the same way (§4.4). The variant can never be reassigned
  through a `&`-binder. A payload holding self-relative references binds
  by reference only (§3.9), which is why an ADT with such payloads is used
  in variable mode, and a resizable ADT holds them only in its resizable
  payloads (§3.5).
* An arm list may hold `const for` expansions (§6.5) beside written arms;
  their arms are flattened first, and every rule above applies to the result.
* `T?` narrows via `if r { … }` / `guard` / `assert(r)` (flow typing, §3.8).

### 8.2 Case functions (match as an overload set)

The cases of a `match` may instead be written as separate functions, each
taking one *variant type* in the same parameter position:

```goose
fn area(c: Shape.Circle) -> f64 { c.r * c.r * 3.14159 }
fn area(r: Shape.Rect) -> f64   { r.w * r.h }
fn area(p: Shape.Point) -> f64  { 0.0 }

let a = area(s);     // s: Shape — dispatches on the tag, like a match
```

* Variant types (`Shape.Circle`) are types in their own right, usable
  directly and behind references (`Shape.Circle&`).
* Calling the overload set with the ADT type (or a reference to it)
  performs tag dispatch (a jump table). Exhaustiveness is checked as for
  `match`: every variant must have exactly one applicable overload, and the
  return types must agree. All arms construct any nonfixed result in the
  same destination (§4.3). The overloads' parameter types choose copy or
  reference as match binders do (`Shape.Circle` vs `Shape.Circle&`), with
  the same rules: a fixed-mode or resizable scrutinee, even behind a
  reference, dispatches only to by-value variant parameters (§3.5), and a
  payload holding self-relative references dispatches only to by-reference
  ones (§3.9).
* Dispatch is on one parameter position (v1 rule: multi-position dispatch is
  an error). Other parameters pass through unchanged.
* A dispatched call writes out every argument. It evaluates the arguments
  once, before the tag picks the case, so no case's defaults (§7.1) are
  passed, and a call that could dispatch only by leaving parameters to
  their defaults is an error.

This is the language's "virtual function" idiom, without vtables or
inheritance, and it keeps the closed-world exhaustiveness guarantee.

---

## 9. References, lifetimes, and safety

### 9.1 The outlives rule

**A reference/slice must not outlive the variable that owns its target, and
must never observe its target at a wrong type.** Everything below enforces
this rule statically. The surface language has no lifetime annotations and
no aliasing or exclusivity restrictions. Aliased references are allowed:
without shared-memory concurrency, and given the layout rules of §3/§5,
aliasing alone cannot break type safety. This outlives-rule is the only
"borrow checker" the language has.

### 9.2 Roots and the depth check

Every reference/slice value has one or more static **roots**. Each root is a
local or global variable, or a temporary (below), that bounds the scope the
target lives in. A value that may point at several places has one root per
place (merged values, below), and every rule is checked against each of
them. A root is **exact** when that variable's own storage contains the
target, and inexact when it only bounds the target's lifetime; the owner is
then that variable or a variable further out.
Every root that a `&lvalue` creates is exact; inexact roots come from the
reads out of containers described in §9.5. Because functions are compiled in
call-graph order and specialized per instantiation, roots are always
statically known: a parameter's roots come from each call site, and a
function is specialized per distinct root. This gives the precision of Rust
lifetimes through monomorphization, with no syntax.

The rules below are the *scope* rules, and they apply to exact and inexact
roots alike. Exactness matters only to the rules that need the target's
**identity** rather than its lifetime:

* storing a reference into a relative-reference location (§3.9);
* converting a reference to an index (`index_of`, §3.3);
* which array a shrink through a reference frees (§5.1);
* where a store through a reference lands (the store rule below);
* the compiler's proof that two references name different arrays.

Without an exact root, each of these takes its conservative answer. A
relative reference that names a pool (§3.9) is another source of exact
roots: a load out of one is rooted exactly at that pool, whatever container
it was read out of.

Rules (scopes ordered by nesting; globals are the outermost scope, §11.1):

* **Store**: `r` may be stored into a location owned by root `L` only if
  `scope(root(r)) ⊇ scope(L)`, that is, if the pointee provably outlives the
  container. A location may be reached through a reference whose root is
  inexact. The location then lies in what the reference points at (for a
  slice, in an element), and its owner is that root or anything further out
  that can hold that pointee: every read-back candidate for that type at the
  root's scope or outside it (§9.5), including the caller's storage behind
  a parameter. `r` must outlive each of these owners, and the store is on
  record for each of them. In `let n: Node& = if c { a } else { b }; n.p .=
  x;` the field is `a`'s or `b`'s, so `x` must outlive both. It need not
  outlive storage that could hold `p`'s type but no `Node`, since the field
  is never in such storage. A call checks in the same way what its callee
  stores through an argument with such a root, or into storage that an
  argument's references lead to, since the callee has only a root for that
  storage.
  A value that *holds* references (a struct with a reference field, an
  array of slices, an ADT payload with a reference field) is stored under
  the same rule for what it holds: its root is that of the references
  stored into it, and each such store is on record for the shrink rules
  (§5.1). The same applies to the elements `append` copies: an array's
  elements are stored as the whole array would be, and a slice's elements as
  each element read out of it would be (§9.5). A declaration stores its
  value into the variable it declares, with or without a type annotation:
  `let s = { let t: u8[] = "abc"; t[..] };` is an error, since `t` ends with
  its block.
* **Branch values**: whatever receives the value of an `if`, `match`,
  `block`, `loop` or bare `{ }` (a call's argument as well as a variable)
  receives it after the scopes that its branch opened have ended. So a
  reference or slice that the value is, or holds, must not be rooted at a
  variable declared in those scopes (including a match arm's binder), or at
  a temporary made there:
  `f(if c { var t: i64[>..] = [1, 2]; t[..] } else { a[..] })` is an error,
  as is the declaration above. So is `f(if c { var t: i64[>..] = [1,
  2]; t } else { a })` for a slice parameter, where the arrays join as a
  slice (§6.4).
* **Return**: a returned reference's root must be visible to the caller (a
  caller-supplied root, a global, or the function's own in-place-constructed
  return value). This applies to every return, whatever the other returns
  give. A returned value that holds references is checked by the references
  it holds; for a local, those are the references stored into it (§9.5). A
  value whose reference fields were only ever null can be returned as
  freely as a value holding no references.
* **Merged values**: a merged value is a value that may be any of several:
  the branches of an `if` or `match`, the `break`s of a `block` or `loop`, a
  reference variable's bindings, or a function's `return`s (which each call
  maps to its own arguments). It has every one of their roots, and it names
  one array exactly only where every one of them names that same root
  exactly (a `null` names none). What a merged value holding references
  holds is rooted the same way. A merged value must outlive a destination
  by every root, and it is writable only where all of them are (§9.5). It
  is stored only where each of them could be: not at all where one of them
  may point into a grow-shrink array (§5.2), and not inside a recursive
  cycle where one of them is rooted where the cycle stores nothing (§7.8).
  A shrink frees what any of them points into (§5.1). A parameter given a
  merged value stands for all of its places, and so does a call's result
  where the argument behind a return may be a merged value. So a function
  may return a view of its input on one path and of a global arena or a
  string literal on another. For a `longer` that returns whichever of its
  two slice parameters is longer, `longer(x, y)` may point into either `x`
  or `y` (whichever it returns at run time), and into nothing else.
* Struct types with reference fields are implicitly generic over those
  fields' roots. Struct instances with different root bindings are distinct
  types for checking purposes, with the same layout. An instance whose
  references all point into one variable is bound to that root exactly. An
  instance whose references point into several variables has each of their
  roots, so a function given both kinds of instance is checked for each
  kind. A `recursive fn`'s back edges reuse its body whatever they pass
  (§7.8), so its by-value parameters are always taken to be of the second
  kind.
* References into a value being copied by value do not transfer to the copy;
  they keep referring to the source (plain value semantics).
* A reference *variable* commits to the root of its first binding. `.=` may
  rebind it only within the same root, or to a root at the same scope
  depth. The common case is retargeting it in a loop to another element of
  the same container or of a sibling container. Any other rebind needs a new
  variable. What a callee reads out of storage it was given is rooted at
  that argument (§9.5). So a key that a dictionary's `each` passes to a
  function value is rooted at the dictionary passed, at that dictionary's
  depth, whatever text the keys view. A slice variable commits the same
  way. A store through a reference to its slot (§3.8) assigns it as `s = v`
  does, even where the variable is a `let` (§4.4). This holds for a store
  through a reference that may name the slot (one read out of storage, a
  merged one, or a parameter given one of those) as well as for a store
  through a reference that does name it. A rebind to a different root
  leaves the variable inexact, since it no longer names one array. It also
  makes the variable a merged value (above), since it may still hold its
  earlier binding.
  A loop body is checked in the state that its earlier iterations leave it
  in, including a rebind in a nested function or a function value that the
  body calls. So a read earlier in the body than such a rebind sees both
  roots, and it cannot be used where the root is needed exactly (such as a
  relative link's identity, §3.9). A variable declared before a loop and
  bound only inside it (`var last: Node? = null;` before `loop { if last {
  last.next .= child; } … last .= child; }`) has, at a use earlier in the
  body than the rebind, the root that the rebind gives it, exact where the
  target lies in that root's own storage. A variable that the loop never
  binds is null throughout the loop, and a use of it is that null (§9.5).
  Any function may rebind a global `var`, a function compiled after a
  function that uses it as well as one compiled before. So a use of a
  global `var` in a function's body has every root it can be given (the
  read-back rule's answer for a global, §9.5), rather than only the roots
  bound so far.
* Inside recursive cycles the stricter §7.8 cycle store rule applies.
* The following expressions create a **temporary** when viewed in place instead
  of built into a destination: array, struct, and variant literals; call
  results; `copy(x)`; `default<T>()`; and values of `if`, `match`, `block`,
  `loop`, or bare `{ }` constructs whose branches do not join as a slice
  (§6.4). A view can come from a slice destination, a `for`, `[..]`,
  `bytes_of`, or a path into the value (§4.2). The value of such a construct is a copy of what the branch taken
  produced, even where that branch names a variable. A temporary lasts for
  the rest of its statement, or for the rest of the block whose final
  expression made it. Its scope is that
  statement's: the variables the statement declares outlive it, and the
  variables of the scopes the statement opens (the body of a `for` over it,
  the arms of a `match` on it) do not. So a reference or slice into a
  temporary may be passed down, and may be stored or bound only in those
  inner scopes: `let s = f()[..];` is an error, while
  `let t = f(); let s = t[..];` is not, and neither is a view into `x`
  bound inside `for x in f() { … }`. Such a reference or slice is never
  returned. A function it is passed to may keep it in its own locals, which
  end first. What a temporary *holds* is not rooted at the temporary
  (§9.5). So, for a slice parameter,
  `take(if c { str("a", n) } else { str("b") })` is an error, because each
  branch's string ends with its branch, while `take((if c { str("a",
  n) } else { str("b") })[..])` views the `if`'s own value, an array that
  lasts for the rest of the statement. A temporary is not storage that `.=`
  or a reference parameter can bind (§4.1); they bind a construct's
  branches instead. A view of a temporary is read-only (§9.5).

Violations are compile errors. There is no escape hatch in v1.

### 9.3 What the runtime still checks

The runtime aborts (prints a message and exits; an abort cannot be caught)
on each of the following. In a contained worker (§11.2), each of them except
the last two ends only that worker:

* array/slice indexing out of bounds (elided wherever the compiler proves it
  cannot fire, §10.5; can be disabled wholesale in a designated unsafe-fast
  build), and a conversion into an index or bound type outside its array
  (§3.11), elided the same way;
* a reusable pool's `free(i)` with an index outside `[0, pool.len)`;
* a slice pool's `alloc_slice`/`realloc_slice` with a negative length, or
  one no data stack could hold (§5.4);
* a slice pool's `free_slice`/`realloc_slice` with a non-empty slice that is
  not one of its runs, where the checker could not tell (§5.4);
* limited-array capacity overflow;
* shrinking below empty (`pop` on an empty array, `pop_n` of a negative
  count or of more elements than the array holds, `resize` to a negative
  length);
* relative-reference offset overflow at store (only where a root array, or
  a named pool, can span more than the width holds, §3.9);
* debug only: integer overflow (per operation as it executes, §6.2), `as`
  conversions that change the value (§6.3; conversions to a float
  excepted);
* division by zero (always);
* `assert` failures, and the program's own `abort(msg)` (`msg` any `u8`
  array or slice; printed as `goose runtime error: <msg>`);
* guard-page hits (stack budget exceeded), which abort safely and never
  corrupt memory;
* native call stack exhaustion (recursion deeper than a thread's stack
  holds, §7.8), which aborts safely in the same way, with a diagnostic of
  its own.

`exit(code)` ends the program normally with the given process exit code.
Neither `abort` nor `exit` returns, and the checker takes this into
account: code after them is unreachable, so a branch ending in one of them
gives no value (§6.4).

### 9.4 Type-safe reuse and stale references

With `reusable` arrays and limited arrays, a stale reference can read a
*different value of the correct type* (type-safe reuse). This can cause a logic
error, but not memory corruption, type confusion, or out-of-bounds access. Slot
reuse allows these errors while avoiding a general-purpose allocator.

External bytes must be validated before they become a Goose value.
`from_bytes` (§12) checks an image's framing, tags, lengths, and
self-relative links. Each link must point to an element start in the same
image. Invalid input returns `false` and an empty array, so it cannot
introduce an unchecked reference. This verifies *safety*, not data
integrity: edited bytes may still describe a valid structure. Detecting
changes to the original data requires a separate check, such as a checksum.

### 9.5 Writability

Everything is writable unless its type says otherwise. Any type may be
qualified `const` (§2). A `const i64[3]` or `const Point` is a value whose
contents cannot be written into (its elements, its fields, and whatever a
reference to it reaches). `const T&` and `const T[:]` are a reference and a
slice through which the pointee, or the elements, cannot be written: no
assignment, compound assignment, `++`, or growing/shrinking operation
through them compiles. Whether a `const` value can be assigned as a whole
is decided by its binding (`let`, §4.4), and a copy of a `const` value is a
fresh, writable value: a cursor `var s: const u8[:] = text;` advances with
`s = s[1..]` and never writes a byte of `text`. `const` is shallow: a
reference or slice read out of a field of a `const Box&` is as writable as
the field's own type says, and so is a path through it (`b.r.n = 7`,
`b.s[0] = 7`). The exception is a self-relative reference (§3.9). It points
within the value or array holding it, so it is no more writable than the
path that reaches it. Every value of reference or slice type is either
read-only or writable, and where that property comes from is *inferred*
per instantiation, in the same way as roots, with no annotation needed:

* Read-only values include string literals (`const u8[:]`, §3.7). A view made
  with `&x`, `x[..]`, or reference binding (§4.1) is also read-only when `x` is
  a `const` value, a field of one, or a by-value `for`/`match` binding (§6.5).
  These views have types `const T&` or `const T[:]`. Views into temporaries are
  read-only because a write would change only the temporary (§9.2). So are
  `bytes_of` views (§12), values read from slots declared `const`, and
  self-relative references read along a read-only path. A slice loaded through a
  reference-to-slice is the slice in the referenced slot. It is writable only if
  both the reference and the slice are writable. A reference to a variable
  holding a read-only slice is therefore read-only too, whether made with `&s`
  or by binding `s` by reference (§4.1). A `let` binding of a reference or slice
  prevents rebinding; writes through it follow the value's own constness. For
  example, `let r .= xs[i]; r = 0;` writes an element of a `var` array (§3.8).
* Writable: everything else, that is, `&x` and `x[..]` of a `var` or of a
  plain `let`, and whatever else is read out of a slot that is not `const`.
* **Parameters and results are generic over constness.** A parameter
  declared `u8[:]` or `T&` takes a read-only or a writable argument, and the
  specialization is checked with the argument's constness (a write through
  a parameter given a literal is an error at that instantiation). A
  parameter declared `const u8[:]` is read-only whatever it is given;
  declaring it so documents that the function only reads its input. A
  declared result type likewise names the shape. The constness of a call's
  result is that of what the function returns, and the result is read-only
  where any return is (a `u8[:]` result of `return s` for a `const u8[:]`
  parameter is read-only at that call). `-> const u8[:]` makes the result
  always read-only. Adding `const` is always implicit (`u8[:]` fits
  `const u8[:]`); dropping it is never implicit.
* **Slots say what they hold.** A field, an element, a global, an annotated
  variable and an assignment target are *slots*, and a read-only reference
  or slice is stored in a slot only if the slot's type is `const`: `struct
  Named { name: const u8[:] }` holds a literal, `struct Buf { bytes: u8[:] }`
  does not, and `views: const u8[:][>..]` takes either kind of view. So
  what is read out of a slot is exactly as writable as the slot's type says
  (a self-relative reference is at most that writable, above), and storing
  a value never makes it writable: a `u8[:]` field that could be written
  through can only ever have been given a writable slice. An un-annotated
  variable's type is its initializer's, made `const` where that value is
  read-only (at this instantiation, for example). So `var t = trim(line);`
  for a read-only `line` is a `const u8[:]` cursor, which
  `t = trim(t[..n]);` can advance. It stays read-only whatever is assigned
  to it later, as if it had been declared with that type.
* An `extern fn` (§7.10) is a C function, and its parameters have exactly
  the constness they declare: a read-only argument needs the parameter
  declared `const`.

**Read-back roots.** A container names a scope, not the storage its contents
point into, so the root of a reference or slice of pointee type `T` read out
of a container `C` is re-derived. A **candidate** is a variable whose own
storage can hold a `T` by value: an array of `T` in any array kind, a struct
or ADT payload with a `T` field, a `T` itself, and so on through by-value
nesting. A variable that only holds *references* to `T` is not a candidate,
and a reference or slice field ends the search. Static data is a candidate
for the element types a literal can supply. For a writable reference or
slice, static data is a candidate only when nothing else is, since a
writable slot is never given a literal (above) and so holds static data
only as a null or an empty slice. For a read out of a field, an element or a
global, a variable holding a grow-shrink array whose elements can hold a
`T` is not a candidate either: a reference rooted at that variable is never
stored (§5.2), whatever else of the variable could hold the `T`. A
reference to the whole array, or to what holds the array, is stored like
any other: its pointee is not an element, and the variable stays a
candidate for it. Then, depending on where `C`'s own root lies:

1. **A global.** Only globals outlive globals (§11.1), so the owner is a
   global candidate, whatever local scope is open. If there is one
   candidate, the root is that variable, exact. Otherwise the root is the
   global scope, inexact.
2. **A local of the function being checked**, at any block depth,
   including by-value parameters. Everything stored into `C` was reachable
   from this frame and had to outlive `C`. So the owner is a local declared
   at or outside `C`'s block, a reference parameter's pointee, a global,
   static data, or storage of the caller's that a parameter leads to through
   references. Those references are the ones a by-value parameter holds
   (which its argument filled), and the ones in what a reference parameter
   points at. This function cannot enumerate that caller storage, but all
   of it outlives the parameter's root (for a by-value parameter, the root
   of what its argument held: §9.2's generic roots of reference fields), so
   that root is its candidate. Every candidate is a root of the value: a
   local or a global as an exact root, and such a parameter's root as a
   bound on the storage behind the parameter. The value names one array
   only when there is exactly one candidate in all and that candidate is
   not such a parameter's root. The candidates are the variables in scope
   at the read: an array that comes into scope after the read cannot be the
   owner and is not a root of the value.

   Where `C` is a local that holds
   values rather than a reference or slice, everything stored into `C`
   since it was made is on record: stores through a reference to it, by a
   function it was passed to, or later in a loop around the read. Where
   each of those stores was rooted at one variable exactly, at static data,
   or at the storage a parameter's argument holds, the value's roots are the
   roots of those stores instead. A variable whose storage cannot hold the
   value's pointee type is not one of these roots, since it owns none of
   what was stored there of that type: an `i64&` read out of a holder that
   also views a `u8` buffer is not rooted at the buffer. A word of
   `words(buf)` for a local `buf` points exactly into `buf`, whatever else
   in scope could hold a `u8`. A by-value parameter holds what its argument
   held, so a slice of `g.edges` for a `g: Graph` parameter is bounded by
   that argument's root. It is not bounded by the callee's other
   parameters, which can hold its element type but none of the caller's
   views.

   A copy of a field or an element of such a local (a `let` of one,
   a `for` or `match` binder's copy, a popped element, or a value loaded
   whole through a reference to it) holds no more than was stored into the
   local, and that record then applies to the copy too: out of
   `let hs = [H { r: a }]`, `hs[0].r` points exactly into `a`. A copy out
   of any other container has only its container's scope on record, and
   its roots are the candidates.

   A nested function's or a function value's
   body reads a local of the function it is written in as one of its own
   locals here, with that local's stores on record in the same way: the
   body is checked for the state each call finds the local in (§7.5,
   §7.6). A `for` loop also reads its binder in this way in each of its
   checks, so that what the loop body stores into the holder it walks
   reaches the elements that later iterations read.
3. **A reference parameter's pointee, or itself inexact.** The owner may be
   caller storage that this function cannot enumerate, so the root is
   `C`'s root, inexact.
   A non-recursive function can retain the caller's more precise record when
   its argument names a local holder exactly, all the holder's recorded
   contents point into one root exactly, and only parameters can reach that
   holder during the call: neither the function nor a function value it runs
   can name it directly. Reads then have that contents root, including any
   roots stored through possibly aliased parameters while the body runs.
   A nested function or callback's stores keep the distinction that the
   resulting references came from that holder's contents; call sites check
   them against the actual stores into its storage, including aliased
   stores.
   Stores feed back around loops, and calls are specialized for the contents
   they receive; a call after a rebind is checked for the new contents. The
   same rule applies when a parameter forwards such a holder to another
   function. It does not separate fields whose contents have different
   roots: those retain the conservative bound. For example, a `Cache&`
   naming a local `Cache { bytes: buffer }` keeps `buffer` as the root of
   `cache.bytes`, so an unrelated reusable byte pool cannot be that buffer.
   Shrinking it still checks every live reference and stored view into
   `buffer`, including those in the caller.
4. **A temporary** (§9.2). Everything in it came from the literal's
   initializers, from the call whose result it is, or from the value it is
   a copy of, so its root is theirs: the innermost root among the
   initializers, the root of the result's contents, or the root of the
   copied value's contents. The root is exact when it is one variable
   exactly.

Any of the program's globals may be a candidate. A function that a global
initializer calls is compiled before the globals declared after that
initializer, but that compilation also serves the calls made once those
globals exist. So inside that function the global scope stands for those
globals as well, as an inexact root.

A global `var` of reference or slice type is a slot that any function may
bind (§9.2). The compilation of a function that uses it serves every call,
including calls made after a function compiled later has bound it. So a
use of such a global in a function's body is a read out of a global (case
1): its roots are each global whose storage can hold its pointee, exact
where there is only one. They never include a grow-shrink array's
elements, since no binding of a global points there (§5.2). Such a global
that names a pool (§3.9) points into that pool, and a global `let` keeps
its binding. A global initializer runs once, when the bindings made so far
are all there are, so a use in it has the roots of those bindings.

An optional variable bound only to `null` so far (`let none: Node? =
null;`, or `var best: Node? = null;` before anything binds it) holds null.
Where every binding that can come before a use of it has been checked, the
use is a null, which has no root (§3.9). For a local, that is the case
wherever it is used. In the body of the function declaring it, this
includes a loop's later bindings (§9.2). It also holds in a nested
function's or a function value's body. Such a body is checked for how a
call finds the variable (§7.5), so a call after a binding has a check of
its own, and the body's own bindings reach a loop or a recursion around
the call as the declaring body's bindings do. For a global, it is the case
in the global initializers, and anywhere for a global `let`, which only its
initializer binds. A global `var` used in a function's body is read as
described above.

A relative reference `T&<w>` read out of `C` always points within `C`'s own
root array (§3.9), so it takes `C`'s root and `C`'s exactness, whichever
case applies. A relative reference that names a pool needs no candidates:
it points into that pool, so it is rooted there exactly. This keeps a
container of links usable where a container of plain references would be
ambiguous.

When a diagnostic depends on an inexact read-back, it names the source container
and the possible roots. For example: "`n` was read out of `slots` and may point
into `pool` or `spare`", or "`g` is a global var, which any function may bind,
and may point into `pool` or `spare`". If only the caller knows the candidates,
the diagnostic instead names the parameter whose pointee bounds the value.

Writability checks depend on the callee's operations. A function that mutates
its slice argument can compile for a writable argument and fail for a
read-only one. The compiler reports the full compile-time call chain,
including the source of the read-only data. `const` is required on slots
that must accept read-only references or slices; parameters and results
may also use it to state that they are always read-only.

---

## 10. Compilation model

### 10.1 Whole program, call-graph order

Goose is a closed-world, whole-program compiler. Functions are typechecked
and specialized in call-graph order (callers before callees), so argument
types, reference roots, writability, destination stacks, and static function
values are always concrete. Recursion is the annotated exception (§7.8).

### 10.2 Monomorphization

Each function is compiled once per unique combination of (argument types,
reference roots, writability provenances, destination/target stacks and
request kinds, static fn values), with a nested function's free variables
counting as arguments (§7.5). Roots and stacks are compile-time constants
inside each specialization, so pushes through references compile to direct
bumps of a known global. The exception is where the compiler chooses a
hidden stack argument to share one body across contexts (§4.3, §7.4).

### 10.3 Stack assignment (deterministic recipe)

Traverse specializations DFS from `main` (and global initializers). Each
specialization carries a compile-time *stack environment*: the watermark of
stack indices in use on entry, and a map from each of its nonfixed locals /
by-value params / in-flight returns to an index. Assign each new
simultaneously-live resizable (and each data-stack-resident variable value,
which shares by nesting per §1.3(4)) the lowest index not in use at that
point. Indices become free again when the owning scope exits (sequential
reuse), so a function called after a local's block has ended may use that
local's index again. This is what lets a recursive cycle's functions own
scratch (§7.8).
A pending nonfixed return's destination stack is part of the in-use set
from the call site until the value is received. This is also what makes
`return from` safe (§7.9). N = the maximum index + 1 reached anywhere.
Threads run the same algorithm per thread program (§11.2).

Implementations may replace per-context specialization with hidden runtime
stack arguments where profitable; semantics are identical.

Fixed locals normally go to the native stack / registers. The C backend
places fixed locals and temporaries larger than 4096 bytes on data stacks,
using the same scope watermarks and hidden stack indices as nonfixed values.
This threshold is a backend policy, not a language limit or a change to a
type's size class, packed layout, value semantics or reference lifetime.
It prevents large arrays and records, including temporaries introduced by
inlining, from exhausting a platform's much smaller native call stack.
Inside a function of a recursive cycle (§7.8) such values stay on the native
stack instead. A data stack held across a call into the cycle would need one
stack per activation, which §7.8 rules out for every value. The recursion's
depth is already bounded by the native stack, and the native stack is where
fixed values normally go. So a large fixed parameter copy, local or
temporary of a cycle function is a native value; deep recursion over large
values avoids this by taking them by reference. Rejecting such parameters
and locals in cycle functions instead would have made the backend's
threshold a language rule.
Variable-class locals may be placed on the native stack (`alloca`) instead
of a data stack when the compiler chooses; the current backend places them
on data stacks. Resizable frame objects (C.2) still keep their fixed prefix
and tail header in the native frame.

### 10.4 Runtime environment

At startup, reserve N address regions (target: multiple GB each; commit-on-
touch via guard pages, prototyped at github.com/aardappel/stackalloc),
plus guard gaps between regions so runaway growth aborts cleanly. An
implementation sets one budget for the address space that regions use
across all thread programs together (32 TB by default here), and this
budget bounds `hardware_threads()` (§11.2). A region that the platform
refuses may be reserved smaller; nothing that the implementation elides
depends on a region's reserved size. Platforms without address-space
reservation (wasm today) fall back to index-based references and
bounds-checked growth, with reduced performance.

**The 48-bit size limit.** A data stack reserves **at most 2^48 bytes**.
Consequently, in every conforming implementation:

* no value's byte size exceeds 2^48;
* no array's element count exceeds 2^48 (stated separately because an
  element type may be zero-size, and because most array types are already
  far tighter: `T[]`'s length field defaults to `u32`, §3.3);
* therefore every length, capacity, index, and byte offset lies in
  `[0, 2^48]`, and `.len` and `.cap` are non-negative by construction.

This limit allows up to 256 TB in one value while leaving headroom for
arithmetic the compiler can reason about. The guard region after each
reservation already turns an attempt to exceed it into a safe abort (§9.3),
so no growth operation needs a separate check for this limit. A fixed
array's size is a constant checked at compile time. Implementations may
impose a *smaller* limit (wasm32 is inherently capped at 2^32); they may not
raise it, so a program's meaning never depends on the target having more.

Making the limit part of the specification provides three guarantees:

* **Size arithmetic cannot overflow.** With 15 bits of headroom below `i64`,
  `len - 1`, `i + 1` for `i < len`, `len + len`, `len * 2`, and
  `i * element size` are all in range. The optimizer may assume this rather
  than prove it, and the bounds-check analysis (§10.5) relies on it directly.
* **Signed is the right default for sizes.** `.len` returns `i64` (§3.1),
  and since the top bit is provably unused, the sign bit loses no usable
  range, while subtraction and differences behave naturally. Signed sizes
  avoid unsigned wraparound in ordinary differences.
* **Non-negativity is a type-level fact, not an inferred one**, so §6.1 can
  admit the one mixed-signedness comparison that matters without any
  analysis.

Compilation target: C/C++ first, LLVM later. Representation and calling
convention: Appendix C.

### 10.5 Bounds-check elimination

The index and slice checks of §9.3 are the only abort that the compiler
routinely proves unnecessary, so the analysis that removes them is part of
the compilation model rather than an implementation detail. It runs over
each specialization after optimization, and it is required only to be
*sound*: every check it removes is one that could not have aborted, and a
check it fails to prove unnecessary stays in the program. Nothing about a
program's meaning depends on how much it proves, except whether a
`checkfree` function compiles (§7.14), for which the specification fixes
what every implementation must prove.

Beyond ordinary flow facts (loop headers, conditions and their negations,
`assert`, match arms), the language gives the analysis the following:

* **Index types.** A value of `uN in a` is below `a.len` at any later point,
  since `a` never shrinks (§3.11). So an index of `a` used on `a` needs no
  check, and one used on another array `b` needs only `a.len <= b.len`.
  That is a relation between two lengths, which the analysis states and
  keeps like any other relation. It is not a fact about every element of an
  array, which the analysis cannot state.
* **Types.** An array is of one type, and a reference reaches only an array
  of its own type, so a growth or shrink through a reference, or a callee's
  write through an argument that is an array of scalars, leaves every
  array of another type as it was.

* **Monotonicity in the type.** A grow-only `[>..]` shrinks only at a `pop`,
  `pop_n`, `resize` or `clear` on the array itself (§5.1), which the analysis
  sees, so a bound established before a `push` still holds after it. This is a
  guarantee a resizable-array type without the grow-only/grow-shrink split
  cannot offer.
* **Roots.** Every reference's root is static per specialization (§9.2), so
  a call cannot invalidate a length the callee has no path to, since
  storage is reachable only through a global, a capture, or an explicit `&`
  (§3.8).
* **Static extents.** `T[k]`'s length and `[..k]`'s capacity are constants.
* **Total operations.** `%` is Euclidean (§6.2), so a reduction is always in
  range; a completed `pop` proves that the array was non-empty, and a
  completed `pop_n(n)` proves that `0 <= n <= len`, because the other cases
  abort.

Known gaps are listed in TODO 0f. Whole-program compilation lets the
analysis reach across calls: each specialization has concrete argument
roots, a caller's facts reach an inlined callee body directly, and a call
that is not inlined carries them too. Every call site records what it
proves about the arguments it passes (constant bounds on an array's length,
and how the passed lengths and integers relate). A specialization enters
with the meet of those facts once all of its call sites have been analyzed.
Callers are analyzed first, and a call site inside a recursive cycle gives
its callee no facts. A thread's entry point and an `export fn`, which C may
call with any arguments, enter with no facts. In the other direction, a
callee is summarized rather than assumed hostile. The storage it may resize
or overwrite (through its reference parameters, in globals, or in captured
outer locals) is computed as a fixpoint over the call graph, and a call
kills only the facts that those effects can name. So a kernel
`fn blur(src: u8[>..]&, dst: u8[>..]&)` that only reads and writes elements
can use `src.len == W * W` when every caller established it, and a call to
it leaves the caller's own facts about `src` intact.

---

## 11. Program structure

### 11.1 Modules and globals

* A program is compiled as a whole. `import a.b.c;` includes the file
  `a/b/c.goose` once. The file is first resolved relative to the root of
  the *importing file's package*: for the program's own files that is the
  directory of the root file being compiled, and for a library's modules
  the library's directory. Next, where `a` names a package the root file
  declared (below), it is resolved as that package's module `b/c.goose`
  (`import a;` is its entry `a.goose`). Last, it is resolved in the
  standard library's directory (`--stdlib <dir>`, else `GOOSE_STDLIB`, else
  the `stdlib/` of the source tree the compiler was built in). A file of
  the importing package with the name of a declared package is an error.
  The form `import .a.b;` (leading dot) resolves relative to the
  *importing file* instead.
* **Packages** (`docs/design/packages.md`). `import name from "source";`
  in the *root file* declares the package `name`, a directory holding its
  entry `name.goose`, and imports that entry; the source is a path relative
  to the root file (`"./lib"`, `"../lib"`) or a git repository at a ref
  (`"owner/repo@v1"` on GitHub, `"host/owner/repo@commit"`, or a URL),
  fetched by the compiler. Only the root file declares packages, so a
  program's dependencies are flat and in one place. The same form in any
  other file is a *requirement*: the root must declare a package of that
  name, from the same source, or compilation fails naming the line to add;
  a different ref is a warning. One version of a package exists in a
  program. All declarations are public in v1; a name collision within one
  namespace is an error, except that a global variable and functions may
  share a name (below). Top-level declarations are order-independent.
* **Namespaces** (docs/design/namespaces.md). `namespace image;`, written at
  most once and before a file's declarations, places those declarations in
  namespace `image`. A file without it declares into the global namespace,
  and several files may declare the same namespace. An import loads a file;
  it neither creates a namespace nor brings a namespace's names into scope.
  From elsewhere a declaration is named `image::Pixel`,
  `image::brightness(p)`, `image::Shape.Circle { r: 1.0 }`, `image::pool`;
  `::name` selects a global declaration (or builtin) that a namespaced one
  shadows. A declaration may write its namespace itself
  (`fn image::brightness(…)`, `fn ::main()` inside a namespaced file). The
  qualifier alone then decides the namespace of the declaration and of the
  names inside it. An unqualified name resolves lexically first, to
  what the scopes around its use declare. It resolves to the innermost
  variable or type parameter of that name (a function's parameters and
  locals, then its type parameters, then what is around its declaration,
  §7.5), or else to the nested functions in scope (the innermost block's
  set, §7.5). Only a name that no scope declares resolves in the current
  declaration's namespace, and then in the global namespace and the
  builtins. So a type parameter or a nested function hides a global and a
  function of its name, as a local does. A namespace keeps its global
  variables apart from its functions, and no variable can be called
  (function values are not data, §7.6). So the name in a call skips the
  global variables and resolves to the functions, in each namespace of
  this order. A global `var last: i32` in any module leaves std's
  `last(xs)` callable from every module, including its own, while any
  other use of `last` (`last = 7`, an argument) is the variable. A
  variable in scope still hides the functions of its name: a local `count`
  makes `count(xs)` an error. A type parameter bound to a type is not a
  value (in `fn f<N>(x: N)`, `N` as an expression is an error, and `::N`
  names a global `N`). No type parameter or nested function is a constant
  that a size or a match pattern may name (§3.3, §8.1). A function name's
  overload set is the set of the first namespace in this order that
  declares the name at all; sets never merge across namespaces (a
  namespaced `hash` overload reaches the global integer overloads as
  `::hash(x)`). A generic body resolves names where it is defined. A UFCS
  call `x.f(...)` resolves `f` by the same rule, except that when `x`'s
  struct or enum type is declared in a named namespace that declares
  `f`, that namespace's set is the one used, ahead of the calling code's
  namespace and the global one (a nested function in scope still hides
  it). So a type's functions are its methods wherever it is used, in a
  generic body too, while a plain call `f(x)` looks only where the calling
  code does. The `format` hook is likewise looked up by type (§3.7).
  Namespaces affect only name resolution, type identity and generated C
  names (§7.10); they have no runtime representation, and they provide no
  privacy and no re-exports.
* Globals are declared like locals (`let`/`var`, any type including
  resizable). Semantically, the program runs inside an implicit outermost
  scope that owns the globals. They take part in the depth check (§9.2) as
  the outermost roots. They are initialized before `main`, in declaration
  order within a file and imported files first across files, so a global
  initializer may name a global from a module it imports; their
  initializers may call functions. A global resizable owns the bottom of a
  stack for the program's lifetime (a natural whole-program arena). A
  global has no value until its initializer has run, and nothing else can
  give it one. Naming it earlier is an error (§4.4), whether to read or to
  write it, and whether in an earlier global's initializer, in its own
  initializer, or in a function that one of those calls. A compile-time
  size or match pattern may still name a later named constant (below).
  That outermost scope is a frame like any other: a global's storage
  belongs to the program instance running it and is never shared with
  another (§11.2).
  Every global has an initializer, except that `var g: T;` leaves its value
  to its type: `g` starts as `T`'s default value (§4.2), set where an
  initializer would run. So `var log: i64[>..];` is `var log: i64[>..] =
  [];`, and a `T` without a default value (a reference) is an error. A
  `let` or `const` global must be given its value, since it can never take
  another.
  `const` globals (`let` globals of a `const` type; a `var` of such a type
  is still assigned as a whole, §4.4) of flat fixed type with
  compile-time-evaluable initializers are stored in static data. The
  initializer of any `let` or `const` global is a named constant that a
  compile-time size may use (`i64[N]`) wherever no local of its name hides
  it (§3.3). An untyped integer or float named constant also adapts
  wherever it is used, as a literal does (§3.1, §6.3).
  A size, a fill count (`[v; N]`), a match pattern (§8.1), or a use that
  adapts an untyped named constant to another type, takes a named constant
  at its initializer's value. A size also takes a `.len` that an
  initializer gives and the type does not fix (§3.3) at that value's
  length. So a `let` that such a use names, directly or through another
  named constant's initializer, may not also be bound anywhere to a
  writable reference, through which its value could change (§4.4). This
  covers binding by `&`, by `.=`, or by a reference parameter, field or
  binding that is not `const`. As for a comparison that relies on a `let`
  being non-negative (§6.1), this is an error in either order; declare it
  `const`, or bind the reference to a copy.
* Entry point: `fn main() { }`, in the global namespace. Only the *root*
  file's `main` is the entry; a `fn main` in an imported file is ignored:
  it is not an entry, it cannot be called, and it causes no name
  collision. A runnable file can therefore also serve as an importable
  library: give it `fn main_x() { ... }` plus a `fn main() { main_x(); }`
  wrapper, and importers call `main_x` directly. A namespaced file does the
  same with `fn ::main() { ns::main_x(); }`.

### 11.2 Concurrency

Threads never share mutable memory. The model is a statically typed
relative of the Linda tuple-space / coordination style.

* A worker entry point is declared `thread_fn worker(a: i64, ...) { }`.
  The compiler compiles a `thread_fn` and everything it calls **as a
  separate program** with its own stack assignment (its own N′). Because
  many instances of a thread program can run at once, a thread program's
  stack references are inherently dynamic: each thread carries a pointer to
  its own block of N′ stacks, and stack accesses in thread-program code are
  indexed from it (one hidden base kept in a register). A function is
  shared between thread programs and the main program only when all its
  stack references arrive through (hidden) arguments; any static stack use
  specializes it again per program, like a template instantiation.
* The number of workers is decided **at runtime** (there is no static
  maximum):
  `thread_spawn(worker, args…) -> i64` reserves a fresh stack block, copies
  the args, starts the worker, and returns its id. `worker` names a
  `thread_fn`, directly or as a function parameter bound to one (§7.6), so
  `fn spawn<W>(n: i32) -> i64 { thread_spawn(W, n) }` spawns whichever
  worker its caller passes. IDs increase monotonically
  and are never reused. `hardware_threads() -> i64` exists for sizing, and
  reports no more workers than the implementation's address space for data
  stacks (§10.4) holds beside the main program, so a worker pool sized by it
  never exhausts that address space.
  `thread_wait(id) -> bool` blocks until that worker has ended and all of
  its Goose storage has been released. This supports both scoped fork/join
  parallelism and orderly shutdown (send quit messages, then wait). Repeated
  waits on a completed id return immediately; multiple workers may wait for
  the same id. Every wait returns `false` for a contained worker that
  aborted, and `true` otherwise. `thread_wait(id, msg) -> bool` waits the
  same way and appends the abort's message to `msg`, a growable `u8` array.
  The message is what the abort would print after `goose runtime error: `,
  including its location. The first such wait for the worker takes the
  message; later waits, and a worker that returned, append nothing. A
  program that wants the message asks for it once and keeps it. A negative
  or never-issued id, or a worker waiting for itself, aborts with a runtime
  diagnostic.
* `thread_spawn_contained(worker, args…) -> i64` starts a **contained**
  worker, and otherwise behaves like `thread_spawn`. An abort in it (§9.3)
  ends only that worker, which releases what a return would, and leaves its
  message for `thread_wait` instead of printing it. The program and other
  workers keep running. Resources it opened through `extern fn` stay open.
  Guard-page hits, native stack exhaustion, `exit(code)`, and the runtime's
  own failures still end the program, as does any abort on main's thread.
  A worker it starts with `thread_spawn` is not contained.
* A worker's return releases its copied arguments, stack block, every reserved
  and committed data-stack region, and active-worker record, whether or not
  anyone waits for it. The runtime retains no per-completed-worker record or
  native handle, except the id of each contained worker that aborted, so
  that waits can answer, and its message until a wait takes it. Native
  threads are detached. Their final native stack/TLS teardown happens when
  the runtime entry wrapper returns, immediately after it publishes that
  the Goose cleanup has completed. A child worker owns its storage
  independently and may outlive the worker that spawned it. Queued messages
  belong to their queues and remain available to receivers after the sender
  exits. Workers still running when `main` returns are killed.
* A thread program is an instance of the program with globals of its own.
  At `thread_spawn`, every global the worker's program uses is copied from
  the spawning instance, in the same way as the arguments, and the worker
  reads and writes its copies from then on. No global is shared: a worker
  that increments a global increments its own copy, and main's copy keeps
  its value. A global whose type is not **flat** (§1.1) cannot be copied
  and is an error in any function a `thread_fn` reaches, unless it is a
  `let` or `const` global whose references all lead to static data, as a
  string literal's do (`let NAMES = ["a", "b"];`). The copy of such a
  global views the same read-only bytes, and no program can tell this
  apart from a copy of its own. `const` globals with compile-time
  initializers are static data (§1.2), read-only and therefore shared as
  they are.
* Args and queue elements must be **flat** types (§1.1); values are copied
  in and out, which is cheap because Goose values are contiguous. A
  `thread_fn`'s parameters have no defaults (§7.1): `thread_spawn` passes
  every argument.
* **Typed queues**: conceptually there is one queue per flat element type.
  `qput(v) -> bool`, `qget<T>() -> T` (blocking), `qpoll<T>() -> T, bool`
  (non-blocking; the bool says whether a value was received; on a miss the
  value is T's zero value: zeroed scalars, empty arrays, variant 0),
  `qget<T>(ms: i64) -> T, bool` (like `qpoll`, after waiting at most `ms`
  milliseconds; zero or less does not wait) and `qlimit<T>(n: i64)` select
  the queue by type. Queues are stored outside stack memory
  (runtime-internal allocation is permitted here).
* **Bounded queues**: `qput` never blocks. It returns `true` once the value
  is queued, and returns `false` immediately, queueing nothing, when the
  queue holds its limit of messages. A call statement drops the result
  (§7.1), so `qput(v);` ignores a refusal. A queue has no limit until
  `qlimit<T>(n)` gives it one of `n` messages; lowering a limit below what
  the queue holds keeps the messages queued, and puts fail until fewer than
  `n` remain. A limit of zero or less aborts with a runtime diagnostic.

### 11.3 Dynamic stacks (future)

This future feature is a resizable-of-resizables local in which each
element owns its own dynamically created stack and is accessed through fat
references. It is not in v1. (The thread mechanism of §11.2 already
introduces the dynamic-stack-block machinery this needs.)

---

## 12. Builtins, the standard library, and what v1 leaves out

The language's own functions:

| Builtin | Meaning |
|---|---|
| `print(a, b, …)` | the arguments' text and a newline to standard output (§3.7) |
| `str(a, b, …) -> u8[>..]` | the arguments' text as a fresh string, built at its destination (§3.7) |
| `format(out, a, b, …)` | the arguments' text appended to `out`, any growable `u8` array (§3.7) |
| `assert(c)` | aborts unless `c`, a `bool` or an optional, which it narrows (§9.3, §3.8) |
| `abort(msg: u8[:])`, `exit(code: i64)` | end the program; both diverge (§9.3) |
| `copy(x) -> T` | a fresh copy of the stored value `x` names (§4.1) |
| `default<T>() -> T` | the value a fixed-size `T` has before anything is written to it (§4.2) |
| `hardware_threads() -> i64`, `thread_spawn(worker, args…) -> i64`, `thread_spawn_contained(worker, args…) -> i64`, `thread_wait(id) -> bool`, `thread_wait(id, msg) -> bool` | workers (§11.2) |
| `qput(v) -> bool`, `qget<T>() -> T`, `qget<T>(ms) -> T, bool`, `qpoll<T>() -> T, bool` | the typed queues (§11.2) |
| `qlimit<T>(n)` | a queue's limit of messages (§11.2) |
| `to_bytes(a) -> u8[>..]`, `to_bytes(a, out)` | an array's image (a varint byte count, then its element region), either fresh or appended to a growable `u8` array (§3.9, `design/serialization.md`) |
| `bytes_of(a) -> u8[:]` | the element region alone, as a view: no copy, and never writable |
| `from_bytes<T[>..]>(b: u8[:]) -> T[>..], bool` | a *verified* array from an untrusted image; empty and `false` if it is not one. Also builds `T[>..<]` and the `T[]` family |
| `embed_shader(path)` or `embed_shader(stage, source, …) -> const u8[:]` | compile-time GLSL compilation to a static shader blob; graphics extension, detailed in the implementation notes, section 3.14 |
| `embed_file(path) -> const u8[:]` | the bytes of a file, read when the program is compiled, as a view of static data; `path` is a string literal (or a `let`/`const` global holding one), relative to the file with the call; each distinct file is read once |

And the array members, ordinary functions of their receiver per UFCS
(`a.push(v)` is `push(a, v)`):

| Member | On | Meaning |
|---|---|---|
| `.len -> i64` | every array kind and slices | the element count (§3.3) |
| `.cap -> i64` | limited arrays | the capacity (§3.3) |
| `.push(v) -> T&` | limited, grow-only, grow-shrink | one element, constructed in place (§3.3) |
| `.append(src)` | limited, grow-only, grow-shrink | an array or slice of elements (§3.3) |
| `.pop() -> T`, `.pop_n(n)`, `.resize(n, v?)`, `.clear()` | limited, grow-shrink; grow-only where §5.1 allows a shrink | shrinking, and growing with a fill value (§3.3, §5) |
| `.index_of(r) -> i64` | fixed, limited, grow-only, grow-shrink | the index of the element `r` refers to (§3.3) |
| `.alloc_index(v) -> i64`, `.alloc_ref(v) -> T&`, `.free(i)` | `reusable` pools | slot reuse (§5.4) |
| `.alloc_slice(n) -> T[:]`, `.realloc_slice(s, n) -> T[:]`, `.free_slice(s)` | `reusable[]` pools | slice reuse (§5.4) |

For serialization, the byte-count prefix must account for exactly the
remaining input. Validation establishes complete elements, representable
lengths, in-range tags, booleans encoded as 0 or 1, canonical varints, and
valid self-relative targets before exposing a value. A link must name an
outer element or the correctly tagged payload of one; interior-field
targets are currently unsupported. Failure returns an empty array and
`false`, not a partially verified value. Success owns a copy of the
payload. The implementation notes, section 6.9, give the byte contract and
current resource limits; section 11 identifies violations still to fix.

Everything else is the standard library: Goose source under `stdlib/`,
reached by `import` (§11.1) and documented in `stdlib.md` (the design it
came from is `design/stdlib_design.md`). The math types of §6.1 are its
`vec` module; the C behind `math` and `os` enters through `extern fn`
(§7.10).

Deliberately out of scope for v1: error-value conventions (§7.9); move
operations for resizables; multiple resizables per struct; two-way growth
arrays; inline compaction / copying GC for pools; mixed-type pools;
alignment annotations and explicit vector types (`simd fn`, §7.12, only
widens the instruction set a function is built for); dynamic stacks;
labeled break; namespace privacy, re-exports and nesting (§11.1).

---

## Appendix A. Worked examples (informative)

### A.1 A list of strings, flat

```goose
fn read_words(text: u8[:]) -> u8[][>..] {     // grow-only array of variable strings
    var words: u8[][>..] = [];
    var i = 0;
    while i < text.len {
        let start = i;
        while i < text.len && text[i] != ' ' { i++; }
        words.push(text[start..i]);            // copies the slice into place, flat
        while i < text.len && text[i] == ' ' { i++; }
    }
    return words;                              // NRVO: built on the caller's stack
}
```

The result is one contiguous block: `len | (len | bytes)*`. Iteration is
sequential, and no allocation happens anywhere.

### A.2 A compact tree with relative references

```goose
enum Sexp {
    Sym  { name: u8[varint] },
    List { kids: (Sexp..&<varint>)[varint] },  // tiny self-relative links
}
var pool: Sexp..[>..] = [];                    // grow-only pool of variable ADTs
```

Children are built first (pushed earlier into `pool`), and parents store
tiny backward offsets: `(a (b c d))` takes about 21 bytes in total and can
be serialized by memcpy.

### A.3 A growable hash table (open addressing), in-model

```goose
struct Map { slots: Slot[>..] }           // Slot fixed-size; grow-only
```

To grow the table, `slots.append(…)` doubles it in place (a bump, with no
move) and then rehashes in place by cycle-walking. Alternatively, rebuild
into a fresh local and copy back with whole-resizable assignment (§4.4).
Deletions use tombstones or the `reusable` mechanism. No allocator is
involved, and references are not invalidated during lookup.

### A.4 Case functions as virtuals

```goose
enum Node { Num { v: f64 }, Add { l: Node..&<u16>, r: Node..&<u16> } }
recursive fn eval(n: Node.Num&) -> f64 { n.v }
recursive fn eval(n: Node.Add&) -> f64 { eval(n.l) + eval(n.r) }
```

### A.5 Long-distance errors

```goose
fn parse_expr(l: Lexer&) -> Expr.. {
    guard l.tokens_remain() else { return Expr.Nothing {}, "eof" from parse; }
    ...
}
// All deep failures land here; the error is the last return value (§7.9).
fn parse(src: u8[:]) -> Expr.., u8[] { ...; return parse_expr(&l), ""; }
```

### A.6 A context of borrowed tables

```goose
struct Ctx {                          // a slice and three references: fixed-size
    source: const u8[:],
    nodes: Node[>..]&,
    text: u8[>..]&,
    scratch: i64[>..<]&,              // the recursion's working stack
}
recursive fn walk(c: Ctx&, n: i64, depth: i64) -> i64 {
    let mark = c.scratch.len;
    c.scratch.push(depth);            // above its callers' entries
    ...
    c.scratch.resize(mark);
    return total;
}
fn compile(source: const u8[:]) -> i64 {
    var nodes: Node[>..] = [];        // one local per table
    var text: u8[>..] = [];
    var scratch: i64[>..<] = [];
    ...
    var c = Ctx { source: source, nodes: nodes, text: text, scratch: scratch };
    return walk(c, 0, 1);
}
```

A struct owns at most one resizable (§3.4), and a function in a recursive
cycle owns none that stays in scope across a call back into the cycle
(§7.8). So a program whose state is several growable tables owns each table
as a local of a driver function, and passes the rest of the program one
struct of references to them. The tables are rooted outside the cycle,
which grows and shrinks them through `c` like any pool passed to it (§7.4,
§7.8). Since every call back into the cycle passes `c` on, the views read
through `c` (a node's name copied into `text`, for example) may also be
stored into the other tables inside the cycle. The tables last for one call
of the driver: each compilation starts from fresh tables, and no state is
global.

---

## Appendix B. TODO / open items

These items were collected from the design discussion, and each still needs
work to resolve. The newest, highest-priority items come first. Items that
have since been resolved are kept at the end, each with a pointer to where
its resolution is described.

0a. **Mixed-signedness ergonomics in practice** — §6.1's unification
    deliberately rejects `u32 + i32`, and `u64` against anything signed
    outside the comparison rule now in §6.1. Watch whether real code (hash
    kernels, size math against `.len`'s `i64`) still accumulates casts.
    This problem only affects `u64`: `u8`/`u16`/`u32` all widen implicitly
    into `i64` (§6.3), so the same code on a `u32` hash already needs no
    casts.
    What remains is a problem for the *analysis*, not the language: the
    bounds-check pass (§10.5) follows a `u64` value's range through
    `%`, `&` and casts, but drops it as soon as the value is bound to a
    `u64` local, because in general a `u64` need not fit `i64`. A `let` has
    one value, so it could carry its initializer's range as §6.1's
    non-negativity already does. That would be the narrow, checkable
    version of "track that this `u64` fits `i64`". Until then, the
    open-addressed map in `bench/goose/words.goose` keeps the four checks
    around `slots[idx]`.
0f. **Bounds-check analysis, known gaps** (§10.5) — a loop's exit condition
    is a disjunction (`!(i < n && p)`), which a difference-constraint domain
    cannot represent, so post-loop bounds rely on the inferred invariants
    instead. LLVM has the same limitation and works around it by
    duplicating blocks so the conditions never merge; that technique is
    available here too. A value loaded from an array has no known range, so
    indirect indexing (`dist[out[k]]`, `bench/goose/graph_csr.goose`) keeps
    its check unless what is stored has an index type (§3.11). Release-mode
    wrapping (§6.2) is deliberate and stays, so the analysis proves the
    absence of wrapping explicitly where it needs to.
0c. **Pointee writes through optionals** — writing through a narrowed
    optional works, but there is no way to write through an optional
    without narrowing it; rebinding to a plain reference first
    (`let r: T& = o;`) is the only workaround. This may be acceptable;
    revisit it with more usage.
0d. **Lifetime precision, remaining cases** — the checker is still more
    conservative than the spec in two places. First, long-distance returns
    (§7.9) written in a named function only carry references to
    globals/static data. The precise rule, which a function value's return
    follows, is that the references are rooted at or above the target's
    frame.
    Second, the recursive-cycle store rule (§7.8) admits as roots of stored
    references only globals, free variables, pool parameters and parameters
    that every call back into the cycle passes on. A reference to a
    caller's fixed-size local that such a call replaces is still
    pass-down-only inside a cycle, and so is a merged value that may be one
    rooted at a parameter the cycle passes on.
    (One-root-per-reference-variable is a language rule, §9.2; a function's
    result is rooted as a branch's value is, at the innermost of its
    returns' roots. Writability follows `const` types; storage no longer
    removes read-only restrictions, §9.5.)
2. **Error propagation sugar** — `return from` is the mechanism; revisit
   whether a convention/sugar layer (a `try`-alike) is wanted once idioms
   emerge.
3. **Move operation for resizables** — an assignment that leaves the source
   empty, as the only sanctioned "move".
5. **Cycle store rule refinement** (§7.8) — the pass-down-only rule is
   conservative. Parameters that the cycle passes on as they were given
   are exempt. A parameter that a recursive call gives other storage stays
   pass-down-only, even where every activation's store would outlive what
   it is given. So does a parameter whose first argument is a local of a
   recursive function outside the cycle. Explore per-activation reasoning.
6. **"Current pool" implicit destinations** — allocation without naming the
   array, bound at compile time to the in-scope array of the right type.
7. **Mixed-type pools & pool GC** — internal vs external references, inline
   compaction, copying; interacts with 6.
8. **Multiple resizables per struct** (memmove-on-insert, opt-in) and
   **two-way growth** arrays.
9. **Concurrency surface finalization** — queue fairness/capacity, select,
   worker-local init, flat-type relaxation for self-contained
   relative-reference values (send whole trees through queues).
10. **SIMD/alignment** — measure whether packed layouts cost real SIMD
    performance; consider opt-in aligned types if so. Related: narrow-lane
    elementwise ops (§6.1) should vectorize now that arithmetic runs at the
    element width; verify with the particle/sum benchmarks. `simd fn`
    (§7.12) gives a function versions for the wider instruction sets; what
    remains open is alignment, and whether a function should be able to
    say which versions it wants (an AVX-512 version is not faster for
    every loop).
13. **Labels for `break`** — if early-out patterns demand them.
15. **Wasm fallback** — index-based reference representation details
    (`docs/design/wasm.md` compares the options).
16. **Relative-reference region tracking** — copies of values containing
    *self-relative* references are currently rejected outright (§3.9; the
    `in pool` form already copies); track the region an offset ranges over so
    whole-region copies (and serialization moves) can be proven safe.
### Resolved

0e. **Per-array index types** — DONE, see §3.11 and §7.14
    (`docs/design/index_types.md`): `u32 in a` is an index validated once
    against a grow-only global `a`, which then never shrinks. So indexing
    `a` with it needs no check, and neither does indexing another array
    once their lengths are related. `checkfree fn` makes any check left in
    a function an error.
0b. **Reference address identity** — DONE, see §3.8: `r1 .== r2` /
    `r1 .!= r2` compare references by address; `==` stays the pointee
    comparison.

0h. **Passing by size class** — DONE, see §4.1 (and §7.2, §3.8, §6.5):
    fixed values connect to destinations by value; non-fixed lvalues bind
    by reference at reference-typed, untyped and un-annotated destinations
    and are an error at value-typed ones, which take an rvalue or an
    explicit `copy(x)`; a redundant `&` is a warning.
0i. **Resizable tails get their own frame header** — DONE, see C.2: a
    resizable-tailed struct with an all-fixed prefix is a frame object, so
    `&s.tail` is an ordinary reference; variable-size prefixes and
    resizable-class ADTs keep the bytes-on-stack shape.
0j. **Slices of grow-shrink arrays** — DONE, see §5.2 (superseding TODO 4):
    a slice or reference into a `[>..<]` is created like any other and held
    by variables only. A shrink while one is live is an error, which names
    the slice or reference and where it was bound. Specializations record
    what they shrink so that calls are checked the same way.
0g. **Recursive results' roots at back edges** — DONE (§7.8, cycle return
    roots): a cycle is checked in rounds. A back edge's result carries the
    merge of the roots the cycle's returns give, mapped through that back
    edge's own arguments. In the first round the result points nowhere
    yet, and the next round repeats that round's checks with the result. A
    result holding references is checked by the roots of what it
    holds, in the same way. One case remains: a back edge that passes
    storage of its own for a parameter the entry call gave static data gets
    a holder result that may only be passed down.
1. **varint format benchmark** — DONE: ULEB128 was adopted (§3.6). The
   break-even point against the best branchless format is at ~70–75%
   single-byte values (a cliff, not a slope); above it ULEB is ~3x faster,
   below it up to ~3x slower. Revisit only if a per-field format choice is
   ever wanted for unpredictable-length data.
4. **`[>..<]` interior-reference relaxation** — DONE, see §5.2: the
   liveness test of §5.1, plus per-specialization shrink summaries for
   the shrinks it cannot see directly (through a reference, or of a global).
11. **FFI** — DONE, see §§7.10–7.11: `extern fn` binds a Goose signature to a C
    function, an `extern "lib" { ... }` block binds its members from a shared
    library by name when the program starts, and `export fn` exposes a Goose
    function through a C-callable wrapper. The flat fixed-size types by
    value, references and slices to them, and `u8[>..]&` builders can cross
    the boundary where supported; other shapes are rejected at the
    declaration.
12. **Open syntax details** — DONE: both trailing-block parameter forms
    exist (the implicit `it` and `x =>`, §7.6), and `recursive fn` is the
    keyword alone on a cycle's entry function (§7.8).
14. **Stdlib math types** — DONE, see `stdlib.md` (`vec`): `vec2/3/4<T>`
    with `float3` and friends as aliases; the named ops are overloads per
    size, and elementwise arithmetic is the language's (§6.1).
17. **Serialization of relative-reference structures** — DONE, see §12 and
    `docs/design/serialization.md`: `to_bytes(a)` copies an array's element
    region into a fresh `u8[>..]`, `from_bytes<T[>..]>(bytes)` verifies an
    image and returns the array plus a bool. The verifier is generated per
    element type next to the size and equality walkers, and a rejected image
    yields an empty array, never an unproven reference (§9.4). An image is
    little-endian by definition and carries a varint byte count in front, so
    a reader of a stream can size its read; `bytes_of(a)` is the payload as a
    view, for a save that copies nothing. v1 does not support references
    into a *field* of an element, `from_bytes` into a fixed or limited array,
    or a big-endian host (which aborts rather than writing bytes only it can
    read).

---

## Appendix C. Representation and calling convention (normative for the C backend)

### C.1 Data stacks

Each data stack `S` has a control block `{ top: byte* }`. For the main
program the control block is a global; threads use an array of control
blocks reached from a hidden per-thread base pointer (§11.2). Allocation is
`p = S.top; S.top += size`. Scope exit restores the watermark recorded at
scope entry. No other runtime state exists.

### C.2 Value layouts

* Scalars: natural little-endian storage of their declared width; `bool` is
  1 byte, 0 or 1. All loads/stores may be unaligned.
* Fixed structs/arrays: packed concatenation in declaration order. A
  zero-length array (`T[0]`, or `T[..0]` after its length field) has one
  element slot holding no element, since C has no empty arrays (§3.4). A
  struct with no bytes of its own (no fields, and no `pad n`) takes up one
  byte, since C has no empty structs. A variant type with no bytes of its
  own takes that byte only as a value of its own, and takes no byte behind
  its ADT's tag (§3.4).
* `T[]` family: length field (of the declared storage type) then elements.
* `T[..k]`: length field (smallest unsigned type fitting `k`), then `k`
  element slots (uninitialized until first written).
* `T[..]`: capacity then length (both `u32` default), then capacity slots.
* ADT tags are zero-based variant indices in declaration order, stored in
  `u8` for up to 256 variants, otherwise `u16` in the current C backend.
  Fixed mode: tag, then payload area of max size (trailing padding
  uninitialized, never read). ADT variable mode: tag, then the actual
  variant's payload.
* varint: §3.6 (ULEB128; zigzag where signed).
* Reference: one pointer, except the fat form for resizables below.
  Optional: 0 = null. Self-relative: signed offset
  of the declared width from the offset field's own address; 0 = null
  only in the optional form (§3.9).
  Pool-relative (`in pool`, §3.9): unsigned offset of the declared width,
  `(target − base(pool)) + 1`, so 0 stays null and a `uN` width spans
  2^N − 1 bytes of the pool. `base(pool)` is the global's element region,
  fixed when its stack is reserved before any initializer runs; a function
  that loads or stores such a reference reads it once into a local at entry.
* Slice: `{ data: T*, len: int64 }` (len = element count).

**Resizable values.** A resizable's element region always tops its data
stack (§1.3(2)). A resizable array is represented as a header in the owning
frame (`{ base: byte*, len: int64 }`) plus the element region on the data
stack. `&v` yields the header's address (a fat reference is that pointer
plus the stack identity). With the stack identity known statically (or
carried as a fat reference or hidden argument), push compiles to
`*(T*)S.top = e; S.top += size; h.len++`.

A resizable-tailed struct whose other fields are fixed-size (and hold no
relative reference) is a **frame object**: a C struct of those fields
followed by the tail's own header (or, for a tail that is itself such a
struct, its frame object). It is held in the owning frame as a fixed value
would be, with only the innermost tail's elements on the data stack.
`&s` is the object's address plus the stack identity, `&s.tail` the tail
header's, so a tail can be referenced, sliced and grown through a
reference like any standalone resizable; `s.f` through either is a plain
member access. Copying, passing by value and returning such a value move
the frame object where an array moves its header (C.3) and the elements
where an array moves its elements. Nesting composes: a struct whose tail is
a frame object embeds it.

The two shapes that are not frame objects keep the bytes-on-stack form
behind a base pointer: a struct with a *variable-size* prefix (a `u8[]`
field before the tail) and a resizable-class ADT, whose payload shape is
per variant. Their header's `base` is the value's start on the data stack
and `len` the tail's count. A reference to such a nested tail is a compile
error; reference the owning variable instead. A tail of such a shape whose
type is a frame object takes the same form: its fixed fields are bytes of
the value, where a bare `pad` inserts nothing (§3.2), and its own tail's
count is that header's `len`, so its tail cannot be referenced either.

### C.3 Calling convention

* Fixed-size parameters and returns up to 4096 bytes: native C values
  (structs as packed C structs). The first such return uses the C return
  value; additional fixed returns use out-pointers. A slice, a reference to
  a resizable value and a reusable pool's reference parameter are passed as
  their members, each a C parameter of its own.
  A fixed aggregate result containing relative references instead uses a
  typed out-pointer at every size, and constructs directly through it.
  Ordinary C-return selection skips these results. A nonlocal return uses
  a saved pointer to the same receiver, rather than a copied result value.
* Larger fixed-size parameters: the caller makes a private value copy on a
  scoped data stack and passes its typed pointer. The callee can mutate
  that copy without affecting the argument's source. Larger fixed returns
  use typed out-pointers to caller-owned storage; any return temporaries
  also live on data stacks. Captures and `return from` keep their ordinary
  lifetime and value rules.
* `extern fn` retains its declared native C ABI (§7.10), including large
  packed structs passed and returned by value. C ABI copies can still use
  the native stack; pass a reference or slice when crossing C with a large
  value to avoid those copies.
* Nonfixed by-value parameters: caller allocates/constructs on the
  parameter's assigned stack (§4.3), passes the header (base, len, or
  metadata appropriate to the type) by value; callee owns and may grow it if
  it is resizable (its stack identity is part of the specialization or a
  hidden argument).
* Nonfixed return values: the destination stack is part of the callee's
  specialization or a hidden argument, as is the destination request's kind
  (one value vs element run, §7.3). The callee writes data at the
  destination's top and returns the outermost metadata (lengths/counts) in
  registers; in the element-run form it emits only raw elements. Because
  in-flight return data carries no inline header above the caller's live
  data, `v.append(f())` is always contiguous.
* Hidden arguments follow the declared arguments in this order: free-variable
  references (§7.5), then destination/target stack identities. The `return from`
  discriminant is a single thread-local int, not a signature argument: 0 means
  normal execution, and k means propagate to target k. It is nonzero only
  between a `return … from` and the catch in its target frame. An ordinary
  return therefore writes nothing. A call on a propagation path costs a load and
  a never-taken branch.

### C.4 Guard pages and commit order

Construction rules (§4.2) write stacks front-to-back. Windows commits on
fault: the runtime's handler commits a chunk from whichever page
was touched, so an allocation that skips ahead without writing (a large
limited-array capacity) needs no explicit commit, and nothing depends on
touch order. macOS and Linux map the usable region read/write and let the
kernel supply physical pages on touch. Fresh pages, including pages
discarded by `trim_memory`, arrive zeroed from the OS. Reused stack memory
is not zeroed. This is harmless, since uninitialized slots are unreachable
(§5.3). A final gap after each region, never committed, turns runaway
growth into a safe abort.

---

## Appendix D. Grammar sketch (informative)

```
program     := namespace? topdecl*             // imports may precede namespace
namespace   := "namespace" ident ";"
topdecl     := import | struct | enum | typealias | fndecl | externblock | globaldecl
import      := "import" "."? ident ("." ident)* ";" | "import" ident "from" strlit ";"
declname    := ident | ident "::" ident | "::" ident   // the namespace, else the file's
qname       := ident | ident "::" ident | "::" ident   // a declaration reference
struct      := "struct" declname generics? "{" fieldlist "}"
enum        := "enum" declname generics? "{" variant ("," variant)* ","? "}"
variant     := ident ( "{" fieldlist "}" )?
fieldlist   := field ("," field)* ","?
field       := ("let" | "const")? ident ":" type ("=" expr)? | "pad" intlit?
typealias   := "type" declname "=" type ";"
generics    := "<" ident (":" type)? ("," ident (":" type)?)* ">"

fndecl      := "checkfree"? ("export" strlit? | "extern" strlit?)? "checkfree"? "simd"?
               "inline"? "recursive"? ("fn" | "thread_fn")
               declname generics? "(" params? ")" ("->" rettypes)?
               (blockexpr | ("=" strlit)? ";")
                                                 // "=" strlit: extern only, its C symbol
                                                 // "extern" strlit: deprecated, the same
                                                 // checkfree at most once (§7.14)
                                                 // nested: ident only
externblock := "extern" strlit "{" externmember* "}"   // §7.10: the library, then its functions
externmember := "fn" declname "(" params? ")" ("->" rettypes)? ("=" strlit)? ";"
params      := param ("," param)* ","?
param       := "var"? ident (":" type ("=" expr)?)?
                                                 // untyped => generic; defaults
                                                 // trailing, not in a block (§7.1)
rettypes    := type ("," type)*                  // no parens in declarations
globaldecl  := ("reusable" ("[" "]")?)? ("let" | "var" | "const") declname (":" type)? ("=" expr)? ";"
                                                 // no initializer: var with a type only

type        := "const"? (prim | qname tyargs? | "(" type ")") postfix*
             | "const"? indextype                // no postfix (§3.11)
                                                 // const: the first & or [:] (§9.5)
indextype   := ("u8"|"u16"|"u32"|"u64") ("in" | "upto") qname
                                                 // parenthesized as a for binder's type
prim        := "bool"|"varint"|"i8"|…|"u64"|"f32"|"f64"
             | "fn" ( "(" types? ")" ("->" (type | "(" types ")"))? )?
tyargs      := "<" type ("," type)* ","? ">"
uint        := "u8"|"u16"|"u32"|"u64"|"varint"
postfix     := "[" expr "]"                      // fixed array (const expr)
             | "[" uint? "]"                     // variable array
             | "[" ".." expr? "]"                // limited (const expr)
             | "[" ">" ".." "<"? "]"             // resizable
             | "[" ":" "]"                       // slice
             | "&" ("<" uint ("in" qname)? ">")?  // reference / relative
             | "?"                               // optional (any type)
             | ".."                              // variable-mode ADT
             | "." ident                         // variant type

stmt        := decl | assign | incdec | guardstmt | deferstmt | exprstmt
decl        := ("reusable" ("[" "]")?)? ("let" | "var" | "const") identlist (":" type)?
               (("=" | ".=") exprlist)? ";"     // .= binds by reference (§3.8)
assign      := lvalue assignop expr ";"          // = .= += -= *= /= %= &= |= ^= <<= >>=
incdec      := lvalue ("++" | "--") ";"
exprstmt    := expr ";"
guardstmt   := "guard" expr ("else" blockexpr | ";")  // guards the rest of its block (§6.4)
deferstmt   := "defer" (blockexpr | assign | incdec | exprstmt)  // runs at the block's exits (§6.6)

expr        := control | binary
control     := ifexpr | matchexpr | blockexpr | loops | jumps
ifexpr      := "if" expr blockexpr ("else" (ifexpr | blockexpr))?
loops       := ("while" expr | "for" forbind "in" iter | "loop") blockexpr
forbind     := "&"? ident (":" type)? ("," ident (":" type)?)?
iter        := expr | expr ".." expr
jumps       := "return" exprlist? ("from" qname)? | "break" expr? | "continue"
matchexpr   := "match" expr "{" arm ("," arm)* ","? "}"
arm         := pattern "=>" expr
pattern     := ident "&"? ident | item ("," item)* | "_"   // a variant and its binder, ...
item        := bound (".." bound)?
bound       := "-"? (intlit | qname)             // qname: a variant or a constant (§8.1)
blockexpr   := "{" stmt* expr? "}"

binary      := (precedence climbing; tightest → loosest)
   postfixe := primary ( "(" args ")" trailingblock?
             | "[" sliceargs "]" | "." ident | "as" "!"? type )*
   unary    := ("-" | "!" | "~" | "&") unary
   levels   := * / %  →  + -  →  << >>  →  &  →  ^  →  |
             →  < <= > >=  →  == != .== .!=  →  &&  →  ||
primary     := literal | qname | "null" | "self" | "(" expr ")"
             | arraylit | structlit | genericcall
genericcall := qname tyargs "(" args ")" trailingblock?
trailingblock := "{" (params "=>")? stmt* expr? "}"
sliceargs   := expr | bound? ".." bound?         // bound := "^"? expr
args        := (expr ("," expr)* ","?)?
arraylit    := "[" (exprlist ","? | expr ";" expr)? "]"
structlit   := (qname tyargs? | varianttype) "{" (fieldinits | fieldinits? "..")? "}"
fieldinits  := fieldinit ("," fieldinit)* ","?   // before "..": the "," required
fieldinit   := (ident ":")? expr
```

Known parse notes:

* The `structlit` vs `trailingblock` vs `blockexpr` ambiguity is resolved
  as in Rust (no struct literals or trailing blocks directly in
  `if`/`while`/`for`/`match`/`guard` scrutinee position).
* A statement that is a block-ended construct needs no `;` and ends at its
  `}` (§2).
* `f<` commits to a type argument list only when followed by `(`, `{`, or
  `.ident {` (§7.7); otherwise `<` is comparison.
* `T&<u8>` tokenizes as `&` `<` in type context only.
* `a..b` ranges exist only in `for` headers, slice brackets, and match arms.
* Multiple return types in a function *type* (not a declaration header)
  require parens around the list.
* `T&?` parses to the same type as `T?`, and `?` on an already-optional
  type is an error.
* `from` is not reserved: `return E from f` is recognized by the identifier
  that follows it, so `from` remains usable as a name.

---

## Appendix E. Implementation notes (informative)

What the current compiler does where the text above leaves it a choice. The
compiler's own description, pass by pass and analysis by analysis, is
`implementation.md`.

* **Two C backends.** The generated C is normally written to a file for a C
  compiler to build (`-o out.c`). Where the compiler was built with TinyCC
  (the `third_party/tinycc` submodule), giving no output file compiles that
  same C inside the compiler's own process instead and calls its `main`, so
  a program can run without a C toolchain and without writing a file. The
  program shares the compiler's process: its exit status becomes the
  compiler's, and the compiler's own progress lines go to stderr so that
  stdout is left to the program. It runs on the compiler's main thread. The
  compiler reserves that thread's stack at 64 MB on Windows and macOS,
  where an executable's main thread gets 1 MB and 8 MB respectively by
  default; on Linux it is the stack limit the process started with
  (`ulimit -s`), as for an executable. The program's own C is the same for
  both backends, and `-D` is written into that C rather than passed to a
  backend so that this stays true. What differs is the runtime around it.
  The in-process build compiles the whole runtime with the program. The
  file holds only the parts of the runtime that have to be inlined into the
  program and declares the rest; `goose --emit-runtime` writes that rest as
  a C file of its own, built once and linked with every program
  (`--standalone` writes a file that holds the whole runtime, as the
  in-process build does). TinyCC does not optimize and cannot place
  thread-local storage in an in-memory run, so a program that uses workers
  (§11.2) is refused there (docs/design/jit_backend.md).
* **Fat references.** A callee that grows a resizable through a reference
  (`push` through a `f64[>..]&`) must know which data stack to bump. The
  compiler does not pin that stack per call site (§10.2): every reference
  to a resizable-class value carries the stack identity beside the header
  address (C.2), and a pool reference (§5.4) carries the freelist as well.
  There is no surface syntax; the form is per instantiation, and a variable
  keeps the one its first binding gives it (§5.4).
* **Stack assignment** uses the hidden-argument strategy permitted by §10.3.
  Every function that uses data stacks receives a base index as a hidden
  argument and addresses its nonfixed locals at constant offsets from that
  base. Callees start above the function's in-use watermark. All stacks
  reachable by the program are reserved when its thread program starts; their
  count is known at compile time (§7.8).
  The globals of a program instance, together with the dedicated stacks of
  the resizable globals, form one struct reached through a single
  thread-local pointer (a plain pointer in a program without workers).
  Main's struct is a static instance; a worker's is allocated when the
  worker starts and filled from the spawn image, which carries the used
  globals after the arguments. The only C statics are string literals and
  `const` globals with compile-time initializers. Every generated function
  is shared between thread programs and the main program.
* **Element-run results** (§7.3) exist for variable *array* results
  (`T[]`): an element-run receiver compiles the callee a second time in run
  form (raw elements plus a count out-parameter), so `v.append(f())` is
  contiguous with no copy, and a named result incurs the specified single
  callee-side copy. Callees with no run form (builtins, tag dispatch) fall
  back to the value form, and the receiver slides the length prefix out
  with one memmove. A runtime-capacity limited result is built on the
  destination stack, then compacted there to discard its header and unused
  slots. Non-array variable results have only the value form.
* **Queue images** (§11.2) are one contiguous byte image per value; a
  resizable's is its count, its fixed fields and its tail's elements, built
  on a scratch stack at `qput` and unpacked at the receiver's destination.
* **C locals at function scope.** Every aggregate-typed C local (a struct,
  array, slice or header) is declared at the top of its C function rather
  than in the block that uses it, and a literal temporary with unused
  limited-array slots is zero-initialized. Both work around MSVC
  19.44–19.51. At any optimization level it miscompiles a read through a
  copy of a struct that holds the address of a block-scoped local (for
  example a slice of a fixed array declared in a loop body, passed to an
  inlined callee), and it exploits the indeterminate bytes of a copied,
  partially written struct. Neither workaround changes what the program
  means.
* **The store record.** Every store of a reference, slice, or value holding
  one, into a variable, field, element or global, is logged program-wide
  with the stored value's root, exactness, and source container. A
  specialization also summarizes the stores into its parameters' pointees,
  and these summaries are replayed onto the caller's containers at each
  call. A grow-only shrink (§5.1) checks this record for any live value
  that holds a reference into the array, following source links (a copy
  holds what its source holds; a global source is checked by its type). A
  shrink inside a loop is checked again at the loop's end against the
  stores that the rest of the body made.
* **Liveness at a shrink.** The checker keeps the statement index of every
  open block; a holder is live when its name occurs in a later statement
  of an open block at or inside its scope, in that block's tail, or
  anywhere in an enclosing loop it outlives. Nested functions in scope are
  followed by name from the calls in that code. What a `for` walks in
  place, and the references and slices on the path to it that the loop
  loads again, are live throughout its body (§6.5).
* **Literal parameters** (§7.7). Which parameters are literals is part of
  the specialization key; their values are not. A read of a literal
  parameter is a value that is not constant-folded but keeps the literal's
  adaptability. Every adaptation of the bare parameter is recorded on the
  specialization with its line, and a literal parameter passed on as a
  literal records a link to the callee's parameter. After the whole program
  is checked, each call site's literal is verified against the closure of
  those records. The parameter is passed at its nominal type (`int64_t`,
  `double`), and each use casts it to the type it adapted to; the
  verification proved that cast exact.
