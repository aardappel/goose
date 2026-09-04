# The Goose Language — Specification (v4 draft)

This is the working specification of the Goose programming language, at a
level of precision intended to support a first implementation without
inconsistencies — not a full ISO-style spec. Unresolved items are collected
in the TODO list (Appendix B).

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
* **Static data** — constants and literals.

There is no general-purpose heap and no `malloc`. All dynamic allocation is
expressed as values living on data stacks, owned by locals (or globals).
(The runtime itself may allocate internally for thread queues, §11.2.)

### 1.3 The stack invariant

For each data stack:

1. Values are pushed in lifetime order and popped en masse when the owning
   scope exits. Lifetimes on one stack are strictly nested.
2. At most one *resizable* value is live per stack, and it is always the
   topmost value on that stack, for its entire lifetime: the region from a
   live resizable's first element byte to the stack's current top belongs
   exclusively to its elements (its own length header lives *below* its
   elements or outside the stack entirely, Appendix C). Growth is a pointer
   bump; no capacity checks are emitted (address space is pre-reserved;
   guard pages catch pathological overflow and abort safely).
3. Two simultaneously-live resizable values never share a stack. Locals with
   disjoint (sequential) lifetimes reuse stacks.
4. A *variable* value may be buried under later allocations on the same stack
   once its construction completes. While a value is under construction on
   stack S, nothing else may allocate on S; construction of nested variable
   parts proceeds in order as part of the same construction.
5. Fixed-size values are placed on the native stack / in registers whenever
   possible; the length header of an outermost resizable local likewise
   lives in the owning frame, not on the data stack (Appendix C).

The compiler proves all of this statically; there is no runtime bookkeeping
beyond the bump pointers themselves.

---

## 2. Lexical structure and syntax style

C/Rust-flavored syntax: `{}` blocks, `//` and `/* */` comments (block comments
nest), semicolon-terminated statements, postfix type annotations (`x: T`).
Identifiers `[A-Za-z_][A-Za-z0-9_]*`.

Literals:

* Integer: decimal and `0x` hex; the full unsigned 64-bit range is accepted
  (a value above `i64.max` is a `u64` constant, §6.2). Character
  literals `'a'` are integer constants.
* Float: with `.` and/or a decimal exponent (`1.5`, `2.5e-3`); C99-style hex
  floats with a mandatory binary exponent (`0x1.8p3`). A `.` starts a
  fraction only when a digit follows, so `1..2` lexes as a range.
* String `"..."`, with escapes `\n \t \r \0 \\ \" \' \xNN` (two hex digits).
* `null` — the empty value of any optional type `T?` (§3.8).
* `self` — inside a struct or variant literal, the value that literal is
  constructing; it exists to initialize non-optional relative-reference
  fields (§3.9, §4.2).

The language is **expression-oriented**: `if`, `match`, and `block` are
expressions; a block's value is its trailing expression. Assignment and
`++`/`--` are *statements*, not expressions.

**Statement termination.** Expression statements end with `;`, but a
statement that *is* a block-ended construct — `if`, `match`, `block`, the
loops, `guard … else { }`, a nested `fn`, or a call with a trailing function
block (§7.6) — needs none. Such a statement ends at its closing `}`:
operators never continue it (`foo(1) { it }` followed by `-x;` is two
statements), and using one as an *operand* requires parens
(`let v = (if c { 1 } else { 2 }) + 3;`). A redundant `;` after a block is
harmless. Trailing commas are allowed in every comma-separated bracketed
list: struct/array literals, call arguments, parameter and generic lists,
field lists, match arms.

Type syntax is postfix throughout: `T[k]` array of T, `T&` reference to T,
`T[:]` slice of T, `T?` optional T, `T&<u8>` relative reference,
`T&<u8 in pool>` one measured from a named pool, `Shape..` variable-mode ADT.

Evaluation order is left-to-right everywhere (operands, arguments, field
initializers — which named struct literals keep aligned with construction
order by requiring declaration order, §4.2). Overlapping copies have
memmove semantics.

A grammar sketch and precedence table are in Appendix D.

---

## 3. Types

### 3.1 Scalars

* `i8 i16 i32 i64 u8 u16 u32 u64` — the integer types, two's-complement,
  usable everywhere: fields, array elements, locals, parameters, returns,
  and expression temporaries. Every integer operation computes at its
  operands' exact type and width (§6.2) — a loop over `i32` data runs in
  32-bit registers end to end, which is what lets backends vectorize it.
  `i64` is the conventional default for indices, sizes, and counts (it is
  what `.len`, integer literals, and the builtins produce); the sized types
  are for data, and for kernels where the width is the point.
* `varint` — variable-length integer **storage type** (§3.6), the one
  integer spelling restricted to fields and array elements. Reads decode to
  `i64`; writable only at construction.
* `f32 f64` — IEEE floats, likewise usable everywhere. `f32` arithmetic
  stays 32-bit; `f32` widens to `f64` implicitly (§6.3), never the reverse.
  Float literals adapt to either type.
* `bool` — 1-byte storage, values `true`/`false`. Produced by comparisons;
  required by `if`/`while` conditions (no int-to-bool coercion).

Integer literals are *constants without a committed type*: they adapt to any
integer type whose range holds their value (`let x: u8 = 255;` is fine,
`= 256` is a compile error), and where nothing constrains them they are
`i64` (`u64` for values above `i64.max`). All other conversions follow one
rule — **implicit when provably value-preserving, an explicit cast
otherwise** (§6.3).

### 3.2 Structs

```goose
struct X { a: i8[3], b: i32, c: i64 = 0 }
```

* Packed by default: a struct of `i8[3]` + `i32` is 7 bytes. Unaligned
  access is assumed cheap; there is no automatic padding.
* `pad n` inserts n bytes explicitly; bare `pad` pads the *next* field to its
  own size, for compatibility with foreign layouts:
  `struct X { a: i8[3], pad, b: i32 }`. Pad bytes are never read and are
  ignored by `==`. In variable-class layouts (where offsets are dynamic)
  `pad n` still inserts n bytes, but bare `pad` has no defined alignment to
  aim for and inserts nothing.
* Fields are mutable by default; `let` before a field name makes it
  const-after-construction (transitively, via writability §9.5).
* A field may declare a default value (`c: i64 = 0` above); constructors may
  then omit it (§4.2).
* Layout is declaration order; variable/resizable fields obey §3.4.

Struct and enum declarations introduce **nominal** types. `type Name = T;`
declares an alias. Generic structs: `struct Pair<T> { a: T, b: T }`,
monomorphized like functions.

### 3.3 The array family

All array types share element type `T` and differ in how their size behaves
and is stored. In all of them, the representation is `[metadata][element
data...]`, inline, packed (exact layouts in Appendix C).

| Type | Class | Stored metadata | Notes |
|---|---|---|---|
| `T[k]` | fixed | none | `k` a compile-time constant expression (literals, named constants, arithmetic) |
| `T[]` | variable | length (`u32` default) | size chosen at construction |
| `T[u8]` etc. | variable | length of given unsigned int type | explicit length field type: `u8`–`u64` only |
| `T[varint]` | variable | varint length | most compact |
| `T[..k]` | fixed | length only (smallest int type fitting `k`) | capacity `k` is static (constant expression); in-place grow/shrink up to `k` |
| `T[..]` | variable | capacity + length (`u32` default) | capacity chosen at construction; in-place grow/shrink up to it |
| `T[>..]` | resizable (grow-only) | length | element region tops a data stack; grow = bump |
| `T[>..<]` | resizable (grow-shrink) | length | as above, may also shrink |

Indexing is bounds-checked — against `k` for fixed arrays (checks statically
elided where provable), against the current length for all others.

Element restrictions:

* Elements of `T[>..<]`, `T[..k]`, `T[..]` must be **fixed-size** (shrink or
  reuse of storage must never change the type layout of a location).
* Elements of `T[]` and `T[>..]` may be fixed or **variable** (e.g. an array
  of strings, all flat). They may not be resizable.
* An array with variable-size elements is *sequential*: it cannot be indexed
  randomly, only iterated / advanced element-by-element (offsets are data-
  dependent). It also has no `pop`-like operations (the start of the last
  element is not findable).

Growth operations (`push`, `append`, …) exist only on resizable arrays and on
limited arrays (`[..k]`, `[..]`) up to capacity (exceeding capacity aborts).
Shrink operations (`pop`, `resize` downward, `clear`) exist on limited
arrays anywhere, on `[>..<]` wherever no variable in scope refers into it
(§5.2), and on a grow-only `[>..]` exactly where the compiler can see that
no reference or slice into it is live (§5.1); they
abort when they would shrink below empty (`pop` on an empty array, `resize`
to a negative length).

Built-in members: `.len` (always, returns `i64`), `.cap` (limited arrays),
`.push(v)` (returns a reference to the new element on resizable and limited
arrays — the idiomatic way to link up just-built data),
`.append(src)` (src an array/slice of the element type), `.pop()`,
`.resize(n, v)` (grow with fill value `v`, or shrink), `.resize(n)` (shrink
only), `.clear()` per the rules above, and `.index_of(r) -> i64` (fixed,
limited and resizable arrays of fixed-size elements): the index of the
element `r` refers to, `(addr − base) / elemsize`. `r` must be rooted at the
array *exactly* (§9.2), which is what makes the division whole and the
result in range, so nothing is checked; a reference rooted elsewhere is a
compile error. Growth always supplies element values — no
operation can expose uninitialized slots (§5.3). Per UFCS these are ordinary
functions: `a.push(v)` is `push(a, v)`.

### 3.4 Placement rules (what may contain what)

* **fixed** values: anywhere — locals, globals, fields at any position, array
  elements, ADT payloads, params, returns.
* **variable** values: locals/globals, params, returns, ADT payloads, array
  elements (making the array sequential), and struct fields at *any* position.
  A field placed after a variable-size field is reached by dynamic offset
  computation (fine when iterating, costly for random access — a documented
  performance note, not a rule).
* **resizable** values: locals/globals, by-value params, returns, and the
  **final field** of a struct — recursively: a struct ending in a resizable
  is itself resizable and may only appear where resizables may. Also as the
  payload of a variable ADT (making that ADT resizable, same tail rules).
  Never an array element.

At most one resizable per struct (the tail). (Future extension: two
resizables with memmove-on-insert semantics, opt-in.)

### 3.5 Algebraic data types

```goose
enum Shape {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Point,
}
```

The only dynamic-polymorphism mechanism in the language (no inheritance, no
traits, no vtables). Representation: integer tag (smallest storage type that
fits the variant count, default `u8`) followed by the payload.

Every ADT *type* can be used in two modes, chosen at the use site by the
containing declaration:

* **fixed mode** (`x: Shape`) — usable only if all payloads are fixed-size.
  Size = tag + max payload size, padded to the largest variant (padding
  never read, ignored by `==`). A fixed-mode value may be **overwritten in
  place by a different variant**, but interior references into its payload
  may never be created.
* **variable mode** (`x: Shape..`) — size = tag + the actual variant's
  payload. Class is variable (or resizable, if the stored variant's payload
  contains a resizable tail). May have interior references into the payload,
  but the variant may **never be replaced in place**.

This dichotomy (replaceable XOR interior-referenceable) is what keeps
existential types sound (Grossman, "Existential Types for Imperative
Languages").

Payload-less ADTs are ordinary C-like enums and are fixed, 1 byte by default.
Variant types (`Shape.Circle`) are themselves nominal struct-like types
(§8.2).

### 3.6 varint fields

`varint` is **LEB128**: little-endian base-128, 7 payload bits per byte,
high bit = continuation; 1–10 bytes; full 64-bit range. LEB was chosen
because this type is optimized for values that are usually very small but
have occasional outliers, where it outperformed other formats (measurements
in `varint_bench/results.md`).

There is one `varint` type, with two encodings by position — this is
user-visible whenever Goose data is serialized directly:

* As an array **length field type** (`T[varint]`): unsigned ULEB128.
* Everywhere else (struct fields, ADT payloads, self-relative offsets):
  **signed**, zigzag-transformed (`(v << 1) ^ (v >> 63)`), so small
  negatives are as compact as small positives. A pool-relative offset (§3.9)
  is unsigned, so it is ULEB128 like a length. The transform sits on the
  value path, not the length/advance path, so it costs no decode latency.

Because the two encodings differ, varint values are never copied byte-wise
between contexts: any varint-to-varint construction (e.g. a struct varint
field initialized from `arr.len`) goes through `i64` — decode, re-encode.

Restrictions: a `varint` field/element is written only at construction of
its containing value; changing it means reconstructing the container. Reads
decode to `i64`; construction stores accept any integer type except `u64`
(a varint holds exactly the `i64` value range). A struct containing
`varint` fields is variable-class.
References to `varint` fields are always read-only (§3.8).

### 3.7 Strings

There is no built-in string type. A "string" is any array-family type with
element `u8`: `u8[]` (immutable flat string), `u8[varint]` (compact), `u8[>..]`
(string builder), etc. String literals are static constant `u8` data,
implicitly copyable into any of these representations and usable as `u8[:]`
slices directly (with non-writable provenance, §9.5). No encoding is
enforced; UTF-8 is a library-level convention.

**Text.** Every scalar, `bool`, and `u8` array or slice has a text form:
integers in decimal, floats in the shortest form that reads back to the same
value, `true`/`false`, and a `u8` array's bytes as they are. Three builtins
produce it, each taking any number of arguments and inserting nothing
between them: `print(a, b, …)` writes the forms to standard output followed
by a newline; `format(out, a, b, …)` appends them to `out`, any growable
`u8` array; `str(a, b, …)` builds a fresh `u8[>..]` of them, constructed at
its destination like any resizable result (§7.3), so `words.push(str("item",
i))` writes straight into the element. A character literal is an integer
(§2), so `str('x')` is `"120"`; a byte is appended with `push`. Aggregates
(structs, ADTs, arrays of other element types) gain a text form in a later
revision.

### 3.8 References

`T&` is a reference to a `T`: one machine address, no pointer arithmetic, no
casts, never dangling.

**References are transparent** (like C++ references): an expression that
denotes a reference behaves as its pointee in every value context — reads,
arithmetic, comparisons (`r1 == r2` compares the pointees), passing to a
by-value parameter, `print(r)` — all operate on the target. There is no
dereference operator; the load is implicit (`copy(r)` names a copy of the
pointee where §4.1 asks for one).

* Created with `&lvalue`. Lvalues are: local/global names, fields, indexed
  elements, dereferenced references, and (for reference creation) variable
  elements reached by iteration. `&` of an rvalue (temporary) is an error.
  `&` of a location that itself holds a reference yields the *stored*
  reference (there are no references to references).
* Writes: `r = v` (and `r += v`, `r++`, …) write the pointee, subject to
  writability provenance (§9.5) and the target's own rules. There is no
  `const`/`mut` distinction inside reference types.
* Rebinding: the special assignment `r .= &x` updates the reference *value*
  itself (the one thing transparency cannot express). `.=` applies to any
  reference-typed location — variables (subject to their `let`/`var`),
  fields, elements. On non-reference locations `.=` is an error.
* Binding contexts keep the reference rather than loading through it: an
  initializer/argument/field whose *declared type* is a reference type binds
  the reference value, and binds an lvalue of the pointee type by reference
  without `&` (§4.1; writing the `&` warns). Where a *variable's* type is
  inferred (`let x = r;`), a reference to a fixed-size value decays to a
  pointee copy — except an explicit `&lvalue` initializer, which infers the
  reference type — while a non-fixed lvalue binds by reference (`let w =
  words[0];` names the element). To bind a reference-returning call,
  annotate: `let e: T& = pool.push(v);`. An untyped parameter is an
  anonymous type variable and binds the argument's exact type, reference or
  not, exactly as an explicit `<T>` does (§7.7): for a fixed value `f(&x)`
  hands `f` a reference and `f(x)` a copy; a non-fixed lvalue is a reference
  either way.
* References to `varint` fields are always read-only (varints are written
  only at construction, §3.6).

**Optionals.** References are non-nullable by default. `T?` is an *optional
T*: represented as a nullable reference to T (null = address 0, no space
cost), with all reference semantics and restrictions (roots, lifetimes,
writability). This composes with any type — pass `i64?` for an optional
integer. Applied to a type that is already a reference, `?` simply makes
that reference nullable (`T&?` ≡ `T?`). The literal `null` is the empty
value of any optional type; an optional struct field with no declared
default defaults to null. Optionals are *not* transparent: access requires
narrowing via `if`/`guard`/`assert(r)`/`== null`/`!= null` tests (flow
typing: inside the guarded region the value behaves as `T&`; narrowing is
killed by rebinding the variable). `o .= &x` / `o .= null` rebind an
optional; a rebind to a plain reference narrows it, a rebind to anything
possibly null un-narrows it.

**Implementation note (fat references).** When a callee grows a resizable
through a reference (e.g. `push` through a `f64[>..]&`), it must know which
data stack to bump. Where the compiler cannot pin that stack statically per
call site (§10.2), such a reference costs two pointers instead of one — the
address plus the stack identity, passed as a fat reference or hidden
argument. There is no surface syntax; this is purely an implementation
choice per instantiation.

**What references may point to.** Anything except the interior of a
fixed-mode ADT payload (§3.5). A reference into a grow-shrink array `[>..<]`
lives in a variable only and must be out of scope at the array's next shrink
(§5.2).

References into a grow-only resizable `[>..]` remain valid for as long as
they can be named: grown memory never moves, and the array shrinks only
where the compiler can see that no such reference or slice is live (§5.1).
References into
limited arrays `[..k]`/`[..]` are also allowed and stay type-valid across
pop/push reuse (§5.3).

### 3.9 Relative references

A relative reference is a storage form of reference stored as a narrow
offset instead of an address. Spelled `T&<u8>`, `T&<u16>`, `T&<u32>`,
`T&<u64>`, `T&<varint>` — widths are spelled unsigned (or `varint`), like
array length field types. It comes in two forms, differing only in what the
offset is measured from: **self-relative**, the default, measured from the
offset field itself; and **pool-relative**, written `T&<u32 in pool>`,
measured from a named pool's base.

Both forms have the optional spelling `T&<u8>?`, which uses offset 0 as null
(no target can encode as 0: a self-relative reference to the offset field
itself is meaningless, and a pool-relative one is biased by one).

**Self-relative.** Constrained to point within the *same enclosing
array/pool* as the location storing it.

* Stored as a signed offset of the given width (`varint` offsets use the
  signed zigzag encoding, §3.6). Loading one yields an ordinary `T&` (base =
  address of the offset field itself). Storing one requires the compiler to
  see that both the reference and the destination location derive from the
  same root array: the two roots must be the same variable *and* both exact
  (§9.2), since a root that only bounds a lifetime does not say which array
  the offset would span. A reference read out of a container qualifies
  exactly when the read-back rule of §9.5 names one candidate; otherwise the
  error names the ones it could not choose between.
  The offset is range-checked at the store (abort on overflow of the width).
  Both ends lie in one root array, so the offset cannot exceed that array's
  span, and the check exists only where a root can be wider than the width's
  signed range: a root on a data stack spans at most the reservation (§10.4),
  so 2 GB or less of it needs no check for `u32`, and 32 KB or less none for
  `u16`.
  Varint-width relative references are written only at construction, like
  varint fields (re-encoding could change the byte length); fixed widths may
  be re-stored with `.=`/`=`.
* Copying a *value that contains* self-relative references (assignment from
  an lvalue, a by-value argument, a by-value match binder, an element copy)
  is a compile error: the copied offsets would still be measured from the
  source location. Construct such values in place (literals), and bind their
  match payloads by reference. (TODO 16: track the region a relative
  reference ranges over, so provably whole-region copies can be allowed.)
* Because they are position-independent, structures linked by self-relative
  references are trivially serializable / mappable.

**Pool-relative (`in pool`).** `pool` names a *global* `var` (or `reusable
var`) of a grow-only resizable type (`[>..]`) whose storage can hold a `T`
by value — an element, or a by-value field of one, transitively (the
candidate notion of §9.5). Anything else is a compile error, which for a
local or parameter pool points at the self-relative form: the pool is named
where the field is declared, and a local's name means nothing there.

* The pool is part of the type's identity: `Node&<u32 in pool>` is neither
  `Node&<u32>` nor `Node&<u32 in spare>`. Loading either form yields an
  ordinary `Node&`, and storing re-encodes.
* The stored value is `(target − base(pool)) + 1`, unsigned, so 0 is the
  null of the optional form and a `uN` width covers a pool of up to
  2^N − 1 bytes; `varint` is the unsigned LEB form (§3.6). Loading is
  `base(pool) + stored − 1`, and the result is a `T&` rooted at `pool`
  *exactly* (§9.2) whatever it was read out of.
* Storing requires the value's root to be exactly `pool`; otherwise the
  error names the value's root, or, for an inexact read-back, the candidates
  §9.5 could not choose between. The destination may be anywhere — another
  global, a local, a parameter's pointee — since the offset does not depend
  on where it is stored. Cross-array links are the point: a slot array can
  hold 4-byte links into the pool. The §9.2 store rule is trivially met, the
  pool being global.
* A width bounds the *pool*, not the distance between the two ends, so the
  store is range-checked exactly where the pool's reservation can exceed it
  (§10.4): `u32` at a 2 GB reservation needs no check, `u16` at any
  realistic one does.
  `base(pool)` never moves — a grow-only global's element region starts at
  its stack's reservation and growth only bumps the top.
* A value whose relative fields are *all* `in pool` copies like any other
  (assignment, by-value arguments, element copies, `pop`): its offsets do
  not depend on where it sits. A value mixing both forms does not.
* Whether a *parameter* points into `pool` is settled at each call site, so
  a function that relinks (`fn front(head: Node&, n: Node&)`) is specialized
  per pool like every other root class (§10.2) and needs nothing passed to
  it.

**`self`.** A non-optional relative reference has no null, so a value whose
links point back at itself — the sentinel of a circular list, the first node
of a pool — could not be constructed at all: there is nothing yet for it to
point at. `self`, written as the entire initializer of such a field in a
struct or variant literal, denotes the value that literal is constructing:
`Node { key: -1, prev: self, next: self }`. It is legal only there, and only
when the field's type is a non-optional relative reference whose pointee is
the type of the value the literal constructs (for a variant literal in fixed
enum mode, §3.5, that is the enum, tag included); `self` in a nested literal
names the literal it is written in, never an enclosing one. That type must
not be resizable: an offset alone cannot reach a resizable value's header,
which is why the same-root rule keeps every *other* relative reference away
from one. In a self-relative field the stored offset is minus the field's
own byte offset within the value, so it is the one relative reference whose
meaning does not depend on where the value lives. In an `in pool` field it
is the value's own offset in the pool, which only a literal being built
*inside* `pool` has — a `push`, an `alloc_index`/`alloc_ref`, or an element
store into it; anywhere else it is a compile error.
The point is that the whole structure can then be non-optional: with
optional links every load pays a null test for a null that never occurs
(that is what a sentinel is for), and non-optional relative references load
as a plain add.

**Which form.** Self-relative for position-independent blobs — a compact
tree that is saved, mapped or moved whole (single-byte links to nearby
nodes, see A.2) — and for structures living in a local or parameter pool,
which has no name to write. `in pool` for relink-heavy structures in a
global pool, where a store is a subtraction from a base already in a
register rather than from the field's own address; for links *into* that
pool from other arrays, which self-relative cannot express at all; and
wherever the offset is wanted as an index, since `&pool[i]` encodes as
`i * sizeof(T) + 1` and `index_of` (§3.3) reads it back.

### 3.10 Slices

`T[:]` = reference + element count, referring to a contiguous run of `T`s
inside some root. Slices are the universal "process a range" parameter type,
unifying all array representations. Like references, slices carry a root and
participate fully in the lifetime system (§9): they are always safe, never
dangling — Goose's fix for the danger of C++ `string_view`/`span`. They are
the intended *read* path; mutation idiomatically goes through references —
but writes through a slice are legal when its provenance is writable (§9.5).

* Created by slicing any array-family value or slice: `a[x..y]` (x inclusive,
  y exclusive; omit for 0 / len; `^k` means "len − k"). At call sites, an
  array argument passed where a slice parameter is expected implicitly
  becomes a whole-array slice; an exact-type overload wins over this
  coercion.
* Slices of fixed-element arrays index and iterate, bounds-checked against
  the slice's own length. Slices never grow; shrinking a slice (re-slicing)
  is always safe.
* Slices of variable-element arrays iterate only (no indexing); the count is
  an element count.
* Slices obey the same restrictions as interior references (§3.8): a slice
  of a grow-shrink array lives in a variable and must be out of scope at the
  array's next shrink (§5.2).
* A slice of a grow-only resizable taken before growth remains valid (it just
  doesn't see the new elements).

---

## 4. Values, copying, and assignment

### 4.1 Everything is a value; passing by size class

Goose has **value semantics**: a variable, field or element owns its value
inline, and a destination (an assignment, an argument, a return, a push, a
field initializer) receives a value of its own. How a value gets there
depends on its size class (§1.1):

* **Fixed-size values connect by value.** Scalars, structs, fixed arrays,
  references and slices are copied — a reference or slice copy is the
  reference/slice itself, never the pointee. Large fixed values copy
  silently; there is no size threshold.
* **Non-fixed values are never copied implicitly.** A variable- or
  resizable-class *lvalue* (a variable, field or element) reaching a
  destination binds by reference where the destination's type is a
  reference or slice, an untyped parameter (§7.7), an un-annotated `let`/
  `var`, or a `for` binding — and is an error at a value-typed destination.
  A value-typed destination takes an **rvalue** (a literal, a call's result,
  a constructing expression, built in place per §4.3) or an explicit
  **`copy(x)`**, which is a fresh copy of the stored value, constructed at
  the destination like any other rvalue. A function's own local is *moved*
  by `return` (§7.3).

So `f(xs)` hands a `u8[][>..]` variable to `fn f(xs: u8[][:])`,
`fn f(xs: u8[][>..]&)` and `fn f(xs)` alike by reference, `out.push(w)`
needs `out.push(copy(w))` when `w` names storage, and `x.f(a)` (§7.1) reads
as `f(x, a)` whatever `x`'s size class. `&x` is still how a reference to a
*fixed* value is spelled at an untyped destination (`let r = &n;`,
`f(&n)` into an untyped parameter); wherever the destination's own type is
a reference it is redundant, and a redundant `&` is a warning.

There is no ownership transfer beyond the return move, no destructors, no
`Drop`, no reference counting. Deallocation is exclusively scope exit
resetting stack pointers. (A `move` operation for resizable arrays —
assign + leave source empty — is anticipated but not in v1.)

Copies of variable/resizable values are real and cost O(size); with
`copy` they are also visible at the site that pays for them.

Shadowing: an inner scope may re-declare a name (a distinct variable).

### 4.2 Construction contexts

Variable and resizable values come into existence only at these points, each
of which provides fresh storage in a statically known place:

* `let`/`var` initialization of a local or global;
* a by-value argument slot (§7.2);
* a `return`ed value (§7.3);
* `push`/`append` into a resizable (the new element region);
* a field/element inside a larger value under construction.

Construction writes the value front-to-back (metadata, then elements /
fields in order), which is what invariant §1.3(4) relies on. Construction of
a limited array writes only its metadata and any provided elements; the
remaining capacity is reserved but **uninitialized** — this is safe because
no read path to uninitialized slots exists (§5.3), and cheap because the
runtime commits skipped address ranges explicitly (Appendix C.4).

Literal forms usable in any construction context:

* array literals `[1, 2, 3]`; `[]` where the element type is known from
  context, or as the whole initializer of a `var` local (`var out = [];`),
  which makes the local a grow-only `T[>..]` whose `T` is fixed by the first
  `push`, `append`, `format` or whole assignment into it — a string literal
  pushed into one makes it a `u8[][>..]` — and must be fixed before the local
  is otherwise used or its scope ends; `[v; n]` fill form for fixed arrays;
* struct literals `X { a: 1, b: 2 }` (named) or `X { 1, 2 }` (positional, in
  declaration order; no mixing). Named initializers must also appear in
  declaration order (out-of-order names are a compile error: values construct
  front-to-back, and reordering would obfuscate either evaluation order or
  cost). Fields with declared defaults (§3.2) may be omitted: trailing ones
  in the positional form, any of them in the named form;
* `[..cap]` — an empty limited array `T[..]` with the given construction-time
  capacity (`cap` a runtime expression); the reserved slots stay
  uninitialized (§5.3, C.4);
* variant literals `Shape.Circle { r: 1.0 }`;
* `self`, inside a struct or variant literal only, as the initializer of a
  non-optional relative-reference field pointing at the very value being
  constructed (`Node { prev: self, next: self }`, §3.9) — the one way to give
  such a field a value before anything else it could point at exists;
* string literals (§3.7);
* `copy(x)`: a fresh copy of the stored value `x` names (or the pointee of a
  reference), constructed at the destination like any rvalue -- the one
  way a non-fixed value reaches a value-typed destination from storage
  (§4.1). Copying a temporary is an error, since it is fresh already.
* `default<T>()`, for any fixed-size `T`: the value a `T` has before anything
  is written to it — numbers 0, `false`, null optionals, empty slices and
  empty limited arrays, variant 0 of an ADT — with declared field defaults
  (§3.2) applied wherever a struct or payload declares them, so an invariant
  a declaration encodes as a default survives being filled in by generic
  code (a `sum`'s accumulator, a hash table's empty slots). A type that
  contains a non-optional reference without a declared default has no
  default value, and `default<T>()` for it is a compile error. (The zero
  value a missed `qpoll` yields, §11.2, is the all-zero-bytes value; the two
  agree except where a field declares a non-zero default.)

### 4.3 The copy-free construction guarantee

A constructed nonfixed value is always built **directly in its final
destination**. The compiler propagates "construct onto this stack"
information top-down through expressions and calls (whole-program,
call-graph-order compilation makes this always possible): all branches of an
`if`/`match`/case-dispatch construct to the same destination, and a function
whose result is nonfixed is compiled against its destination stack — either
statically per specialization, or via a hidden destination-stack argument
when one compiled body serves call sites with different destinations.
Construct-then-copy never occurs. (The semantic fallback — construct on a
fresh stack, then copy — is definable but the compiler is required not to
need it.) Copying an *already-constructed* value (assignment from a
variable, a by-value argument that is a variable) is an ordinary copy; the
guarantee is about newly constructed values — and §7.3 extends it to
returned locals.

### 4.4 Assignability

An lvalue may be assigned (`=`) after construction iff its size cannot change
or it can absorb the change (for reference-typed lvalues these rules govern
the *pointee* write that `=` performs; the reference value itself is updated
only by `.=`, §3.8):

* fixed-size lvalues declared `var`: assignable (plain overwrite copy);
* whole resizable arrays (top of their stack): assignable — semantically
  clear-then-construct; the bump pointer resets to the array's element start
  and the new contents are built in (from an rvalue or `copy(x)`, §4.1);
* limited arrays `[..k]`/`[..]`: assignable if the new length fits capacity;
* fixed-mode ADT lvalues: assignable, including with a different variant;
* **not** assignable: variable-class lvalues (`T[]` locals/fields/elements,
  variable-mode ADT lvalues, `varint` fields) — these are frozen at
  construction; rebuild the container instead.

`let` forbids assignment through that name/field, and makes references
derived from it non-writable (§9.5). Definite assignment is enforced: no
reads of uninitialized locals; every declaration either has an initializer
or is provably assigned on all paths before use. Fixed arrays require full
initialization (every slot is indexable); the `[v; n]` fill literal makes
large ones cheap.

### 4.5 Equality and comparison

`==`/`!=` are **structural** and require both operands to have the same
type: scalars and bools by value; structs and fixed arrays memberwise (pad
and ADT padding bytes excluded — semantic comparison is per-member; memcmp
is a valid optimization only for gap-free layouts); array-family values and
slices by length then elements (sequential walk for variable elements); ADTs
by tag then payload.

References and slices follow a *top-level rule*: as the direct operands of
`==` they have value-like semantics — a reference compares its pointee
(transparency, §3.8), a slice compares length then elements. As *members* of
a compared composite they compare by identity (the reference address, the
slice's address+length): recursing through them would turn `==` into an
unbounded pointer traversal. Optionals compare as nullable references
(`o == null` is the null test). Ordering `< <= > >=` exists on the numeric
types only, with operands unified per §6.1.

---

## 5. The resizable/shrink rules

### 5.1 Grow-only `[>..]`

"Grow-only" names the guarantee, not the operation set: **the array never
shrinks while a reference or slice into it can be live.** Memory below the
top never moves and is never reused while the owner lives, so every interior
reference and slice stays valid for as long as it can be named, and a bound
on the length holds across every `push`.

* `push`/`append` bump the stack.
* May contain variable-size elements (build strings/ADTs in place, §7.3).
* Shrinking — `pop`, `resize` downward, `clear` — is legal exactly where the
  compiler can see that nothing is rooted in the array, under the conditions
  below. `pop` and `resize` additionally need fixed-size elements, since a
  sequential array cannot find its last element (§3.3).

This is the workhorse type: arenas, pools, string builders, tree storage, and
scratch that is refilled or popped between phases.

**When a grow-only local may shrink.** The receiver must be a *local* of the
function being compiled — not a global, not a field, not an array reached
through a reference, whose holders lie outside anything the check can see —
and not a `reusable` pool (§5.4: its freelist keeps every slot live). The
call must stand on its own: a statement, the initializer of a declaration, or
the right-hand side of an assignment to a variable, so that no reference
taken earlier in the same expression outlives the shrink; and not inside a
block, `if`, `match` or loop that produces a value, whose enclosing expression
may hold such references too. At the shrink, no variable in an open scope may
hold a reference or slice rooted at the array. Scopes are what make this
usable: what a loop body or a nested block took out of the buffer is gone at
its end, so a scratch buffer refilled per iteration, or a stack popped between
phases, can hand out slices of itself — "reusable scratch" and "structure I
can point into" are the same type. The test is conservative wherever roots
are (§9.2): a live value of any type containing references counts as a holder
unless the array is declared deeper than it, as does a `var` reference that
the same-depth rebinding rule could retarget into the array. The operations
themselves are the grow-shrink ones: a stack-top move and a length store.
Assigning the array whole (`a = …`) replaces its elements and is a shrink
under the same rule.

### 5.2 Grow-shrink `[>..<]`

* Fixed-size elements only.
* `pop()` returns the element by value; `resize`/`clear` allowed, from
  anywhere: on a local, through a reference, on a global, on a struct's
  tail. Assigning the array whole is a shrink too.
* References and slices into it are created like any other (§3.8, §3.10),
  and are **held by variables only**: bound to a local, passed down,
  returned — never stored into a field, element, or global. Rationale: after
  a pop, that stack-top memory can later be reused by *different types*
  (other locals, other pushes), so no reference into it may outlive the next
  shrink; keeping such references out of storage is what makes the next rule
  a scan of the variables in scope.
* **A shrink is an error while any variable in scope may refer into the
  array** — the test a grow-only local's shrink applies (§5.1). The error is
  at the shrink and names the variable and where it was bound, so either end
  can be changed: end the slice's block before the shrink, or move the
  shrink. What differs from §5.1 is a shrink through a reference or of a
  global, which cannot see the callers' variables: each function
  specialization records what it shrinks — which globals, and which
  parameters' pointees, directly or through its own callees — and a call is
  an error while a variable in scope refers into an argument or global the
  callee shrinks. A call into a recursive cycle still being checked counts
  as shrinking every grow-shrink array it can reach. Function values run
  inline, so a shrink inside a block is checked against the block's own
  enclosing scopes.
* `push` returns a reference to the new element, and `index_of` works, as on
  grow-only arrays.
* Iterating with `for` uses indices under the hood; the `&x` binding is a
  variable in scope, so a shrink inside such a loop is an error, while
  mutating the length inside a by-value `for x` loop is legal and merely a
  logic hazard (bounds checks keep it safe).

### 5.3 Limited arrays `[..k]` / `[..]`

Grow and shrink freely within fixed capacity; not stack-top-bound (their
capacity is reserved at construction), so they can live anywhere fixed/
variable values live (`[..k]` is fixed-class, `[..]` variable-class). A
`T[..]` constructs either from element contents (capacity = initial length)
or with the `[..cap]` literal (capacity `cap`, length 0).

Interior references and slices **are allowed**, including surviving pops:
because capacity and element type are fixed, every slot within capacity,
once first written, remains a valid value of the element type forever —
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
  live value of its type forever. Where the code holds references rather
  than indices, `a.free(a.index_of(r))` (§3.3) is what turns one back.

Semantics, not just implementation: *all elements remain valid at all times*.
A reference to a freed-then-reused slot reads a different (same-typed) value —
type-safe reuse, never memory corruption. This is the language's answer to
tree-mutation workloads that would otherwise need an allocator.

---

## 6. Expressions and arithmetic

### 6.1 Operators

C/Rust set: `+ - * / %` (`%` is Euclidean on integers, §6.2; on floats it is
C `fmod`), comparisons, `! && ||`
(short-circuit, `bool` only), bitwise `~ & | ^ << >>` (on integers),
assignment statements `=`, `+=` etc. on assignable lvalues, `.=` (reference
rebinding, §3.8), and `++`/`--` as statements on integer lvalues (no
expression form). Precedence: Appendix D. Range expressions `a..b` appear
only in slicing brackets, `for` headers, and match arms.

**Operand unification.** A binary numeric operator's operands must reach
*one common type*, which is also the result type, found as follows: equal
types stand; a constant adapts to the other operand's type (compile error
if its value does not fit); otherwise, if exactly one operand implicitly
widens into the other's type (§6.3), the wider type wins. Anything else —
same-width signed/unsigned, `u64` with anything signed, int with float — is
a compile error asking for a cast. Nothing here invents a type absent from
the expression: `u8 + i64` is an `i64` add, but `u32 + i32` does not
silently become 64-bit math — that would smuggle the wide operations this
type system exists to avoid. Exceptions: shifts take the *left* operand's
type as the result (the count is any integer type, masked per §6.2), and
`==`/`!=`/orderings unify the same way but produce `bool`. Unary `-`
requires a signed (or float) operand; `~` any integer, keeping its type.

**Comparisons and `u64`.** A comparison produces `bool`, so unlike every
other binary operator it has no result type to choose, and the mathematical
answer across a sign boundary is never in doubt. `u64` is the one unsigned
type with no signed supertype (`u8`–`u32` widen into `i64`, §6.3), so it is
the only one this ever bites. A comparison between `u64` and a signed type
is therefore allowed **when the signed operand is known non-negative**: it
converts to `u64` without changing value, and the comparison is a single
unsigned one — no wider than either operand, and never a hidden branch.

"Known non-negative" is deliberately a *syntactic* property, not an inferred
one: a non-negative integer literal, a `.len` or `.cap` (non-negative by
§10.4), or a `let` bound to one of those. Whether a comparison compiles thus
depends only on what is written, never on how much the optimizer managed to
prove. Anything else is a compile error asking for the cast — the honest
outcome, since the conversion could then change the value. Note that writing
that cast by hand is not a cheaper workaround but a wrong one: `x as! i64`
on a `u64` above `i64.max` silently compares as negative, and the checked
`x as i64` aborts in debug on a value that was perfectly legitimate to
compare. Within its rule, the direct comparison is the only correct
spelling as well as the cheapest.

**Elementwise math**: the arithmetic operators apply memberwise to any two
values of the *same* struct/fixed-array type whose scalar leaves are all
integers or all floats; the result has that same type. This covers vector
math without an operator-overloading feature. The standard library supplies
`float3` and friends; named vector ops (`dot`, `cross`, `normalize`, …) are
ordinary stdlib overloads per math type, not language builtins.

### 6.2 Integer semantics

* Every integer operation computes **at its operands' type**: `u8 + u8` is
  an 8-bit add, `i32 * i32` a 32-bit multiply. There is no promotion —
  operands reach a common type only by the unification rule of §6.1, which
  never widens both sides behind the programmer's back.
* Signedness is part of the type: `/`, `>>`, and the ordering comparisons are
  unsigned operations on unsigned types (`>>` shifts in zeroes) and signed on
  signed ones (`>>` replicates the sign bit).
* **`%` is Euclidean** at every integer type: the result lies in `[0, |b|)`
  and is never negative, whatever the dividend's sign. So `x % n` is a valid
  index into a length-`n` array for *any* `x` — the reduction idiom means
  what it looks like, and the optimizer can drop the resulting bounds check
  (§10.5) instead of demanding a guard the programmer knows is redundant.
  On unsigned types this is ordinary remainder; on signed ones it costs one
  predictable conditional add over C's truncating `%`, on an operation that
  is already a division. Consequently `i64.min % -1` is `0`, not a trap.
  (A truncating form may be added later if a use for it appears; float `%`
  stays C `fmod`, whose convention numeric code expects.)
* **Signed overflow** (at the operation's width): aborts in debug builds;
  wraps two's-complement (defined) in release. The debug abort is per
  operation *as it executes*, not per operation as written: the optimizer
  may regroup an associative chain or drop an operation altogether, and
  each check travels with the operation it belongs to. So a debug build is
  a bug-finding tool, not a promise about every intermediate the source
  spells out; what a release build computes never depends on any of it.
  **Unsigned arithmetic wraps modulo 2^width by definition, in every
  build** — modular arithmetic is what hashing, PRNGs, and bit manipulation
  mean by unsigned math, and it is why those kernels are written on
  unsigned types. Division/modulo by zero: aborts always. `i64.min / -1`
  aborts in every build (it would trap in hardware); at narrower signed
  widths the same case is an ordinary overflow — debug abort, release wrap.
  Shift counts are masked to `0..width-1` (defined).
* Integer literals above `i64.max` (up to `u64.max`) are `u64` constants
  (§2, §3.1); negating one is a compile error (except `-(2^63)`, which is
  exactly `i64.min`).

### 6.3 Conversions

One principle: within a kind, a conversion the machine can prove
value-preserving is implicit; anything that could lose a bit or flip a sign
takes a cast — and crossing between int and float is always a cast, exact
or not.

* **Implicit** (silent, everywhere a value meets a differently-typed
  destination or operand): to a *wider* integer type of the same
  signedness (`i8→i16/i32/i64`, `u8→u16/u32/u64`); from an unsigned type to
  any *strictly wider* signed type (`u8→i16..i64`, `u32→i64`); `f32 → f64`;
  and constants into any type their value fits (§3.1). Also: any integer
  type except `u64` into a `varint` store (§3.6).
* **Never implicit**: narrowing; same-width signedness changes (`i32 ↔ u32`);
  anything signed into any unsigned type (a negative value can hide in any
  signed operand — so `u32→i64` is silent but `i32→u64` is not); `u64` into
  any signed type; `f64 → f32`; and int ↔ float in either direction.
* `x as T` — explicit conversion between any two numeric types,
  **range-checked in debug** (abort on value change), truncates/wraps/
  rounds-toward-zero silently in release.
* `x as! T` — always-unchecked wrap/truncate, for when losing bits is what
  is intended, even in debug.
* float → int (both forms in release, `as!` always) is defined exactly:
  truncate toward zero, then wrap modulo 2^64 into the target's width; NaN
  yields 0. The common in-range case is one compare and a hardware
  conversion; only the out-of-range tail pays for the defined wrap.

### 6.4 Control expressions

```goose
if c { e } else { e }        // expression; both arms required when used as value
while c { s }
for v in e { s }             // see §6.5
loop { s }                   // infinite; exit via break
block { s }                  // early-out construct: break E exits with value E
guard c else { s }           // s must diverge; after the guard, c holds
guard c;                     // shorthand: exit the innermost valueless construct
match e { ... }              // §8
return e? (from f)?          // §7.3, §7.9
break e?                     // exits innermost loop/block, optionally with value
continue
```

`block { }` exists to promote early-out style anywhere, not just at function
top level. `break` binds to the innermost `loop`/`while`/`for`/`block`;
labels are not in v1. All `break E` of one construct must agree on E's type.

`guard c else { s }`: the block runs when `c` is false and must diverge
(`return`, `break`, `continue`, or a call that never returns: `abort(msg)`,
`exit(code)`, §9.3); code
after the guard proceeds with `c` known true — including flow-narrowing of
`T?` (`guard r else { return; }` leaves `r: T&`). The bare form `guard c;`
is shorthand for `guard c else { break }` inside a loop or `block`, and for
`guard c else { return }` otherwise; it is valid only where that implicit
exit requires no value.

### 6.5 `for`

Built-in iteration only (no iterator protocol):

* `for i in a..b` — integer range, half-open `[a..b)`; the bounds unify per
  §6.1 and `i` runs at that type (an `i32` range gives a genuinely 32-bit
  loop variable).
* `for i in n` — sugar for `0..n`; `i` has `n`'s type.
* `for x in arr` — element copies for fixed-size elements, at the element's
  type; element references for non-fixed ones (§4.1), whose walk is
  sequential.
* `for &x in arr` — element references (the mutation form for fixed-size
  elements; redundant, and a warning, for non-fixed ones). Over a `[>..<]`,
  the binding is a reference in scope: no shrink inside the loop (§5.2).
* `for x, i in arr` / `for &x, i in arr` — with index (`i: i64`).

All array-family types and slices are iterable. Custom access patterns are
provided by HOFs taking static function values (§7.6), which compile to
plain loops.

---

## 7. Functions

### 7.1 Declarations and generics

```goose
fn name(a: i64, var b: f64, xs: i32[:]) -> i64 { ... }
fn generic<T>(a: T, b: T) -> T { ... }
fn also_generic(a, b) { ... }        // untyped params are generic
```

* Free functions only. No methods, no impl blocks. UFCS: `x.f(a)` is exactly
  `f(x, a)`; resolution tries fields/built-in members first, then functions.
* Parameters are immutable bindings by default; `var` makes the (by-value)
  parameter a mutable local.
* A parameter with no type annotation is generic (Lobster-style); explicit
  `<T>` parameters express same-type constraints and let signatures name
  types. Return types may be omitted where inferrable (required across
  recursive cycles, §7.8).
* Overloading by parameter types is allowed; resolution is static: the
  unique overload matching exactly wins, else the unique one matching after
  coercions (array→slice §3.10, literal fit §3.1, implicit widening §6.3),
  else tag dispatch (§8.2), else error. Ambiguity is an error.
* Multiple return values: `fn f() -> A, B`; received as `let a, b = f();`.
  There is no tuple *type* — multiple returns are a calling convention;
  structs are the way to keep data together. (Function *types* with
  multiple returns need parens — `fn(i64) -> (A, B)` — to disambiguate the
  comma; declaration headers don't.) Nonfixed types are allowed in any
  return position; each nonfixed result gets its own destination per §7.3
  (possibly distinct stacks).

Declarations at top level are order-independent (whole-program compile);
only global initializers have ordered semantics (§11.1).

### 7.2 Parameter passing

Uniform with `=` (§4.1): the parameter's type is the destination, and the
argument connects to it by size class. A fixed-size argument is copied into
a by-value parameter and bound by a reference-typed one (`fn inc(n: i64&)`
takes `inc(n)`; `inc(&n)` says the same and warns). A non-fixed lvalue
argument binds by reference to a reference, slice or untyped parameter and
is an error at a by-value one — `f(copy(xs))` spells the copy, `f(make())`
constructs the result in the parameter's slot (§4.3). Array→slice at call
sites (§3.10) is the one shape change, cheap and copy-free by definition.
A by-value non-fixed parameter reserves the statically assigned stack region
for the value at the call site, constructs it there, and the callee owns it
like any local.

### 7.3 Return values and result placement

Fixed-size returns use registers/native stack as usual. A variable or
resizable return value is **constructed directly in its destination**
(§4.3); the callee is compiled knowing the destination stack (statically or
as a hidden argument), writes element data there, and returns the value's
metadata (lengths) in registers — the destination stack never carries a
transient header between the caller's data and the new elements
(Appendix C.3):

* `let x = f();` — a fresh stack region (statically assigned to `x`).
* `v.push(f());` / `v.append(f());` — the top of `v`'s stack; the push is a
  no-op on return beyond `v.len` adjustment (the data is already in place).
* `g(f())` — the argument slot of `g` (its statically reserved stack).
* `x = f();` (resizable `x`) — `x`'s stack, replacing its contents.

**Named results (guaranteed NRVO).** When every `return` of a nonfixed
result returns the same local variable (and those returns are its last
uses), that local is allocated at the return destination from its
declaration — `return x` then costs exactly the same as returning the
constructing expression directly. When different locals are returned on
different paths, only one can live at the destination and the others are
copied on return (the one place a returned value can cost a copy).

**Destination requests.** The construct-here information handed to a callee
(or any constructing expression) comes in one of two forms, chosen by the
receiver: *one value of type R* (receivers that store a value: `let`,
params, `=`, `push` of one element — R's nested metadata, if any, is part of
the constructed bytes), or *a run of elements of type T* (receivers that
splice: `append`) — the callee emits raw elements and returns the count.
The request kind is part of the specialization signature (§10.2); the same
function may be compiled in both forms for different callers. A callee that
merely constructs its result satisfies either form with zero copies; one
that must first operate on its result as a whole array makes the copy
itself, just before returning — at most one copy, on the callee side, and
only in that case. (Often even that is avoided: a callee local resizable
assigned the destination stack is operated on via its frame header,
Appendix C.2, and its elements are already in place.)

Implementation status: both request kinds exist for variable *array* results
(`T[]`): an element-run receiver compiles the callee a second time in run
form (raw elements + count out-parameter), so `v.append(f())` is contiguous
with no copy, and a named result pays the specified single callee-side copy.
Callees with no run form (builtins, dynamic dispatch) fall back to the value
form; the receiver then slides the length prefix out with one memmove.
Non-array variable results have only the value form for now.

Consequence: returning a built-up value and out-parameter style are the same
cost, and building a variable element "inside" a container is idiomatically a
function call in argument/push position.

### 7.4 Growing through a reference

Passing `&v` where `v` is resizable lets the callee push/append: per call
site the compiler specializes on the identity of the stack `v` tops (§10.2),
or passes it as a hidden argument / fat reference (§3.8) when one body serves
multiple stacks. No surface syntax distinguishes these.

An in-scope "current pool" mechanism (allocation without naming the array,
compile-time bound to whichever suitable array is in scope) is deferred
(TODO).

### 7.5 Nested functions and free variables

Functions may be declared inside functions. A nested function may read and
write the enclosing function's locals ("free variables"), subject to those
variables' normal rules (writability §9.5, roots §9.2, shrink rules §5).

Implementation model: free variables become hidden reference parameters of
the nested function. Since nested functions are non-escaping (like all
function values, §7.6) this is always safe. When a nested function is passed
as a static function value and inlined into its HOF — the expected, common
case — the hidden parameters disappear entirely; un-inlined builds (debug)
keep them as real arguments.

### 7.6 Static function values

```goose
fn foo<F: fn(i64)>(a: i64) { F(a); }
foo(1) { print(it) }                  // trailing-block sugar, implicit `it`
foo(1) { x => print(x) }              // named block params
fn h(v: i64) { print(v) }  foo(1, h)  // or pass a named function
```

Function values are compile-time entities passed as generic parameters, not
runtime data. The `: fn(...)` bound is optional documentation: a bare `<F>`
works identically — the call `F(a)` typechecks per instantiation like
everything else (passing a non-function just produces the error at that call,
reported with the instantiation chain).

Function values capture enclosing locals per §7.5, and cannot escape:
storing them, returning them, or putting them in data is a compile error.
Every call is direct and inlinable; HOFs compile to hardcoded loops
(Lobster-style guarantee). There are no closures-as-objects and no runtime
function pointers in v1.

A plain `return` inside a function value returns from the **lexically
enclosing named function**, not from the HOF that calls the value — so a
HOF-based iterator has the same structuring power as `for`. This is
implemented with the `return from` mechanism (§7.9), the HOF's frames
unwinding transparently.

### 7.7 Generics and monomorphization

Monomorphic specialization, C++/Lobster-style: type parameters are
unconstrained; the body is re-typechecked per unique combination of (type
arguments, reference roots (§9.2), writability provenances (§9.5), static
function values, destination stacks). Errors are reported at the offending
instantiation **with the compile-time call chain** — the whole-program,
call-graph-order compiler can always show which call path produced the
failing instantiation.

Type variables are never checked abstractly. A generic body is checked only
at an instantiation where every type is concrete — including the result of
every call it makes on a function-value parameter, since that value's body
is checked inline against the concrete argument types at that point (§7.6).
So a HOF never needs to state what its function value returns: `map`'s
result element type is simply the type `F(x)` turns out to have, even when
the block is `{ generic(it) }` and that type depends on the instantiation.

**Call-site type arguments.** Type arguments are inferred from the argument
types whenever they appear in the parameter list: `fn foo<T>(x: T)` is
called as `foo(1)`, never `foo<i64>(1)` — the typechecker must support this
for both `<T>`-style and untyped (implicitly generic) parameters. An
explicit list `f<i64>(x)` is allowed, and *needed* only when no argument
mentions the parameter (e.g. `qget<i64>()`). Syntactically, `f<` commits to
a type argument list only when the `<…>` is immediately followed by `(`,
by `{` for a struct literal (`Pair<i64> { … }`), or by `.ident {` for a
variant literal (`Opt<i64>.Some { … }`); otherwise `<` is the comparison
operator.

Passing arrays by reference is the generic way to write mutating range
algorithms (each array-family type instantiates its own copy); slices are
the uniform non-mutating way.

### 7.8 Recursion

Non-recursive call graphs are the default and require nothing. Recursion is
opt-in and annotated: the entry function of every recursive cycle is marked
`recursive fn` (exact annotation verbosity TBD, TODO 12), all functions in the
cycle need fully explicit signatures (no inference across the cycle
back-edge), and — the key restriction — **no function in the cycle may
declare locals requiring a new data-stack assignment**. Growable data used
by recursive code must be owned outside the cycle and passed in (references,
slices, reusable pools). The compiler checks this in call-graph order;
recursion depth then only consumes native call stack. Unnamed nonfixed
*temporaries* (e.g. an intermediate call result) are exempt: they cannot be
referred to across activations, so the soundness argument holds — but an
implementation may then consume data-stack slots proportional to recursion
depth for them (aborting past its limit).

**Cycle store rule.** Within a recursive cycle, distinct activations of the
same local are statically indistinguishable, so the §9.2 depth check is not
sufficient there. Therefore: a reference whose root is a local of a cycle
function may be passed *down* as an argument, but may not be stored into any
location, nor returned. References rooted outside the cycle are
unrestricted — in particular a pool handed to the cycle by reference (a
parameter whose pointee is resizable-class, which no cycle function can own),
so a recursive builder can push into a caller's local pool and link what it
pushed. Because the cycle's functions are checked once against the entry
call's roots, every recursive call must pass such a pool by the same
reference the entry call did: swapping two pools, or passing a different one,
at a back edge is a compile error. (Further refinements are future work,
TODO.)

**Cycle return roots.** A back edge reaches a function whose own returns may
not have been checked yet, so the root of its result cannot come from them.
Instead, **the return roots of a cycle are the fixpoint over the returns of
the functions in it**, computed before any of their bodies are checked: a
return of `X.push(…)`, `X.alloc_ref(…)` or `&X[…]` gives the root of `X` (a
global, or a parameter, whose root each specialization already has from its
call site); a return of a reference variable gives the root of what it was
bound to; a return of `g(…)` gives `g`'s return root, mapped through the
argument that carries it; iterating settles the mutual definitions. A cycle
function may therefore `return` the result of a back-edge call, and a
parenthesised subexpression in a recursive-descent parser needs no wrapper
node (`bench/goose/calc_noparen.goose`). Every real return is then checked
against the fixpoint's answer, and the §9.2 rule that all returns agree
applies as usual.

Where the fixpoint cannot determine a root — a callee that returns one of two
of its own pool parameters, say, so which one it is depends on the call site —
the result of a back edge is not treated as static data (that would let it be
stored into a global and outlive the pool it points into). It carries instead
a root that outlives nothing: such a result may be passed down, but storing or
returning it is a compile error.

### 7.9 `return … from` (long-distance return)

```goose
return E from f     // f = a named function on every compile-time call path
                    //     from f to this statement
```

Returns `E` as the result of the innermost active call of `f`, unwinding all
frames in between. It is the language's opt-in, lightweight "exception"
mechanism (e.g. a deep parser/loader error doing `return err from
load_level`), and the mechanism behind function-value `return` (§7.6).

Semantics and implementation:

* Validity is checked statically in call-graph order: every call site of the
  returning function must lie within the dynamic extent of an `f` call.
* Each function on a path between `f` and the `return from` gains a hidden
  return discriminant: "result is for my caller" vs "propagate for target
  `f`" (an enum when multiple targets are possible). Callers check it and
  immediately return in the propagate case. Normal epilogues run, so all
  intermediate data-stack watermarks restore correctly. There is no
  unwinder, no tables, no destructors (there are none to run).
* `E`'s destination follows §7.3 for `f`'s call site. No special stack rule
  is needed: a pending nonfixed return's destination stack is in the in-use
  watermark from `f`'s call site through `f`'s entire dynamic extent
  (§10.3), so no function between `f` and the `return from` can own data on
  it — unwinding intermediate frames cannot disturb the in-flight value.
* Multiple `return from` sites and multiple targets compose; agreement with
  `f`'s return type applies as usual.

Error handling idiom: there is deliberately **no specified error-value
convention** (bool, enum, string, i64 — application's choice; by custom the
error is the last of multiple return values). Short-distance: manual
multi-value returns. Long-distance: `return from`.

---

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
```

* Over ADTs: exhaustive over variants; `_ =>` wildcard allowed; arm binds
  the variant payload (`c: Shape.Circle` above).
* Over integers (any integer type): constant and half-open-range arms; `_`
  required; pattern values must fit the scrutinee's type.
* Arm binders are explicit about copy vs reference, like the rest of the
  language: `Circle c =>` binds the payload *by value* — a copy, potentially
  a large one for variable-size payloads — and `Circle &c =>` binds it *by
  reference* (the `for &x` spelling; `Circle& c` is the same tokens).
* `&`-binders are legal only on **variable-mode** payloads (whether the
  scrutinee is a value or a reference): a fixed-mode value may be
  overwritten with another variant — inside the arm included — so a
  reference into its payload is exactly what §3.5 forbids, and matching a
  fixed-mode ADT (even through a reference) offers by-value binding only.
  The variant can never be reassigned through a `&`-binder.
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

* Variant types (`Shape.Circle`) are first-class types, usable directly and
  behind references (`Shape.Circle&`).
* Calling the overload set with the ADT type (or a reference to it) performs
  tag dispatch (jump table); exhaustiveness is checked exactly like `match`:
  every variant must have exactly one applicable overload; return types must
  agree. All arms construct any nonfixed result to the same destination
  (§4.3). The overloads' parameter types choose copy vs reference like match
  binders do (`Shape.Circle` vs `Shape.Circle&`), with the same §3.5 rule: a
  fixed-mode scrutinee — even behind a reference — dispatches to by-value
  variant parameters only.
* Dispatch is on one parameter position (v1 rule: multi-position dispatch is
  an error). Other parameters pass through unchanged.

This is the language's "virtual function" idiom, without vtables or
inheritance, and it keeps the closed-world exhaustiveness guarantee.

---

## 9. References, lifetimes, and safety

### 9.1 The one rule

**A reference/slice must not outlive the variable that owns its target, and
must never observe its target at a wrong type.** Everything below is the
static enforcement of this; there are no lifetime annotations anywhere in
the surface language, and no aliasing/exclusivity restrictions at all
(aliased references are fine — without shared-memory concurrency, aliasing
alone cannot break type safety given the layout rules of §3/§5). This
outlives-rule is the language's entire "borrow checker".

### 9.2 Roots and the depth check

Every reference/slice value has a static **root**: a local or global variable
that bounds the scope its target lives in. The root is **exact** when that
variable's own storage contains the target, and inexact when it only bounds
the target's lifetime — the owner is then that variable or one further out.
Every root a `&lvalue` creates is exact; the reads out of containers of §9.5
are where inexact ones come from. Compilation in call-graph order with
per-instantiation specialization means roots are always statically known —
parameters' roots come from each call site, and functions are specialized per
distinct root (Rust-lifetime precision via monomorphization, with zero
syntax).

The rules below are the *scope* rules, and hold of exact and inexact roots
alike. Only rules that need the target's **identity** rather than its lifetime
consult exactness — storing a reference into a relative-reference location
(§3.9), converting one to an index (`index_of`, §3.3), and the compiler's
proof that two references name different arrays — and each of those takes its
conservative answer without it. A relative reference that names a pool (§3.9)
is where an exact root also *comes from*: a load out of one is rooted at that
pool, exactly, whatever container it was read out of.

Rules (scopes ordered by nesting; globals are the outermost scope, §11.1):

* **Store**: `r` may be stored into a location owned by root `L` only if
  `scope(root(r)) ⊇ scope(L)` — the pointee provably outlives the container.
* **Return**: a returned reference's root must be visible to the caller (a
  caller-supplied root, a global, or the function's own in-place-constructed
  return value).
* Struct types with reference fields are implicitly generic over those
  fields' roots; struct instances with different root bindings are distinct
  types for checking purposes (same layout).
* References into a value being copied by value do not transfer to the copy;
  they keep referring to the source (plain value semantics).
* A reference *variable* commits to its first binding's root: `.=` may
  rebind it only within the same root, or to one at the same scope depth
  (the common case: retargeting to another element of the same or a sibling
  container in a loop). Anything else needs a new variable. This keeps the
  checker single-pass over loop bodies. A rebind to a different root leaves
  the variable inexact, since it no longer names one array; and since a loop
  body is checked once, such a rebind is rejected outright when the
  variable's root has already been used as an identity earlier in that loop.
* All `return`s of one function must agree on the returned reference's root
  (v1 simplification; use one source or split the function).
* Inside recursive cycles the stricter §7.8 cycle store rule applies.

Violations are compile errors. There is no escape hatch in v1.

### 9.3 What the runtime still checks

Aborts (message + exit; not catchable):

* array/slice indexing out of bounds (elided wherever the compiler proves it
  cannot fire, §10.5; can be disabled wholesale in a designated unsafe-fast
  build);
* limited-array capacity overflow;
* shrinking below empty (`pop` on an empty array, `resize` to a negative
  length);
* relative-reference offset overflow at store (only where a root array, or
  a named pool, can span more than the width holds, §3.9);
* debug only: integer overflow (per operation as it executes, §6.2), `as`
  range violations;
* division by zero (always);
* `assert` failures, and the program's own `abort(msg)` (`msg` any `u8`
  array or slice; printed as `goose runtime error: <msg>`);
* guard-page hits (stack budget exceeded) — safe abort, never corruption.

`exit(code)` ends the program normally with the given process exit code.
Both `abort` and `exit` never return, which the checker knows: code after
them is unreachable, and either may be the whole of a `guard`'s else block
(§6.4).

### 9.4 The residual unsafety, stated honestly

With `reusable` arrays and limited arrays, a stale reference can read a
*different value of the correct type* (type-safe reuse). This is the entire
extent of "dangling"; it can produce a logic bug, never memory corruption,
never a type confusion, never OOB. This is the deliberate trade that buys
allocator-free speed.

### 9.5 Writability

Reference and slice types carry no const/mut markers. Instead, *writability
is an inferred provenance attribute*, tracked per instantiation exactly like
roots, with zero syntax:

* Non-writable provenances: static data (string literals, constants), and
  anything derived from a `let` binding or `let` field — `let` is genuinely,
  transitively const.
* A write through a non-writable reference/slice (or a shrink/grow operation
  through one) is a compile error at the offending instantiation.
* Everything else is writable. Slices remain the read idiom by convention,
  not by rule.
* Provenance is tracked through *direct* derivation paths (variables,
  fields, `&`, slicing, calls), not through storage round-trips: a
  reference stored into a container and read back out is writable regardless
  of its original provenance. This laundering is deliberate — the struct
  definition (its `let` fields) is the source of truth for what may be
  written through paths *it* controls, and the loophole is this pragmatic
  language's const-cast.

**Read-back roots.** A container names a scope, not the storage its contents
point into, so the root of a reference or slice of pointee type `T` read out
of a container `C` is re-derived. A **candidate** is a variable whose own
storage can hold a `T` by value — an array of `T` in any array kind, a struct
or ADT payload with a `T` field, a `T` itself, and so on through by-value
nesting; a variable that merely holds *references* to `T` is not one, and a
reference or slice field ends the search. Static data is a candidate for the
element types a literal can supply. Then, by where `C`'s own root lies:

1. **A global.** Only globals outlive globals (§11.1), so the owner is a
   global candidate whatever local scope is open. One candidate: that
   variable, exact. Otherwise the global scope, inexact.
2. **A local of the function being checked**, at any block depth,
   by-value parameters included. Everything stored into `C` was reachable
   from this frame and had to outlive `C`, so the owner is a local declared
   at or outside `C`'s block, a reference parameter's pointee, a global, or
   static data. The root is the innermost such candidate — the true owner is
   that one or one further out, so its scope bounds every possibility — and
   is exact when there is exactly one candidate in all.
3. **A reference parameter's pointee, or itself inexact.** The owner may be
   caller storage this function cannot enumerate: the root is `C`'s, inexact.

A relative reference `T&<w>` read out of `C` points within `C`'s own root
array by construction (§3.9), so it takes `C`'s root and `C`'s exactness
whichever case applies. One that names a pool needs no candidates at all: it
points into that pool, so it is rooted there and exact, and this is how a
container of links stays usable where a container of plain references would
be ambiguous.

A diagnostic that turns on an inexact read-back names the container it came
out of and the candidates it could not choose between ("`n` was read out of
`slots` and may point into `pool` or `spare`"), or, where the candidates are
the caller's to know, the parameter whose pointee bounds it.

Consequence, accepted deliberately: the check is callee-driven — a utility
function that mutates its slice argument compiles in one calling context and
errors in another. The call-graph-order compiler always reports such errors
with the full compile-time call chain, so the origin (e.g. "this slice came
from a string literal at …") is visible. This buys most of const-correctness
with none of the type-soup churn.

---

## 10. Compilation model

### 10.1 Whole program, call-graph order

Goose is a closed-world, whole-program compiler. Functions are typechecked
and specialized in call-graph order (callers before callees), so argument
types, reference roots, writability, destination stacks, and static function
values are always concrete. Recursion is the annotated exception (§7.8).

### 10.2 Monomorphization

Each function is compiled per unique (argument types, reference roots,
writability provenances, destination/target stacks and request kinds,
static fn values). Roots and stacks are compile-time constants inside each
specialization — pushes through references compile to direct bumps of a
known global — except where the compiler chooses a hidden stack argument to
share one body across contexts (§4.3, §7.4).

### 10.3 Stack assignment (deterministic recipe)

Traverse specializations DFS from `main` (and global initializers). Each
specialization carries a compile-time *stack environment*: the watermark of
stack indices in use on entry, and a map from each of its nonfixed locals /
by-value params / in-flight returns to an index. Assign each new
simultaneously-live resizable (and each data-stack-resident variable value,
which shares by nesting per §1.3(4)) the lowest index not in use at that
point; indices free again when the owning scope exits (sequential reuse).
A pending nonfixed return's destination stack is part of the in-use set
from the call site until the value is received — which is also what makes
`return from` safe (§7.9). N = the maximum index + 1 reached anywhere.
Threads run the same algorithm per thread program (§11.2).

Implementations may replace per-context specialization with hidden runtime
stack arguments where profitable; semantics are identical.

Fixed locals go to the native stack / registers. Variable-class locals may
be placed on the native stack (`alloca`) instead of a data stack when the
compiler chooses; the reference v1 strategy is: everything nonfixed on data
stacks, everything fixed native.

### 10.4 Runtime environment

At startup, reserve N address regions (target: multiple GB each; commit-on-
touch via guard pages — prototyped at github.com/aardappel/stackalloc),
plus guard gaps between regions so runaway growth aborts cleanly. Platforms
without address-space reservation (wasm today) fall back to index-based
references + bounds-checked growth, with reduced performance.

**The 48-bit size limit.** A data stack reserves **at most 2^48 bytes**.
Consequently, in every conforming implementation:

* no value's byte size exceeds 2^48;
* no array's element count exceeds 2^48 (stated separately because an
  element type may be zero-size, and because most array types are already
  far tighter — `T[]`'s length field defaults to `u32`, §3.3);
* therefore every length, capacity, index, and byte offset lies in
  `[0, 2^48]`, and `.len` and `.cap` are non-negative by construction.

This is a deliberate trade of unreachable range for reasoning the compiler
can rely on everywhere, and it costs nothing to enforce. 2^48 is the
canonical address width current 64-bit hardware actually implements, so the
limit is above anything reachable (256 TB in one value); the guard region
after each reservation already turns an attempt to exceed it into a safe
abort (§9.3), so no growth operation needs a check of its own, and a fixed
array's size is a constant checked at compile time. Implementations may
impose a *smaller* limit (wasm32 is inherently capped at 2^32); they may not
raise it, so a program's meaning never depends on the target having more.

What the limit buys, and why it is worth a spec clause rather than an
implementation assumption:

* **Size arithmetic cannot overflow.** With 15 bits of headroom below `i64`,
  `len - 1`, `i + 1` for `i < len`, `len + len`, `len * 2`, and
  `i * element size` are all in range. The optimizer may assume this rather
  than prove it, and the bounds-check analysis (§10.5) relies on it directly.
* **Signed is the right default for sizes.** `.len` returns `i64` (§3.1) and
  the top bit is provably unused, so the sign bit costs nothing real, while
  subtraction and difference math stay natural. This is the trade C++'s
  `size_t` gets backwards.
* **Non-negativity is a type-level fact, not an inferred one**, which is what
  lets §6.1 admit the one mixed-signedness comparison that matters without
  any analysis being involved.

Compilation target: C/C++ first, LLVM later. Representation and calling
convention: Appendix C.

### 10.5 Bounds-check elimination

The index and slice checks of §9.3 are the one abort the compiler routinely
proves unnecessary, so the analysis that removes them is part of the
compilation model rather than an implementation detail. It runs over each
specialization after optimization, and it is required only to be *sound*:
every check it removes is one that could not have aborted, and what it fails
to prove stays in the program. Nothing about a program's meaning depends on
how much it proves.

What the language gives it, beyond ordinary flow facts (loop headers,
conditions and their negations, `assert`, match arms):

* **Monotonicity in the type.** A grow-only `[>..]` shrinks only at a `pop`,
  `resize` or `clear` on the array itself (§5.1), which the analysis sees, so
  a bound established before a `push` still holds after it. This is a
  guarantee a resizable-array type without the grow-only/grow-shrink split
  cannot offer.
* **Roots.** Every reference's root is static per specialization (§9.2), so
  a call cannot invalidate a length the callee has no path to — storage is
  reachable only through a global, a capture, or an explicit `&` (§3.8).
* **Static extents.** `T[k]`'s length and `[..k]`'s capacity are constants.
* **Total operations.** `%` is Euclidean (§6.2), so a reduction is in range
  by construction; a completed `pop` proves the array was non-empty, because
  the empty case aborts.

Known gaps are TODO 0f. Whole-program compilation makes the analysis
intraprocedural without loss: each specialization sees concrete argument
roots, and a caller's facts reach a callee body once it is inlined.

---

## 11. Program structure

### 11.1 Modules and globals

* One program, compiled whole. `import a.b.c;` includes the file `a/b/c.goose`
  once, resolved relative to the *main file being compiled*; the form
  `import .a.b;` (leading dot) resolves relative to the *importing file*
  instead. All declarations are public in v1; name collisions are errors (no
  namespacing yet). Top-level declarations are order-independent.
* Globals are declared like locals (`let`/`var`, any type including
  resizable). Semantically the whole program runs inside an implicit
  outermost scope owning them: they participate in the depth check (§9.2) as
  the outermost roots, initialize in declaration order before `main` (their
  initializers may call functions), and a global resizable simply owns a
  stack's bottom for the program's life (a natural whole-program arena).
  `let` globals of flat fixed type with compile-time-evaluable initializers
  live in static data.
* Entry point: `fn main() { }`. Only the *root* file's `main` is the entry;
  a `fn main` in an imported file is ignored entirely (not an entry, not
  callable, no collision). A runnable file can thus double as an importable
  library: give it `fn main_x() { ... }` plus a `fn main() { main_x(); }`
  wrapper, and importers call `main_x` directly.

### 11.2 Concurrency

No shared mutable memory, ever. The model is a statically typed cousin of
the Linda tuple-space / coordination style.

* A worker entry point is declared `thread_fn worker(a: i64, ...) { }`.
  The compiler compiles a `thread_fn` and everything it calls **as a
  separate program** with its own stack assignment (its own N′). Because
  many instances of a thread program can run at once, a thread program's
  stack references are inherently dynamic: each thread carries a pointer to
  its own block of N′ stacks, and stack accesses in thread-program code are
  indexed off it (one hidden register-resident base). A function is shared
  between thread programs / the main program only when all its stack
  references arrive via (hidden) arguments; any static stack use
  re-specializes it per program, exactly like a template instantiation.
* Worker count is decided **at runtime** (no static maximum):
  `thread_spawn(worker, args…) -> i64` reserves a fresh stack block, copies
  the args, starts the worker, and returns its id. `hardware_threads() ->
  i64` exists for sizing. `thread_wait(id)` blocks until that worker
  returns — enabling both scoped fork/join parallelism and orderly shutdown
  (send quit messages, then wait). Workers still running when `main`
  returns are killed.
* A thread program may not access globals — that would be shared mutable
  memory between programs; the typechecker rejects it for every function a
  `thread_fn` reaches. Data enters through the spawn arguments and queues.
* Args and queue elements must be **flat** types (§1.1); values are copied
  in and out, which is cheap because Goose values are contiguous.
* **Typed queues**: conceptually one queue per flat element type;
  `qput(v)`, `qget<T>() -> T` (blocking), `qpoll<T>() -> T, bool`
  (non-blocking; bool = got one; on a miss the value is T's zero value —
  zeroed scalars, empty arrays, variant 0) select the queue by type. Queues
  live outside stack memory (runtime-internal allocation permitted here).

### 11.3 Dynamic stacks (future)

A resizable-of-resizables local, each element owning its own dynamically
created stack, accessed via fat references. Noted for the future; not in v1.
(The thread mechanism of §11.2 already introduces the dynamic-stack-block
machinery this needs.)

---

## 12. Deliberately out of scope for v1

Standard library contents (beyond: `print(x)` for scalars and u8-arrays/
slices — with `str` and `format`, §3.7 —, `assert`, `abort`, `exit`,
`default<T>()`, `hardware_threads`, and the math types of §6.1 are assumed;
the library's design is in `stdlib_design.md`); FFI details (an `extern fn`
C boundary is assumed to exist, unchecked by nature); error-value
conventions (§7.9); move operations for resizables; multiple resizables per
struct; two-way growth arrays; inline compaction / copying GC for pools;
mixed-type pools; SIMD/alignment annotations; dynamic stacks; labeled
break.

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

One contiguous block: `len | (len | bytes)*`. Iteration is sequential; no
allocations happened anywhere.

### A.2 A compact tree with relative references

```goose
enum Sexp {
    Sym  { name: u8[varint] },
    List { kids: (Sexp..&<varint>)[varint] },  // tiny self-relative links
}
var pool: Sexp..[>..] = [];                    // grow-only pool of variable ADTs
```

Children are built first (pushed earlier into `pool`), parents store tiny
backward offsets: `(a (b c d))` ≈ 21 bytes total, serializable by memcpy.

### A.3 A growable hash table (open addressing), in-model

```goose
struct Map { slots: Slot[>..] }           // Slot fixed-size; grow-only
```

Growth: `slots.append(…)` doubles in place (a bump, no move), then rehashes
in place by cycle-walking; or rebuild into a fresh local and copy back via
whole-resizable assignment (§4.4). Deletions use tombstones or the
`reusable` mechanism. No allocator, no reference invalidation during lookup.

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

---

## Appendix B. TODO / open items

Collected from the design discussion; each needs future resolution work.
The newest, highest-priority items first:

0h. **Passing by size class** — DONE, see §4.1 (and §7.2, §3.8, §6.5):
    fixed values connect to destinations by value; non-fixed lvalues bind
    by reference at reference-typed, untyped and un-annotated destinations
    and are an error at value-typed ones, which take an rvalue or an
    explicit `copy(x)`; a redundant `&` is a warning.
0i. **Resizable tails get their own frame header** — decided, pending
    implementation (`stdlib_design.md` §8.7), likewise before the library.
    A resizable-tailed struct is a frame object of its fixed prefix fields
    plus the tail's `{ base, len }` header, with only the tail's elements on
    the data stack, so `&s.tail` is an ordinary reference and C.2's
    "reference the owning variable" restriction goes away. Variable-size
    prefixes and resizable-class ADTs keep the bytes-on-stack shape behind
    a base pointer.
0j. **Slices of grow-shrink arrays** — DONE, see §5.2 (superseding TODO 4):
    a slice or reference into a `[>..<]` is created like any other and held
    by variables only; a shrink is an error while one is in scope, naming
    it and where it was bound; specializations record what they shrink so
    that calls are checked the same way.

0a. **Mixed-signedness ergonomics in practice** — §6.1's unification
    deliberately rejects `u32 + i32`, and `u64` against anything signed
    outside the comparison rule now in §6.1. Watch whether real code (hash
    kernels, size math against `.len`'s `i64`) still accumulates casts.
    Note this is a `u64`-only problem: `u8`/`u16`/`u32` all widen implicitly
    into `i64` (§6.3), so the same code on a `u32` hash composes already.
    What remains is the *analysis* half rather than the language half: the
    bounds-check pass (§10.5) follows a `u64` value's range through
    `%`, `&` and casts, but drops it the moment the value is bound to a
    `u64` local, because in general a `u64` need not fit `i64`. A `let` has
    one value, so it could carry its initializer's range the way §6.1's
    non-negativity already does — the narrow, checkable version of "track
    that this `u64` fits `i64`". Until then, the open-addressed map in
    `bench/goose/words.goose` keeps the four checks around `slots[idx]`.
0e. **Per-array index types** — an index validated once against a specific
    array, so repeated `a[i]` and indirect `a[b[i]]` need no further check.
    Goose is unusually well placed for this: specializations already carry
    each reference's root (§10.2), and a grow-only `[>..]` shrinks only at an
    operation on the array itself (§5.1), so such an index could only be
    invalidated by one the analysis sees. This
    is the one direction that would take §10.5 past what an LLVM-based
    language can prove, since it turns a dataflow question into a type one.
0f. **Bounds-check analysis, known gaps** (§10.5) — a loop's exit condition
    is a disjunction (`!(i < n && p)`), which a difference-constraint domain
    cannot represent, so post-loop bounds rest on the inferred invariants
    instead; LLVM meets the same wall and works around it by duplicating
    blocks so the conditions never merge, which is available here too. A
    value loaded from an array has no known range, so indirect indexing
    (`dist[out[k]]`, `bench/goose/graph_csr.goose`) keeps its check — see
    0e. Release-mode wrapping (§6.2) is deliberate and stays, so the
    analysis proves absence of wrap explicitly where it needs to.
0b. **Reference address identity** — references are transparent (§3.8), so
    `r1 == r2` compares pointees; there is currently no way to ask whether
    two references alias the same address (a `same_ref(a, b)` builtin?).
0c. **Pointee writes through optionals** — a narrowed optional writes
    through fine, but there is no way to write through an optional without
    narrowing; and rebinding to a plain reference first (`let r: T& = o;`)
    is the only escape hatch. Possibly fine; revisit with usage.
0d. **Lifetime precision, remaining cases** — two checker conservatisms
    still exceed the spec: long-distance returns (§7.9) only carry
    references to globals/static data (precise rule: rooted at or above the
    target's frame), and the recursive-cycle store rule (§7.8) admits only
    globals and pool parameters as roots of stored references — a reference
    to a caller's fixed-size local is still pass-down-only inside a cycle.
    (Container-read writability laundering, one-root-per-reference-variable,
    and the single agreed return root are now deliberate language rules,
    §9.2/§9.5.)
0g. **Recursive results' roots at back edges** — DONE (§7.8, cycle return
    roots): a cycle's return roots are the fixpoint over its returns,
    computed before any body is checked, so a back edge's result carries a
    real root wherever the fixpoint determines one and an outlives-nothing
    root (pass-down-only) where it does not. What remains is the precision of
    the scan itself: it reads returns of `X.push(…)`/`&X[…]`, of reference
    variables, and of calls to uniquely named functions, and gives up on
    anything else (overload sets, function values, nested functions), which
    only ever costs a back-edge result its usability, never soundness.

1. **varint format benchmark** — DONE, see `varint_bench/results.md`:
   ULEB128 adopted (§3.6). Break-even vs the best branchless format sits at
   ~70–75% single-byte values (a cliff, not a slope); above it ULEB wins
   ~3x, below it loses up to ~3x. Revisit only if a per-field format choice
   is ever wanted for unpredictable-length data.
2. **Error propagation sugar** — `return from` is the mechanism; revisit
   whether a convention/sugar layer (a `try`-alike) is wanted once idioms
   emerge.
3. **Move operation for resizables** — assign-and-leave-source-empty, as the
   one sanctioned "move".
4. **`[>..<]` interior-reference relaxation** — DONE, see §5.2: the
   scope-based test of §5.1, plus per-specialization shrink summaries for
   the shrinks it cannot see directly (through a reference, or of a global).
5. **Cycle store rule refinement** (§7.8) — the pass-down-only rule is
   conservative; explore per-activation reasoning.
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
    element width; verify with the particle/sum benchmarks.
11. **FFI** — `extern fn` boundary rules (what types may cross; flat types
    again?).
12. **Open syntax details** — trailing-block parameter form (`it` vs
    `x =>`); `recursive fn` annotation verbosity (how much of the signature/
    roots/destinations must be spelled).
13. **Labels for `break`** — if early-out patterns demand them.
14. **Stdlib math types** — `float3` etc.; which named ops are overloads vs
    generic-over-structural-shape.
15. **Wasm fallback** — index-based reference representation details.
16. **Relative-reference region tracking** — copies of values containing
    *self-relative* references are currently rejected outright (§3.9; the
    `in pool` form already copies); track the region an offset ranges over so
    whole-region copies (and serialization moves) can be proven safe.

---

## Appendix C. Representation and calling convention (normative for the C backend)

### C.1 Data stacks

Each data stack `S` has a control block `{ top: byte* }` — a global for the
main program; threads use an array of control blocks reached from a hidden
per-thread base pointer (§11.2). Allocation is `p = S.top; S.top += size`.
Scope exit restores the watermark recorded at scope entry. No other runtime
state exists.

### C.2 Value layouts

* Scalars: natural little-endian storage of their declared width; `bool` is
  1 byte, 0 or 1. All loads/stores may be unaligned.
* Fixed structs/arrays: packed concatenation in declaration order.
* `T[]` family: length field (of the declared storage type) then elements.
* `T[..k]`: length field (smallest unsigned type fitting `k`), then `k`
  element slots (uninitialized until first written).
* `T[..]`: capacity then length (both `u32` default), then capacity slots.
* ADT fixed mode: tag, then payload area of max size (trailing padding
  uninitialized, never read). ADT variable mode: tag, then the actual
  variant's payload.
* varint: §3.6 (ULEB128; zigzag where signed).
* Reference: one pointer. Optional: 0 = null. Self-relative: signed offset
  of the declared width from the offset field's own address; 0 = null.
  Pool-relative (`in pool`, §3.9): unsigned offset of the declared width,
  `(target − base(pool)) + 1`, so 0 stays null and a `uN` width spans
  2^N − 1 bytes of the pool. `base(pool)` is the global's element region,
  fixed when its stack is reserved before any initializer runs; a function
  that loads or stores such a reference reads it once into a local at entry.
* Slice: `{ data: T*, len: int64 }` (len = element count).

**Resizable values.** A resizable's element region always tops its data
stack (§1.3(2)); where its length lives depends on nesting:

* *Nested* (tail of a struct / ADT payload on a data stack): the length
  field is inline, immediately before the elements (it is below them on the
  stack, so growth never moves it; reserved when construction reaches it,
  backpatched when the count is known).
* *Outermost* (a local, global, or by-value param): the value is represented
  as a header in the owning frame — `{ base: byte*, len: int64 }` — plus the
  element region on the data stack. `&v` yields the header's address (a fat
  reference is that pointer plus the stack identity); with the stack
  identity known statically (or carried as a fat reference / hidden
  argument), push compiles to `*(T*)S.top = e; S.top += size; h.len++`.
  Since only whole variables have headers, a reference to a *nested*
  resizable value (`&s.tail`) is a compile error — reference the owning
  variable. (The current implementation always uses the header form; the
  nested inline-length case above does not arise, because resizable-tailed
  values are themselves resizable-class and so always outermost.)

### C.3 Calling convention

* Fixed-size parameters and returns: native C ABI (structs passed as packed
  C structs by value).
* Nonfixed by-value parameters: caller allocates/constructs on the
  parameter's assigned stack (§4.3), passes the header (base, len — or
  metadata appropriate to the type) by value; callee owns and may grow it if
  it is resizable (its stack identity is part of the specialization or a
  hidden argument).
* Nonfixed return values: the destination stack is part of the callee's
  specialization or a hidden argument, as is the destination request's kind
  (one value vs element run, §7.3); the callee writes data at the
  destination's top and returns the outermost metadata (lengths/counts) in
  registers — in the element-run form it emits raw elements only. Because
  in-flight return data carries no inline header above the caller's live
  data, `v.append(f())` is contiguous by construction.
* Hidden arguments, in order after declared ones: free-variable references
  (§7.5), destination/target stack identities. The `return from`
  discriminant is not in the signature: it is a single thread-local int
  (0 = normal, k = propagate to target k) that is zero except between a
  `return … from` and the catch in its target frame, so an ordinary return
  writes nothing and a call on a propagation path costs a load and a
  never-taken branch.

### C.4 Guard pages and commit order

Construction rules (§4.2) write stacks front-to-back. When an allocation
skips ahead without writing (a large limited-array capacity), the runtime
commits the skipped range explicitly (VirtualAlloc/mprotect) instead of
relying on touch order — Windows-style guard-page auto-commit covers the
ordinary sequential-write case; Linux uses reserve + explicit commit.
Fresh pages arrive zeroed from the OS; reused stack memory does not —
harmless, since uninitialized slots are unreachable (§5.3). A final
unmapped gap after each region turns runaway growth into a safe abort.

---

## Appendix D. Grammar sketch (informative)

```
program     := topdecl*
topdecl     := import | struct | enum | typealias | fndecl | globaldecl
import      := "import" "."? ident ("." ident)* ";"
struct      := "struct" ident generics? "{" fieldlist "}"
enum        := "enum" ident generics? "{" variant ("," variant)* ","? "}"
variant     := ident ( "{" fieldlist "}" )?
fieldlist   := field ("," field)* ","?
field       := "let"? ident ":" type ("=" expr)? | "pad" intlit?
typealias   := "type" ident "=" type ";"
generics    := "<" ident (":" type)? ("," ident (":" type)?)* ">"

fndecl      := "recursive"? ("fn" | "thread_fn") ident generics?
               "(" params? ")" ("->" rettypes)? blockexpr
params      := param ("," param)* ","?
param       := "var"? ident (":" type)?          // untyped => generic
rettypes    := type ("," type)*                  // no parens in declarations
globaldecl  := "reusable"? ("let" | "var") ident (":" type)? "=" expr ";"

type        := prim | ident tyargs? | "(" type ")" | type postfix
prim        := "bool"|"varint"|"i8"|…|"u64"|"f32"|"f64"
             | "fn" ( "(" types? ")" ("->" (type | "(" types ")"))? )?
tyargs      := "<" type ("," type)* ","? ">"
uint        := "u8"|"u16"|"u32"|"u64"|"varint"
postfix     := "[" expr "]"                      // fixed array (const expr)
             | "[" uint? "]"                     // variable array
             | "[" ".." expr? "]"                // limited (const expr)
             | "[" ">" ".." "<"? "]"             // resizable
             | "[" ":" "]"                       // slice
             | "&" ("<" uint ("in" ident)? ">")?  // reference / relative
             | "?"                               // optional (any type)
             | ".."                              // variable-mode ADT
             | "." ident                         // variant type

stmt        := decl | assign | incdec | exprstmt
decl        := "reusable"? ("let" | "var") identlist (":" type)?
               ("=" exprlist)? ";"
assign      := lvalue assignop expr ";"          // = .= += -= *= /= %= &= |= ^=
incdec      := lvalue ("++" | "--") ";"
exprstmt    := expr ";"

expr        := control | binary
control     := ifexpr | matchexpr | blockexpr | loops | jumps | guardstmt
ifexpr      := "if" expr blockexpr ("else" (ifexpr | blockexpr))?
guardstmt   := "guard" expr ("else" blockexpr | ";")
loops       := ("while" expr | "for" forbind "in" iter | "loop") blockexpr
forbind     := "&"? ident ("," ident)?
iter        := expr | expr ".." expr
jumps       := "return" exprlist? ("from" ident)? | "break" expr? | "continue"
matchexpr   := "match" expr "{" arm ("," arm)* ","? "}"
arm         := pattern "=>" expr
pattern     := ident ("&"? ident)? | "-"? intlit (".." "-"? intlit)? | "_"
blockexpr   := "{" stmt* expr? "}"

binary      := (precedence climbing; tightest → loosest)
   postfixe := primary ( "(" args ")" trailingblock?
             | "[" sliceargs "]" | "." ident | "as" "!"? type )*
   unary    := ("-" | "!" | "~" | "&") unary
   levels   := * / %  →  + -  →  << >>  →  < <= > >=  →  == !=
             →  &  →  ^  →  |  →  &&  →  ||
primary     := literal | ident | "null" | "self" | "(" expr ")"
             | arraylit | structlit | genericcall
genericcall := ident tyargs "(" args ")" trailingblock?
trailingblock := "{" (params "=>")? stmt* expr? "}"
sliceargs   := expr | bound? ".." bound?         // bound := "^"? expr
args        := (expr ("," expr)* ","?)?
arraylit    := "[" (exprlist ","? | expr ";" expr)? "]"
structlit   := (ident tyargs? | varianttype) "{" (fieldinit ("," fieldinit)* ","?)? "}"
fieldinit   := (ident ":")? expr
```

Known parse notes: `structlit` vs `trailingblock` vs `blockexpr` ambiguity
is resolved as in Rust (no struct literals or trailing blocks directly in
`if`/`while`/`for`/`match`/`guard` scrutinee position); a statement that is
a block-ended construct needs no `;` and ends at its `}` (§2); `f<`
commits to a type argument list only when followed by `(`, `{`, or
`.ident {` (§7.7), otherwise `<` is comparison; `T&<u8>` tokenizes as `&`
`<` in type context only; `a..b` ranges exist only in `for` headers, slice
brackets, and match arms; multiple return types in a function *type* (not a
declaration header) require parens around the list; `T&?` parses to the
same type as `T?`, and `?` on an already-optional type is an error.
