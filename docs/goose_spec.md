# The Goose Language — Specification (v4 draft)

This working specification defines Goose's syntax, semantics, and compilation
model. It provides the detail needed to implement the language, without the
formal structure of an ISO specification. Appendix B lists unresolved items.

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

The syntax follows C and Rust: `{}` blocks, `//` and `/* */` comments (block comments
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
construct that produces a value on no path — an else-less `if`, a `guard`, or
a block or scope ending in one of those — is a statement rather than the
block's value, so a function whose result type is inferred, and a function
value's body, may end in one and produce nothing.

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
One prefix, `const`, qualifies the first reference or slice built on the
base type: `const T&` and `const T[:]` are a reference and a slice through
which the pointee, or the elements, cannot be written (§9.5); `const
(u8[:])&` parenthesizes to reach an outer one.

`ns::name` names a declaration of namespace `ns` (§11.1) wherever a
declaration can be named -- a type, a call, a global, a `return … from`
target, a pool; `::name` names a global one explicitly. The `::` token is
distinct from `.`, so `image::Shape.Circle` is the variant of a namespaced
enum.

Evaluation order is left-to-right everywhere (operands, arguments, field
initializers — which named struct literals keep aligned with construction
order by requiring declaration order, §4.2). Overlapping copies have
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
  operands' exact type and width (§6.2) — a loop over `i32` data runs in
  32-bit registers end to end, which is what lets backends vectorize it.
  `i64` is the conventional default for indices, sizes, and counts (it is
  what `.len`, integer literals, and the builtins produce); the other integer widths
  are useful for compact data and kernels that require a particular width.
* `varint` — variable-length integer **storage type** (§3.6), the one
  integer spelling restricted to fields and array elements. Reads decode to
  `i64`; writable only at construction.
* `f32 f64` — IEEE floats, likewise usable everywhere. `f32` arithmetic
  stays 32-bit; `f32` widens to `f64` implicitly (§6.3), never the reverse.
  Float literals adapt to either type; an integer literal never adapts to a
  float type (`let x: f64 = 2;` is an error asking for `2.0`), so a float
  expression reads as one.
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
* A field may declare a default value (`c: i64 = 0` above); constructors may
  then omit it (§4.2). Defaults resolve in the declaration's namespace and
  generic environment, can name globals, and cannot name the constructor's
  locals or sibling fields. A used default executes at each construction,
  in field order among explicit initializers; it is not a cached value.
  Its effects obey the same rules as an explicit initializer's.
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
  element is not findable).

Growth operations (`push`, `append`, …) exist only on resizable arrays and on
limited arrays (`[..k]`, `[..]`) up to capacity (exceeding capacity aborts).
Shrink operations (`pop`, `resize` downward, `clear`) exist on limited
arrays anywhere, on `[>..<]` wherever no live variable refers into it
(§5.2), and on a grow-only `[>..]` exactly where the compiler can see that
no reference or slice into it is live (§5.1); they
abort when they would shrink below empty (`pop` on an empty array, `resize`
to a negative length).

Built-in members: `.len` (always, returns `i64`), `.cap` (limited arrays),
`.push(v)` (returns a reference to the new element on resizable and limited
arrays — the idiomatic way to link up just-built data),
`.append(src)` (src an array/slice of the element type, whose elements are
copied: the references they hold must outlive the array, §9.2, and none may
be self-relative, §3.9; an array literal there is built instead as a run of
such elements, §4.2), `.pop()`, `.resize(n, v)` (grow with fill value `v`,
copied into every slot added, or shrink), `.resize(n)` (shrink only),
`.clear()` per the rules above, and `.index_of(r) -> i64` (fixed,
limited and resizable arrays of fixed-size elements): the index of the
element `r` refers to, `(addr − base) / elemsize`. `r` must be rooted at the
array *exactly* (§9.2), so the division is exact and the result is in range without a runtime check.
A reference rooted elsewhere is a compile error. Growth always supplies element values — no
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
element `u8`: `u8[]` (flat string with a fixed length), `u8[varint]` (compact), `u8[>..]`
(string builder), etc. String literals are static constant `u8` data,
implicitly copyable into any of these representations and usable directly
as slices, of type `const u8[:]` (§9.5): read-only through the type, so a
literal is stored only in a slot declared `const`. No encoding is enforced;
UTF-8 is a library-level convention.

**Text.** Every scalar, `bool`, and `u8` array or slice has a text form:
integers in decimal, floats in the shortest form that reads back to the same
value, `true`/`false`, and a `u8` array's bytes as they are. Three builtins
produce it, each taking any number of arguments and inserting nothing
between them: `print(a, b, …)` writes the forms to standard output followed
by a newline, as one write, so lines printed by different threads (§11)
never interleave; `format(out, a, b, …)` appends them to `out`, any growable
`u8` array; `str(a, b, …)` builds a fresh `u8[>..]` of them, constructed at
its destination like any resizable result (§7.3), so `words.push(str("item",
i))` writes straight into the element. A character literal is an integer
(§2), so `str('x')` is `"120"`; a byte is appended with `push`.

Every other value type renders structurally, so `print(v)` shows any value:
an array or slice of other elements as `[1, 2, 3]`, a struct or ADT variant
as its positional literal with the type name (`vec3<f32> { 1, 2, 3 }`,
`Circle { 1.5 }`, a payload-less variant as `Shape.Dot`), a reference as
its pointee and a null optional as `null`; a `u8` array nested inside an
aggregate is quoted and escaped. A user overload `fn format(out: u8[>..]&,
v: T)` (taking `T` by value or by reference) renders a `T` through itself
instead, wherever a `T` occurs in an argument, so a type can choose its own
text once for all three builtins. The overload is looked up in the
namespace `T` is declared in, then globally (§11.1): rendering follows the
type, not the namespace of the code printing it.

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
  writability (§9.5) and the target's own rules. A `const T&` does not allow
  writes to its pointee.
* Rebinding: the special assignment `r .= &x` updates the reference *value*
  itself, rather than writing through it. `.=` applies to any
  reference-typed location — variables (subject to their `let`/`var`),
  fields, elements. On non-reference locations `.=` is an error. The
  declaration form `let r .= e;` / `var r .= e;` binds `r` to `e` by
  reference whatever `e` is — an lvalue of any size class (`var cur .=
  pool[0];` names the element, no `&`), a reference-returning call (`let e
  .= pool.push(v);`), a narrowed optional — and is how a reference variable
  is declared without spelling its type: the `.=` says the binding is a
  reference, exactly as the rebind does. `let` on a reference variable says
  it does not rebind, not that its pointee is const: `let r .= xs[i]; r =
  5;` writes the element (§9.5).
* Identity: `r1 .== r2` and `r1 .!= r2` compare the references themselves —
  whether they have the same address (`==` compares the pointees). Both operands
  are references to one pointee type, or `null` for an optional; storage (a
  variable, field or element) is taken by reference, as `.=` takes it, so `n .==
  pool[head]` asks whether `n` names that element.
* Binding contexts keep the reference rather than loading through it: an
  initializer/argument/field whose *declared type* is a reference type binds
  the reference value, and binds an lvalue of the pointee type by reference
  without `&` (§4.1; writing the `&` warns). Where a *variable's* type is
  inferred (`let x = r;`), a reference to a fixed-size value decays to a
  pointee copy — except an explicit `&lvalue` initializer, which infers the
  reference type (but not a construct choosing one, such as
  `if c { &a } else { &b }`, whose value is a copy, §4.1) — while a
  non-fixed lvalue binds by reference (`let w = words[0];` names the
  element). To bind a reference-returning call, use `.=`
  (`let e .= pool.push(v);`) or annotate (`let e: T& = pool.push(v);`).
  An untyped parameter is an
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
narrowing via `if`/`while`/`guard`/`assert(r)`/`== null`/`!= null` tests.
Flow typing: inside the guarded region the optional *is* a `T&` in every
respect — transparent, writable through, bound by `.=` (`if n { let m .= n;
… }`) — and a `while` condition narrows its body the same way even when the
body rebinds the variable, since the test runs again before each iteration
(`while n { …; n .= n.next; }`). Elsewhere narrowing is killed by rebinding
the variable. `o .= &x` / `o .= null` rebind an optional; a rebind to a
plain reference narrows it, a rebind to anything possibly null un-narrows
it.

**What references may point to.** Anything except the interior of a
fixed-mode ADT payload (§3.5). A reference into a grow-shrink array `[>..<]`
lives in a variable only and must no longer be live when the array next
shrinks (§5.2).

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

Both forms have the optional spelling `T&<u8>?`, which reserves offset 0
for null. A pool-relative offset is biased by one. A self-relative field
can share its address with an enclosing value, so a non-null store must
not encode as the optional's null: such a store aborts, while a statically
known collision may be rejected during compilation. A non-optional
self-relative reference has no null sentinel: offset 0 denotes its field
address, as in `self` in a value's first field. Null is
therefore representable in any relative location whatever the location's
root: a value known to be null — the literal, or a reference whose root is
static data, which nothing but null has — stores into an optional relative
slot without a root check of its own. So a sentinel-ended chain does not
force plain links on the whole structure.

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
  an lvalue, a by-value argument, a by-value match binder, an element copy,
  an `append` of anything but an array literal, a `resize` fill value, which
  is copied into every slot it adds even when it is a literal)
  is a compile error: the copied offsets would still be measured from the
  source location. Construct such values in place (literals), and bind their
  match payloads by reference. (TODO 16: track the region a relative
  reference ranges over, so provably whole-region copies can be allowed.)
* Because they are position-independent, structures linked by self-relative
  references are trivially serializable / mappable: `to_bytes(a)` writes an
  array's image out, `bytes_of(a)` views it without copying, and
  `from_bytes<T[>..]>(bytes)` verifies one back in (§12,
  `design/serialization.md`). Verifying is what keeps the arriving bytes from
  being the one place a reference could enter the program unproven (§9.4),
  and is also why a `bytes_of` view is never writable: bytes written through
  it would be links the checker never proved.

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
  on where it is stored. This permits links between arrays: a slot array can
  hold 4-byte links into the pool. Since the pool is global, its lifetime
  satisfies the §9.2 store rule.
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
*inside* `pool` has — a `push` or `append`, an `alloc_index`/`alloc_ref`, or
an element store into it; anywhere else it is a compile error.
This allows a structure to use non-optional links throughout. A sentinel
eliminates the need for null links, and loading a non-optional relative
reference requires only an addition, without a null test.

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
participate fully in the lifetime system (§9), which prevents dangling
slices. They are
the intended *read* path; mutation idiomatically goes through references —
but writes through a slice are legal when its provenance is writable (§9.5).

* Created by slicing any array-family value or slice: `a[x..y]` (x inclusive,
  y exclusive; omit for 0 / len; `^k` means "len − k"), or allocated from a
  slice pool (§5.4). At call sites, an
  array argument passed where a slice parameter is expected implicitly
  becomes a whole-array slice; an exact-type overload wins over this
  coercion. (An array literal of variable-size elements is not such an
  argument: it has no temporary to be sliced, §4.2.)
* Slices of fixed-element arrays index and iterate, bounds-checked against
  the slice's own length. Slices never grow; shrinking a slice (re-slicing)
  is always safe.
* Slices of variable-element arrays iterate only (no indexing); the count is
  an element count.
* Slices obey the same restrictions as interior references (§3.8): a slice
  of a grow-shrink array is held only by variables and must no longer be
  live when the array next shrinks (§5.2).
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
*fixed* value is spelled at an untyped destination (`let r = &n;`, or
equivalently `let r .= n;`, §3.8; `f(&n)` into an untyped parameter);
wherever the destination's own type is
a reference it is redundant, and a redundant `&` is a warning.

A reference to a fixed-size value that an `if`, `match`, `block`, `loop`
or bare `{ }` branch gives, `&x` included, is copied as its pointee where
the construct has no destination type, as it is wherever a
reference meets a value (§3.8): `let r = if c { &a } else { &b };` makes `r`
a copy of `a` or `b`, so the `&`s are redundant and warn, while
`let r: i64& = if c { a } else { b };` binds `a` or `b` itself.

There is no ownership transfer beyond the return move, no destructors, no
`Drop`, no reference counting. Deallocation is exclusively scope exit
resetting stack pointers. (A `move` operation for resizable arrays —
assign + leave source empty — is anticipated but not in v1.)

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
required by invariant §1.3(4). Construction of
a limited array writes only its metadata and any provided elements; the
remaining capacity is reserved but **uninitialized** — this is safe because
no read path to uninitialized slots exists (§5.3), and cheap because the
runtime commits skipped address ranges explicitly (Appendix C.4).

While a value is built in place in an array — an element `push`ed into or
allocated in it that is variable-size or holds relative references, a call's
array result `append`ed to it, an `append`ed array literal of such elements,
or the new contents of a whole assignment (§4.4) — nothing may grow that
array, neither the expression being built nor a function it calls
(§1.3(4)). The compiler rejects a growth it cannot show to be of a
different array. A whole assignment builds its new contents over the old
ones, so its right-hand side may not use the array at all (§4.4).

Literal forms usable in any construction context:

* array literals `[1, 2, 3]`; `[]` where the element type is known from
  context, or as the whole initializer of a `var` local (`var out = [];`),
  which makes the local a grow-only `T[>..]` whose `T` is fixed by the first
  `push`, `append`, `format` or whole assignment into it — a string literal
  pushed into one makes it a `u8[][>..]` — and must be fixed before the local
  is otherwise used or its scope ends; `[v; n]` fill form with a non-negative
  compile-time element count. `v` is evaluated once, including when `n` is
  zero, and its value is repeated. Relative links retain their target at
  every destination; `self` in a literal refers to each constructed element.
  A literal whose destination names no array type is a `T[k]`, or a `T[]`
  when its elements are not fixed-size (§3.3). A fixed one may also be a
  temporary for a slice parameter, a `for`, `[..]` or `bytes_of` to view; a
  `T[]` one is a variable value and exists only in a construction context,
  so viewing it takes a variable bound to it first. `==` and `!=` compare
  either one with any array or slice of its element type (§4.5), since
  their result holds no view of it. An `append`ed literal is the run it adds:
  the array's element type `T` makes it a `T[k]`, or a `T[]` when `T` is not
  fixed-size, and its elements are constructed as the array's own
  (`bytes.append([1, 2])` for a `u8[>..]`, `[]` adding nothing, a `str()`
  built in its element). Elements that are variable-size or hold relative
  references are built where they stay, so a relative reference among them
  may point into the array; others are evaluated before the run is added,
  as a pushed fixed-size element is;
* struct literals `X { a: 1, b: 2 }` (named) or `X { 1, 2 }` (positional, in
  declaration order; no mixing). Named initializers must also appear in
  declaration order (out-of-order names are a compile error: values construct
  front-to-back, and reordering would obscure either evaluation order or copying
  cost). Fields with declared defaults (§3.2) may be omitted: trailing ones in
  the positional form, any of them in the named form. An omitted optional
  field without a declared default is null; other omitted fields are errors;
* `[..cap]` — an empty limited array `T[..]` with the given construction-time
  capacity (`cap` a runtime expression); the reserved slots stay
  uninitialized (§5.3, C.4). An array or string literal constructing a
  limited array of static capacity must fit it (a compile error otherwise);
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

### 4.3 The in-place construction guarantee

A constructed nonfixed value is built **directly in its receiving
storage**, including through calls and the selected branch of an
`if`/`match`/case-dispatch. Passing through such an expression is not a
reason to materialize a separate result and then copy it into place.

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
  their tail (§3.4): assignable — semantically clear-then-construct; the
  bump pointer resets to the array's element start and the new contents are
  built in (from an rvalue or `copy(x)`, §4.1), so the right-hand side may
  not use the array (below);
* limited arrays `[..k]`/`[..]`: assignable if the new length fits capacity;
* fixed-mode ADT lvalues: assignable, including with a different variant;
* **not** assignable: variable-class lvalues (`T[]` locals/fields/elements,
  variable-mode ADT lvalues, `varint` fields) — these are frozen at
  construction; rebuild the container instead.

The right-hand side of a whole assignment of a resizable runs once the old
contents are gone, while the new ones are built in their place, so it may
not use the array: not name it (its length included), nor a reference to it
or to the value holding it, and neither may a function it calls, through a
global or captured variable that function names. `x = [x[1], x[0]]`,
`x = f(x)` and `g = rebuilt()` for a `rebuilt` that reads `g` are errors. As
with growth (§4.2), so is a use the compiler cannot show to be of a
different array: of a reference parameter a caller may bind to the global
being assigned, say, or through a call into a recursive cycle still being
checked, which counts as using every global and every variable in scope.
Where the assignment names the array as a field (`t.chars = …`), the
right-hand side may still name the holding value's other fields that are
flat (§1.1): `t.chars = render(t.font)`. A slice or reference into the old
elements is the shrink rule's to judge (§5.1): the right-hand side runs
after the shrink the assignment starts with, so one it uses is live there.
New contents computed from the old are built in a variable of their own and
assigned as `copy()` of it, the one copy spelled out where it is paid
(§4.3).

`let` forbids assigning that name or field as a whole, and nothing more:
the *contents* of a `let` array or struct are as writable as their type
says (`let xs = [1, 2, 3]; xs[0] = 9;` is fine), and so is what a reference
to it reaches. Contents are made read-only by the type, `const T` (§9.5),
and `const x = e;` declares a `let` of type `const T`: neither reassigned
nor written into. Definite assignment is enforced: no
reads of uninitialized locals; every declaration either has an initializer
or is provably assigned on all paths before use. Fixed arrays require full
initialization (every slot is indexable); the `[v; n]` fill literal makes
large ones cheap.

### 4.5 Equality and comparison

`==`/`!=` are **structural** and require both operands to have the same
type: scalars and bools by value; structs and fixed arrays memberwise (pad
and ADT padding bytes excluded — semantic comparison is per-member; memcmp
is a valid optimization only for gap-free layouts without floats, since a
float compares by IEEE value wherever it sits: `-0.0 == 0.0`, and NaN is
unequal to itself); array-family values and slices by length then elements
(sequential walk for variable elements) — two operands of *different* array
kinds compare too, as slices, whenever their slices would have one type
(§3.10): a `u8[>..]` field against a string literal, a `u8[..32]` against a
`u8[:]`, with no `[..]` needed; ADTs by tag then payload.

References and slices follow a *top-level rule*: as the direct operands of
`==` they have value-like semantics — a reference compares its pointee
(transparency, §3.8), a slice compares length then elements. As *members* of
a compared composite they compare by identity (the reference address, the
slice's address+length): recursing through them would turn `==` into an
unbounded pointer traversal. Optionals compare as nullable references
(`o == null` is the null test); `.==`/`.!=` compare two references by
address rather than by pointee (§3.8). Ordering `< <= > >=` exists on the numeric
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

Use grow-only arrays for arenas, pools, string builders, tree storage, and
scratch buffers that are refilled or popped between phases.

**When a grow-only array may shrink.** The receiver is the array's variable,
a reference variable or parameter bound to the whole array, or a global —
not an element of a larger value, and not a `reusable` pool (§5.4: its
freelist keeps every slot live). The call must stand on its own: a
statement, the initializer of a declaration, or the right-hand side of an
assignment to a variable, so that no reference taken earlier in the same
expression outlives the shrink; and not inside a block, `if`, `match` or
loop that produces a value, whose enclosing expression may hold such
references too. At the shrink, nothing *live* may refer into the array: no
reference or slice variable rooted at it (a `var` reference the same-depth
rebinding rule (§9.2) could retarget into it counts, as does one not bound
yet further down a loop body), and no value that *holds* a reference into
it — a struct with a reference field, an array of slices, a `let` copy of
an element of such. Values hold references only where a store put them,
and every store the checker has seen is on record (§9.2), so this half of
the test is exact to the store: the error names the holder and the line
where a reference into the array was stored into it. A value whose type
cannot hold a reference to anything the array's elements contain by value
is never a holder, nor is one linked by relative references alone (a node
pool): those point within their own root. A reference to a slice variable
or to a holder counts as what it refers to: while the reference is live,
so is whatever that slice or holder may refer into.

A variable is live at the shrink when it can be read again afterwards:
it is named later in its own block or in an enclosing block it was
declared in, or anywhere in a loop that contains the shrink and that the
variable was declared outside of, since the next iteration runs the rest of
the body again. "Named" is syntactic — any mention, a call of a nested
function that mentions it included — so the test never depends on what
the optimizer proved. A reference whose last use is before the shrink is
dead, and its block need not end: `let w = line[..5]; print(w);
line.clear();` is fine, and a scratch buffer refilled per iteration, or a
stack popped between phases, hands out slices of itself freely — "reusable
scratch" and "structure I can point into" are the same type. The operations
themselves are the grow-shrink ones: a stack-top move and a length store.
Assigning the array whole (`a = …`), or a value holding it, replaces its
elements and is a shrink under the same rule; the right-hand side runs after
it, so a variable that side names is used after the shrink.

Through a reference or of a global, a shrink cannot see the callers'
variables, so it is checked at every level: each function specialization
records which globals, and which parameters' pointees, it shrinks — directly
or through its own callees — and what its calls stored into those pointees,
and a call is checked as if it performed those shrinks and stores in the
caller. The pointees of a parameter that holds references by value (a
struct with a reference field, an array of slices) are what those
references point to: a shrink or a store through one of them is of the
caller's storage, not of the callee's copy of the holder. A global receiver
additionally counts every other global whose type can hold a reference to
something the array can contain as holding one, since its stores may come
from functions not yet checked. A call into a recursive cycle still being
checked counts as shrinking every array a function of the cycle textually
shrinks.

A reference whose root is inexact (§9.2) — a value chosen between arrays,
`if c { a } else { b }`, or one read back out of a container — may point at
any array of its type that the root bounds, and a shrink through it counts
as a shrink of each of them: those in variables at the root's scope or
outside it, and the caller's arrays a parameter's references lead to, for
which the callers check it. The same holds where a call shrinks what an
inexactly rooted argument points at.

Nor can a shrink inside a function tell what its parameters point into: a
parameter's pointee is whatever each call passes, which may be a global, a
variable of an enclosing function, or what another parameter points into —
an inexactly rooted argument may be any array its root bounds. Where a
reference, slice or holder parameter, or a variable bound from one, is still
used after a shrink of a global, of such a variable or of another
parameter's pointee — or a view of a global or of such a variable is still
used after a shrink of a parameter's pointee — the specialization records
the pair, and each call judges it by the arguments it passes: a call that may
make the two one array is an error, as a variable of the caller still used
would be, and a call passing its own parameters on records the pair for its
own callers. A call into a recursive cycle still being checked is judged
again against the pairs the cycle records once the whole cycle is.

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
  a scan of the variables in scope. A literal's fields and elements are
  storage wherever the literal lands, so no struct, variant or array literal
  holds one, even where the literal itself is passed down or returned:
  `f(S { a[..] })` is an error, as `let s = S { a[..] };` is. The rule is
  about references that can point *into* the array: one merely rooted at a
  value that holds one, whose pointee type the array's elements cannot
  contain — a slice key read back out of a dictionary's slots — stores like
  any other.
* **A shrink is an error while any live variable may refer into the array** —
  the test a grow-only shrink applies (§5.1), its liveness rule and its call
  summaries for a shrink through a reference or of a global included, minus the
  store record: references into a grow-shrink array live only in variables, so
  checking those still in use is sufficient, a reference to a slice variable
  among them. The error is at the shrink and
  names the variable and where it was bound, so either end can be changed: use
  the slice for the last time before the shrink, or move the shrink. A call into
  a recursive cycle still being checked counts as shrinking every grow-shrink
  array it can reach, through the references its arguments hold as well.
  Function values run inline, so a shrink inside a block is checked against the
  block's own enclosing scopes.
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
  live value of its type forever. The index must lie in `[0, a.len)`;
  otherwise the operation aborts with the ordinary array-index out-of-bounds
  diagnostic, in every build, before changing the freelist. The index
  expression is evaluated once. Where the code holds references rather
  than indices, `a.free(a.index_of(r))` (§3.3) is what turns one back.

Semantics, not just implementation: *all elements remain valid at all times*.
A reference to a freed-then-reused slot reads a different (same-typed) value —
type-safe reuse, never memory corruption. This is the language's answer to
tree-mutation workloads that would otherwise need an allocator.

**Slice pools.** Declared `reusable[]` (`reusable[] var pool: T[>..] = [];`,
also valid on globals), the array hands out runs of elements instead. Its
hidden freelist holds (index, count) spans sorted by index, and a freed run
merges with the spans it touches:

* `a.alloc_slice(n) -> T[:]` — `n` elements from the front of the first free
  span, in index order, that holds them; failing that, from the end of the
  array, which grows by `n` — less the elements of a free span that reaches
  its end, which the run starts with.
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
those two need an element type that has one. The length is evaluated once, and
a negative one, or one no data stack could hold, aborts before the pool
changes. The slice handed to `free_slice` or `realloc_slice` must be one of the
pool's runs: one the pool handed out, a re-slice of it, or a slice of the pool
itself. Where the checker sees that, because the slice is rooted at the pool
exactly (§9.2), nothing is checked when the call runs. A slice rooted exactly at
another global, or at another variable of the calling function, is a compile
error. Any other slice, such as one read out of storage where runs of other
arrays could be kept as well, is checked when the call runs: unless it is
empty, its elements must lie inside the pool's length starting on an element
boundary, or the call aborts before the pool changes. An empty slice frees
nothing and grows like a new run, wherever it points. A grown slice may be copied, which a value holding
self-relative references cannot survive (§3.9), so `realloc_slice` is a compile
error for such an element type; `in pool` references copy fine, and the other
two operations never move an element. The single-slot operations do not exist
on a slice pool, nor these on a `reusable` one; `push` and `append` add at the
end of either without consulting the freelist.

The guarantee is the same: a slice still naming freed elements reads whatever
their next owner wrote, or the default values an allocation put there. Freeing
a run twice is a logic error of the same kind — the freelist can then hand the
same elements out twice, but never anything outside the array.

---

## 6. Expressions and arithmetic

### 6.1 Operators

C/Rust set: `+ - * / %` (`%` is Euclidean on integers, §6.2; on floats it is
C `fmod`), comparisons, `! && ||`
(short-circuit, `bool` only), bitwise `~ & | ^ << >>` (on integers),
assignment statements `=`, `+=` etc. (the compound form of every binary
operator, shifts included) on assignable lvalues, `.=` (reference
rebinding, §3.8), reference identity `.==`/`.!=` (§3.8), and `++`/`--` as
statements on integer lvalues (no expression form). Precedence is Rust's
(Appendix D): the bitwise operators bind tighter than comparisons, so `x &
mask != 0` is `(x & mask) != 0`, and shifts bind tighter than `&`. Range
expressions `a..b` appear only in slicing brackets, `for` headers, and
match arms.

**Operand unification.** A binary numeric operator's operands must reach
*one common type*, which is also the result type, found as follows: equal
types stand; a constant adapts to the other operand's type (compile error
if its value does not fit); otherwise, if exactly one operand implicitly
widens into the other's type (§6.3), the wider type wins. Anything else —
same-width signed/unsigned, `u64` with anything signed, int with float — is
a compile error asking for a cast. Nothing here invents a type absent from
the expression: `u8 + i64` is an `i64` add, but `u32 + i32` does not
become 64-bit arithmetic implicitly. Exceptions: shifts take the *left* operand's
type as the result (the count is any integer type, masked per §6.2), and
`==`/`!=`/orderings unify the same way but produce `bool`. Unary `-`
requires a signed (or float) operand; `~` any integer, keeping its type.

**Comparisons and `u64`.** A comparison produces `bool`, so it does not need a common numeric result
type. However, comparing signed and unsigned operands must preserve their
values. `u64` is the only unsigned type with no signed supertype
(`u8`–`u32` widen into `i64`, §6.3). A comparison between `u64` and a signed type
is therefore allowed **when the signed operand is known non-negative**: it
converts to `u64` without changing value, and the comparison is a single
unsigned one — no wider than either operand, and never a hidden branch.

"Known non-negative" is deliberately a *syntactic* property, not an inferred
one: a non-negative integer literal, a `.len` or `.cap` (non-negative by
§10.4), or a `let` bound to one of those. Whether a comparison compiles thus
depends only on what is written, never on how much the optimizer managed to
prove. Other cases require an explicit cast because conversion may change the
value. A cast does not, however, preserve every comparison: `x as! i64`
on a `u64` above `i64.max` silently compares as negative, and the checked
`x as i64` aborts in debug on a value that was perfectly legitimate to
compare. When the rule permits it, use the direct comparison to preserve the values
without conversion overhead.

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
  any signed type; `f64 → f32`; and int ↔ float in either direction — an
  integer *literal* included: `2.0`, not `2`, where a float is expected.
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
{ s }                        // a plain scope with a value; not a break target
guard c else { s }           // s must diverge; after the guard, c holds
guard c;                     // shorthand: exit the innermost valueless construct
match e { ... }              // §8
return e? (from f)?          // §7.3, §7.9
break e?                     // exits innermost loop/block, optionally with value
continue
```

`block { }` exists to promote early-out style anywhere, not just at function
top level. `break` binds to the innermost `loop`/`while`/`for`/`block`;
labels are not in v1. All `break E` of one construct must agree on E's
type; as at any destination (§3.1), an integer literal in one arm adapts to
the other arm's type (`if c { x } else { 0 }` has `x`'s type).
A bare `{ … }` in expression position — a match arm of several statements,
say — is only a scope: `break` inside it still leaves the enclosing loop.

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
  §6.1 and `i` runs at that type (an `i32` range gives a 32-bit loop variable).
* `for i in n` — sugar for `0..n`; `i` has `n`'s type.
* `for x in arr` — element copies for fixed-size elements, at the element's
  type; element references for non-fixed ones (§4.1), whose walk is
  sequential. A copy is not written (`x.f = 1` is an error naming `&x`):
  the write would update the copy and nothing else. An element that *is* a
  relative reference (§3.9) binds as the
  loaded plain reference, exactly as indexing it gives; an element with
  relative references *inside* it cannot be copied out of its root at all,
  and needs the `&x` form.
* `for &x in arr` — element references (the mutation form for fixed-size
  elements; redundant, and a warning, for non-fixed ones). Over a `[>..<]`,
  the binding is a reference in scope: no shrink inside the loop (§5.2).
* `for x, i in arr` / `for &x, i in arr` — with index (`i: i64`).

All array-family types and slices are iterable. Custom access patterns are
provided by HOFs taking static function values (§7.6), which compile to
plain loops.

Range/count bounds are evaluated once, before the first iteration; an
empty or reversed range performs no iterations. Array traversal tests the
current length before each iteration, so permitted growth can add elements
to the traversal. A slice keeps its own length: growing its source does
not extend that view. `continue` advances to the next index or sequential
element just as reaching the end of the body does.

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
* Parameters are `let` bindings by default (not reassigned; their contents
  are as writable as their type says, §4.4); `var` makes the (by-value)
  parameter a reassignable local.
* A parameter with no type annotation is generic (Lobster-style); explicit
  `<T>` parameters express same-type constraints and let signatures name
  types. Return types may be omitted where inferrable (required across
  recursive cycles, §7.8).
* Overloading by parameter types is allowed; resolution is static: the
  unique concrete exact match wins, then a generic exact match, then a
  match requiring coercion (array→slice §3.10, literal fit §3.1, implicit
  widening §6.3). A candidate's rank is its worst argument rank; ties are
  errors, without further specificity or declaration-order tiebreaking.
  Only if no ordinary candidate matches is tag dispatch (§8.2) tried.
  The expected result type neither selects an overload nor binds generics.
* Multiple return values: `fn f() -> A, B`; received as `let a, b = f();`.
  There is no tuple *type* — multiple returns are a calling convention;
  structs are the way to keep data together. (Function *types* with
  multiple returns need parens — `fn(i64) -> (A, B)` — to disambiguate the
  comma; declaration headers don't.) Nonfixed types are allowed in any
  return position; each nonfixed result gets its own destination per §7.3
  (possibly distinct stacks).
  An ordinary value context takes only the first result; a call statement
  discards them all. A multi-name binding requires exactly that many
  results. `return f()` forwards all of a multi-result call's results,
  adapting each to its corresponding declared return type.

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
metadata (lengths) outside the element run. Where a result is first built
with its packed value header, the header is removed in place under §4.3's
relocation exception (Appendix C.3):

* `let x = f();` — a fresh stack region (statically assigned to `x`).
* `v.push(f());` / `v.append(f());` — the top of `v`'s stack; the push is a
  no-op on return beyond `v.len` adjustment (the data is already in place).
* `g(f())` — the argument slot of `g` (its statically reserved stack).
* `x = f();` (resizable `x`) — `x`'s stack, replacing its contents, which
  `f` may therefore not use (§4.4).

**Named results (guaranteed NRVO).** When every `return` of a nonfixed
result returns the same local variable (and those returns are its last
uses), that local is allocated at the return destination from its
declaration — `return x` then costs exactly the same as returning the
constructing expression directly. When different locals are returned on
different paths, only one can live at the destination and the others are
copied on return. This is in addition to §4.3's layout relocation.

**Exits.** A `return` or `break` taken while its destination already holds
part of a value -- inside an element of a literal being built there, say,
or, for a `return from` (§7.9), below a named result built in the
destination the target handed down -- abandons that part. Its own value,
constructed behind it, is moved down over it: a copy only such an exit
costs.

**Destination requests.** The construct-here information handed to a callee
(or any constructing expression) comes in one of two forms, chosen by the
receiver: *one value of type R* (receivers that store a value: `let`,
params, `=`, `push` of one element — R's nested metadata, if any, is part of
the constructed bytes), or *a run of elements of type T* (receivers that
splice: `append`) — the callee emits raw elements and returns the count.
The request kind is part of the specialization signature (§10.2); the same
function may be compiled in both forms for different callers. A callee that
merely constructs its result passes the destination through. A callee that
must first operate on its result as a whole array may make one copy into
the requested element run, just before returning. Where its packed value
can occupy the receiving storage, removing the header is instead the
layout relocation of §4.3 and may be performed by the callee or receiver.
A local resizable can use metadata outside the element region
(Appendix C.2), with its elements already in place.

Consequence: returning a built-up value and out-parameter style are the same
cost, and building a variable element "inside" a container is idiomatically a
function call in argument/push position.

### 7.4 Growing through a reference

Passing `&v` where `v` is resizable lets the callee push/append: per call
site the compiler specializes on the identity of the stack `v` tops (§10.2),
or passes it as a hidden argument / fat reference (Appendix E) when one body serves
multiple stacks. No surface syntax distinguishes these.

An in-scope "current pool" mechanism (allocation without naming the array,
compile-time bound to whichever suitable array is in scope) is deferred
(TODO).

### 7.5 Nested functions and free variables

Functions may be declared inside functions. A nested function may read and
write the enclosing function's locals ("free variables"), subject to those
variables' normal rules (writability §9.5, roots §9.2, shrink rules §5).
A function value's body (§7.6) encloses what is written in it the same way:
a function declared there, or a function value written there, may also name
the value's parameters and the body's locals.

Names in a nested function resolve where it is declared, whatever surrounds
a call of it: a free variable is the one in scope at the declaration, not
one of the same name that a scope around the call declares, or that is
declared further on. The functions it calls are those in scope there and
every function declared in the blocks around its declaration, before or
after it, so nested functions may call each other in either order, as a
recursive descent parser's do; calling one of them before the enclosing
code has reached its declaration is an error. That code names a nested
function from its declaration to the end of its scope. It may pass the
name directly as a static function argument (§7.6).

Implementation model: free variables become hidden reference parameters of
the nested function. A nested function's value never outlives the function
declaring it (function values do not escape, §7.6), and a call where a
variable it names is out of scope is rejected, so every call can pass them.
When a nested function is passed as a static function value and inlined into
its HOF — the expected, common case — the hidden parameters disappear
entirely; un-inlined builds (debug) keep them as real arguments.

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

A function argument must be a function name (including a bound generic
function parameter) or a block literal. Runtime expressions producing a
function value, such as a call, `if`, `match`, `block`, or `loop`, are
rejected. Evaluate runtime work in ordinary statements before the call;
use locals when its ordering with other arguments matters.

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
failing instantiation. Every instantiation the program reaches is checked, so
there must be finitely many: a recursive call may not keep making new ones
(§7.8).

Type variables are never checked abstractly. A generic body is checked only
at an instantiation where every type is concrete — including the result of
every call it makes on a function-value parameter, since that value's body
is checked inline against the concrete argument types at that point (§7.6).
So a HOF never needs to state what its function value returns: `map`'s
result element type is the type of `F(x)` in that instantiation, even when
the block is `{ generic(it) }` and that type depends on the instantiation.

**Call-site type arguments.** Type arguments are inferred from the argument
types whenever they appear in the parameter list: `fn foo<T>(x: T)` can
be called as `foo(1)` without an explicit type argument. The typechecker
must support inference for both `<T>`-style and untyped (implicitly generic)
parameters. Where
several arguments mention one type variable, the typed ones bind it and a
literal then adapts (`max(n, 0)` with `n: u32` is the `u32` instantiation),
so an `i64` literal never fixes the type by coming first. An
explicit list `f<i64>(x)` is allowed; it is *needed* when no argument
mentions the parameter (e.g. `qget<i64>()`, or `zero<f64>()` for
`fn zero<T>() -> T`); it binds the leading type parameters in order, and
the rest are inferred. Syntactically, `f<` commits to
a type argument list only when the `<…>` is immediately followed by `(`,
by `{` for a struct literal (`Pair<i64> { … }`), or by `.ident {` for a
variant literal (`Opt<i64>.Some { … }`); otherwise `<` is the comparison
operator.

**Literal arguments.** Where a literal is the only thing binding a type
variable — the parameter is untyped, or its type is a bare `T` no typed
argument mentions — the variable takes the literal's default type (§3.1),
and inside the specialization the parameter is a *literal parameter*: a
constant of unknown value that adapts wherever the bare parameter meets a
type, as the literal would have, so `push_n(flags, 1, n)` with `fn
push_n<A, T>(xs: A&, v: T, n: i64)` pushes a `u8` into a `u8[>..]`, and
`fill(small, -7, 2)` an `i16` into an `i16[..4]`, with no suffix or cast
at the call. The value is not part of the specialization: every call with
a literal shares one body, checked once with the value unknown, and the
body records each type the parameter adapted to (in itself, in a callee
it passed the parameter on to as a literal, and in a block it handed it
to). Each call's literal is then checked against every recorded type —
`push_n(flags, 300, n)` is an error naming the `push` that takes a `u8` —
once the whole program is checked and the records are complete. What the
body cannot do with an unknown value it cannot do with a literal
parameter: `[0; n]` needs a constant, and `v + 1` is an ordinary `i64`
expression, exactly as `let y = v;` commits `y` to `i64`, so a recursive
call on `v + 1` passes a typed value and terminates. `var` parameters and
`extern` functions take literals as ordinary values.

Passing arrays by reference is the generic way to write mutating range
algorithms (each array-family type instantiates its own copy); slices are
the uniform non-mutating way.

### 7.8 Recursion

Non-recursive call graphs are the default and require nothing. Recursion is
opt-in and annotated: the entry function of every recursive cycle is marked
`recursive fn` (the keyword alone: roots and destinations follow from the
entry call as for any specialization, §7.7), all functions in the cycle need
fully explicit signatures (no inference across the cycle back-edge), and —
the key restriction — **no function in the cycle may
declare locals requiring a new data-stack assignment**. Growable data used
by recursive code must be owned outside the cycle and passed in (references,
slices, reusable pools). The compiler checks this in call-graph order;
recursion depth then only consumes native call stack. Unnamed nonfixed
*temporaries* (e.g. an intermediate call result) are exempt: they cannot be
referred to across activations, so the soundness argument holds — but an
implementation may then consume data-stack slots proportional to recursion
depth for them (aborting past its limit).

**Polymorphic recursion.** A recursive call may instantiate its callee with
other type arguments than those of the call it sits in (§7.7): `flip<A, B>`
calling `flip<B, A>` is back at `flip<A, B>` one round later, and that call
is the cycle's back edge. The instantiations a recursion reaches must be
finite, though. Every branch of every specialization is checked, whatever
values reach it at run time, so a call whose types grow with each round —
`g(a, n - 1)` with `let a: T[1] = [x]` inside `g<T>`, or `grow<T[2]>(n - 1)`
inside `grow<T>` — would instantiate without end, even where `n` bounds the
recursion at run time. The compiler rejects such a call once it would put
more than 16 specializations of one function in progress on one compile-time
call path, the way C++ bounds template instantiation depth, and reports it
with the chain of instantiations that led there. A finite recursion has one
specialization in progress per instantiation on its cycle, or per level of a
nested type it descends through, so only an unusually deep one meets the
bound.

**Cycle store rule.** Within a recursive cycle, distinct activations of the
same local are statically indistinguishable, so the §9.2 depth check is not
sufficient there. Therefore: a reference whose root is a local of a cycle
function may be passed *down* as an argument, but may not be stored into any
location, nor returned. Rebinding one of the activation's own reference
variables (`cur .= cur.next`) is not such a store: the variable dies with
the activation, and what it is bound to came from one that outlives it.
References rooted outside the cycle are
unrestricted — in particular a pool handed to the cycle by reference (a
parameter whose pointee is resizable-class, which no cycle function can own),
so a recursive builder can push into a caller's local pool and link what it
pushed. A local of an enclosing function outside the cycle — a free variable
(§7.5) of a nested `recursive fn` — outlives every activation the same way,
so a parser keeps its pool, its key table and its input as locals of a
non-recursive `parse`, and the nested recursive functions store, link and
return references rooted at them freely. Because the cycle's functions are
checked once against the entry
call's roots, every recursive call must pass such a pool by the same
reference the entry call did: swapping two pools, or passing a different one,
at a back edge is a compile error. (Further refinements are future work,
TODO.)

**Cycle return roots.** A back edge reaches a function whose own returns may
not have been checked yet, so the root of its result cannot come from them.
Instead, **the return roots of a cycle are the fixpoint over the returns of
the functions in it**, computed before any of their bodies are checked: a
return of `X.push(…)`, `X.alloc_ref(…)` or `&X[…]` gives the root of `X` (a
global, a free variable, or a parameter, whose root each specialization
already has from its call site); a return of a reference variable gives the
root of what it was
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
returning it is a compile error. It may point into a grow-shrink array, too,
so what it is passed down to holds it the way §5.2 holds references into one,
in variables only: not in a literal, either.

### 7.9 `return … from` (long-distance return)

```goose
return E from f     // f = a named function on every compile-time call path
                    //     from f to this statement
```

`f` is resolved where the returning function is written -- a nested function
in scope, else the name's overload set by the rules of §11.1 (`from
image::parse` is explicit) -- and the target is the innermost enclosing call
of a function in that set. An unrelated function that merely shares the leaf
name never catches the return.

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
extern "sqrtf" fn sqrt(x: f32) -> f32;
extern "gs_os_read_file" fn read_file(path: u8[:], out: u8[>..]&) -> bool;
extern fn gl_vertex3(v: float3&);
```

A declaration without a body binds a Goose signature to a C function: the
optional string is the C symbol (default: the Goose name). It is top-level
only, fully typed, not generic, and joins overload resolution like any
function. A call compiles to a direct C call with no calling-convention
extras, and a prototype is emitted from the declaration unless the runtime
defines the symbol (the `os` library's `gs_os_*` functions, in
`src/runtime/runtime_os.h`). User C reaches the program through
`--include <header>`, emitted after the generated type declarations so a
header can implement shims against the generated typedefs. Every name that
comes from the program appears in the C with a `_g` suffix -- a struct `Stats`
with a field `lo` is `Stats_g` with `lo_g` -- which is what keeps the generated
file clear of the C keywords and of whatever the platform's headers declare,
without depending on a list of names to avoid. A namespaced declaration
`ns::x` (§11.1) is `ns_x_g` followed by the namespace's length --
`image::Pixel` is `image_Pixel_g5` -- so `a::b_c` and `a_b::c` stay apart and
no namespaced name coincides with a global one. An `extern fn`'s own symbol is
the exception: that is the C name the declaration gives, unchanged.

What crosses (layouts per C.2): the integer and float scalars and `bool`;
any flat fixed-size value (including structs, fixed and static-capacity
limited arrays, and fixed-mode ADTs), by value as its packed C type;
`T&` to such a `T`, as a pointer; `T[:]` of such a `T`, as the slice struct
(`{ data, len }`); and `u8[>..]&`, as the resizable's header plus its stack
(`gs_rref`), which C appends to through `gs_bld_append(ref, p, n)`.
Returns are one scalar or flat fixed-size value by value, or nothing;
references, slices and builders are parameter-only forms. Everything
else — varints, references inside structs, variable and resizable values
by value, slices of variable elements, optionals — is rejected at the declaration.
A parameter of reference or slice type is what it says (§9.5): a read-only
argument needs it declared `const`.

C must preserve the types and lifetimes of borrowed storage, must not
retain a borrowed argument beyond the call, and may only append to a
builder. The compiler assumes an extern may write through its arguments
but does not mutate unrelated Goose globals; C code is responsible for
honoring those obligations.

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
  a large one for variable-size payloads, and not written, as a `for` copy
  is not (§6.5) — and `Circle &c =>` binds it *by reference* (the `for &x`
  spelling; `Circle& c` is the same tokens).
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

Every reference/slice value has a static **root**: a local or global
variable, or a temporary (below), that bounds the scope its target lives in.
The root is **exact** when that variable's own storage contains the target,
and inexact when it only bounds the target's lifetime — the owner is then
that variable or one further out.
Every root a `&lvalue` creates is exact; the reads out of containers of §9.5
are where inexact ones come from. Compilation in call-graph order with
per-instantiation specialization means roots are always statically known —
parameters' roots come from each call site, and functions are specialized per
distinct root (Rust-lifetime precision via monomorphization, with zero
syntax).

The rules below are the *scope* rules, and hold of exact and inexact roots
alike. Only rules that need the target's **identity** rather than its lifetime
consult exactness — storing a reference into a relative-reference location
(§3.9), converting one to an index (`index_of`, §3.3), which array a shrink
through a reference frees (§5.1), and the compiler's proof that two
references name different arrays — and each of those takes its conservative
answer without it. A relative reference that names a pool (§3.9)
is where an exact root also *comes from*: a load out of one is rooted at that
pool, exactly, whatever container it was read out of.

Rules (scopes ordered by nesting; globals are the outermost scope, §11.1):

* **Store**: `r` may be stored into a location owned by root `L` only if
  `scope(root(r)) ⊇ scope(L)` — the pointee provably outlives the container.
  A value that *holds* references (a struct with a reference field, an
  array of slices, an ADT payload with one) stores under the same rule for
  what it holds: its root is that of the references stored into it, and each
  such store is on record for the shrink rules (§5.1). So do the elements
  `append` copies: an array's as the whole array would store, and a slice's
  as each element read out of it would (§9.5). A declaration stores
  its value into the variable it declares, with a type annotation or
  without: `let s = { let t: u8[] = "abc"; t[..] };` is an error, since `t`
  ends with its block.
* **Branch values**: whatever receives the value of an `if`, `match`,
  `block`, `loop` or bare `{ }` — a call's argument as much as a variable —
  gets it after the scopes its branch opened have ended. So a reference or
  slice that value is, or holds, must not be rooted at a variable declared
  in them, a match arm's binder included, or at a temporary made there:
  `f(if c { var t: i64[>..] = [1, 2]; t[..] } else { a[..] })` is an error,
  as is the declaration above.
* **Return**: a returned reference's root must be visible to the caller (a
  caller-supplied root, a global, or the function's own in-place-constructed
  return value).
* Struct types with reference fields are implicitly generic over those
  fields' roots; struct instances with different root bindings are distinct
  types for checking purposes (same layout). One whose references all point
  into one variable is bound to that root exactly; one whose references
  point into several is only bounded by the innermost of their roots, so a
  function given both kinds is checked for each. A `recursive fn`'s back
  edges reuse its body whatever they pass (§7.8), so its by-value parameters
  are always taken to be of the second kind.
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
  A variable declared before a loop and bound only inside it (`var last:
  Node? = null;` before `loop { if last { last.next .= child; } … last .=
  child; }`) is bound *ahead* of the loop: the body is scanned for its
  `.=` targets, and where every one of them resolves syntactically to one
  root — a container's element or field, a `push` or `alloc_ref` into it,
  a call whose return root is known (§7.8's scan), a reference variable
  bound to such — the variable has that root, exactly where the root's
  storage is its own, for the whole body; the first real rebind confirms
  it and supplies the rest of the provenance. Where the scan cannot tell,
  nothing changes, and a use before the rebind sees the read-back rule's
  answer (§9.5).
* All `return`s of one function must agree on the returned reference's root
  (v1 simplification; use one source or split the function).
* Inside recursive cycles the stricter §7.8 cycle store rule applies.
* A **temporary** — an array, struct or variant literal, or a call's
  result, viewed where it stands rather than built into a destination (by a
  slice parameter, a `for`, `[..]`, `bytes_of` or a path into it, §4.2) —
  lasts for the rest of its statement, or of the block whose final
  expression made it. Its scope is that statement's: the variables the
  statement declares outlive it, and those of the scopes the statement
  opens — the body of a `for` over it, the arms of a `match` on it — do
  not. So a reference or slice into it may be passed down, and stored or
  bound only in those inner scopes: `let s = f()[..];` is an error, while
  `let t = f(); let s = t[..];` is not, and neither is a view into `x`
  bound inside `for x in f() { … }`; it is never returned. A function it is
  passed to may keep it in its own locals, which die first. What a
  temporary *holds* is not rooted at the temporary (§9.5).

Violations are compile errors. There is no escape hatch in v1.

### 9.3 What the runtime still checks

Aborts (message + exit; not catchable):

* array/slice indexing out of bounds (elided wherever the compiler proves it
  cannot fire, §10.5; can be disabled wholesale in a designated unsafe-fast
  build);
* a reusable pool's `free(i)` with an index outside `[0, pool.len)`;
* a slice pool's `alloc_slice`/`realloc_slice` with a negative length, or
  one no data stack could hold (§5.4);
* a slice pool's `free_slice`/`realloc_slice` with a non-empty slice that is
  not one of its runs, where the checker could not tell (§5.4);
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
* guard-page hits (stack budget exceeded) — safe abort, never corruption;
* native call stack exhaustion (recursion deeper than a thread's stack
  holds, §7.8) — the same safe abort, with a diagnostic of its own.

`exit(code)` ends the program normally with the given process exit code.
Both `abort` and `exit` never return, which the checker knows: code after
them is unreachable, and either may be the whole of a `guard`'s else block
(§6.4).

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
qualified `const` (§2): a `const i64[3]` or `const Point` is a value whose
contents cannot be written into (its elements, its fields, and whatever a
reference to it reaches), and `const T&` / `const T[:]` are a reference and
a slice through which the pointee, or the elements, cannot be written: no
assignment, compound assignment, `++`, or growing/shrinking operation
through it compiles. Assigning a `const` value as a whole is the binding's
business (`let`, §4.4), and a copy of one is a fresh, writable value.
`const` is shallow: a reference read out of a field of a `const Box&` is as
writable as the field's own type says. Every value of reference or slice
type is either read-only or writable, and where that comes from is
*inferred*, per instantiation, exactly like roots, with no annotation
needed:

* Read-only: a string literal (`const u8[:]`, §3.7); `&x` and `x[..]` of a
  `const` value, of a field of one, or of a by-value `for`/`match` binding
  (§6.5) — these are `const T&` and `const T[:]`; a `bytes_of` view (§12);
  and whatever is read out of a slot declared `const`. A `let` binding *of*
  a reference or slice names the reference: it does not rebind, and writes
  through it follow the value's own constness (`let r .= xs[i]; r = 0;`
  writes an element of a `var` array, §3.8).
* Writable: everything else — `&x` and `x[..]` of a `var` or of a plain
  `let`, and whatever is read out of a slot that is not `const`.
* **Parameters and results are generic over constness.** A parameter
  declared `u8[:]` or `T&` takes a read-only or a writable argument, and the
  specialization is checked with the argument's constness (a write through
  a parameter given a literal is an error at that instantiation); one
  declared `const u8[:]` is read-only whatever it is given, which is what a
  function that only reads its input documents. A declared result type
  likewise names the shape, and the constness of a call's result is that of
  what the function returns (a `u8[:]` result of `return s` for a `const
  u8[:]` parameter is read-only at that call); `-> const u8[:]` makes it
  read-only always. Adding `const` is implicit everywhere (`u8[:]` fits
  `const u8[:]`); dropping it is never.
* **Slots say what they hold.** A field, an element, a global, an annotated
  variable and an assignment target are *slots*, and a read-only reference
  or slice is stored in a slot only if the slot's type is `const`: `struct
  Named { name: const u8[:] }` holds a literal, `struct Buf { bytes: u8[:] }`
  does not, and `views: const u8[:][>..]` takes either kind of view. So
  what is read out of a slot is exactly as writable as the slot's type says,
  and nothing is laundered through storage: a `u8[:]` field that could be
  written through can only ever have been given a writable slice.
* An `extern fn` (§7.10) is a C function and its parameters are what they
  say: a read-only argument needs the parameter declared `const`.

**Read-back roots.** A container names a scope, not the storage its contents
point into, so the root of a reference or slice of pointee type `T` read out
of a container `C` is re-derived. A **candidate** is a variable whose own
storage can hold a `T` by value — an array of `T` in any array kind, a struct
or ADT payload with a `T` field, a `T` itself, and so on through by-value
nesting; a variable that merely holds *references* to `T` is not one, and a
reference or slice field ends the search. Static data is a candidate for the
element types a literal can supply; for a writable reference or slice only when
nothing else is, since a writable slot is never given a literal (above) and so
holds static data only as a null or an empty slice. Then, by where `C`'s own
root lies:

1. **A global.** Only globals outlive globals (§11.1), so the owner is a
   global candidate whatever local scope is open. One candidate: that
   variable, exact. Otherwise the global scope, inexact.
2. **A local of the function being checked**, at any block depth,
   by-value parameters included. Everything stored into `C` was reachable
   from this frame and had to outlive `C`, so the owner is a local declared
   at or outside `C`'s block, a reference parameter's pointee, a global,
   static data, or storage of the caller's that a parameter leads to through
   references: the ones a by-value parameter holds, which its argument
   filled, and the ones in what a reference parameter points at. This
   function cannot enumerate that storage, and it all outlives the
   parameter's root — for a by-value parameter the root of what its
   argument held (§9.2's generic roots of reference fields) — so that root
   is its candidate. The root is the innermost candidate — the true owner is
   that one or one further out, so its scope bounds every possibility — and
   is exact when there is exactly one candidate in all and it is not such a
   parameter's root, which only bounds the storage behind it.
3. **A reference parameter's pointee, or itself inexact.** The owner may be
   caller storage this function cannot enumerate: the root is `C`'s, inexact.
4. **A temporary** (§9.2). Everything in it came from the literal's
   initializers or from the call whose result it is, so the root is theirs:
   the innermost root among the initializers, or the one the result's
   contents are rooted at, exact when that is one variable exactly.

An optional variable bound only to `null` so far (`var best: Node? = null;`
before the loop that binds it) has no binding to take a root from; a use of
it there takes the answer the read-back rule gives a local container: the
innermost candidate for its pointee type at its own depth or outside, exact
when there is exactly one.

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
* **Signed is the right default for sizes.** `.len` returns `i64` (§3.1) and the
  top bit is provably unused, so the sign bit costs nothing real, while
  subtraction and difference math stay natural. Signed sizes avoid unsigned
  wraparound in ordinary differences.
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

Known gaps are TODO 0f. Whole-program compilation lets the analysis reach
across calls: each specialization sees concrete argument roots, a caller's
facts reach an inlined callee body directly, and a call that is not inlined
carries them too. Every call site records what it proves about the arguments
it passes — constant bounds on an array's length, and how the passed lengths
and integers relate — and a specialization enters with the meet of those
facts once all of its sites have been analyzed (callers are analyzed first;
a site inside a recursive cycle leaves its callee with nothing). In the
other direction a callee is summarized rather than assumed hostile: the
storage it may resize or overwrite, through its reference parameters, in
globals, or in captured outer locals, is computed as a fixpoint over the
call graph, and a call kills only what those effects can name. A kernel
`fn blur(src: u8[>..]&, dst: u8[>..]&)` that only reads and writes elements
thus sees `src.len == W * W` when every caller established it, and leaves
the caller's own facts about `src` intact.

---

## 11. Program structure

### 11.1 Modules and globals

* One program, compiled whole. `import a.b.c;` includes the file `a/b/c.goose`
  once, resolved relative to the *main file being compiled*, then in the
  standard library's directory (`--stdlib <dir>`, else `GOOSE_STDLIB`, else
  the `stdlib/` of the source tree the compiler was built in); the form
  `import .a.b;` (leading dot) resolves relative to the *importing file*
  instead. All declarations are public in v1; a name collision within one
  namespace is an error. Top-level declarations are order-independent.
* **Namespaces** (docs/design/namespaces.md). `namespace image;`, at most
  once and before a file's declarations, places them in namespace `image`;
  a file without it declares into the global namespace, and several files
  may declare the same namespace. An import loads a file; it neither creates
  a namespace nor brings a namespace's names into scope. From elsewhere a
  declaration is `image::Pixel`, `image::brightness(p)`,
  `image::Shape.Circle { r: 1.0 }`, `image::pool`; `::name` selects a
  global declaration (or builtin) that a namespaced one shadows. A
  declaration may spell its namespace itself (`fn image::brightness(…)`,
  `fn ::main()` inside a namespaced file); the qualifier decides the
  declaration's namespace entirely, names inside it included. An
  unqualified name resolves lexically first (locals, parameters, type
  parameters, nested functions), then in the current declaration's
  namespace, then in the global namespace and the builtins. A function
  name's overload set is that of the first namespace in this order that
  declares the name at all; sets never merge across namespaces (a
  namespaced `hash` overload reaches the global integer ones as
  `::hash(x)`). UFCS follows the same rule for the calling code's
  namespace, a generic body resolves names where it is defined, and the
  `format` hook is the one type-directed exception (§3.7). Namespaces
  affect only name resolution, type identity and generated C names
  (§7.10): no runtime representation, no privacy, no re-exports.
* Globals are declared like locals (`let`/`var`, any type including
  resizable). Semantically the whole program runs inside an implicit
  outermost scope owning them: they participate in the depth check (§9.2) as
  the outermost roots, initialize before `main` in declaration order within a
  file and imported-file-first across files (so a global initializer may name
  one from a module it imports; their initializers may call functions), and a
  global resizable simply owns a stack's bottom for the program's life (a
  natural whole-program arena). That outermost scope is a frame like any
  other: a global's storage belongs to the program instance running it and
  is never shared with another (§11.2).
  `const` globals of flat fixed type with compile-time-evaluable
  initializers live in static data; the initializer of any `let` or
  `const` global is a named constant a compile-time size may use
  (`i64[N]`).
* Entry point: `fn main() { }`, in the global namespace. Only the *root*
  file's `main` is the entry; a `fn main` in an imported file is ignored
  entirely (not an entry, not callable, no collision). A runnable file can
  thus double as an importable library: give it `fn main_x() { ... }` plus a
  `fn main() { main_x(); }` wrapper, and importers call `main_x` directly. A
  namespaced file does the same with `fn ::main() { ns::main_x(); }`.

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
  the args, starts the worker, and returns its id. IDs increase monotonically
  and are never reused. `hardware_threads() -> i64` exists for sizing.
  `thread_wait(id)` blocks until that worker's body has returned and all of
  its Goose storage has been released — enabling both scoped fork/join
  parallelism and orderly shutdown (send quit messages, then wait). Repeated
  waits on a completed id return immediately; multiple workers may wait for
  the same id. A negative or never-issued id, or a worker waiting for itself,
  aborts with a runtime diagnostic.
* A worker's return releases its copied arguments, stack block, every reserved
  and committed data-stack region, and active-worker record, whether or not
  anyone waits for it. The runtime retains no per-completed-worker record or
  native handle. Native threads are detached; their final native stack/TLS
  teardown occurs when the runtime entry wrapper returns, immediately after
  publishing Goose cleanup completion. A child worker owns its storage
  independently and may outlive the worker that spawned it. Queued messages
  belong to their queues and remain available to receivers after the sender
  exits. Workers still running when `main` returns are killed.
* A thread program is an instance of the program with globals of its own:
  at `thread_spawn` every global the worker's program uses is copied from
  the spawning instance, exactly as the arguments are, and the worker reads
  and writes its copies from then on. Nothing is shared: a worker that
  increments a global increments its own, and main's is what it was. A
  global whose type is not **flat** (§1.1) cannot be copied and is an error
  in any function a `thread_fn` reaches. `const` globals with compile-time
  initializers are static data (§1.2), read-only and therefore shared as
  they are.
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
| `hardware_threads() -> i64`, `thread_spawn(worker, args…) -> i64`, `thread_wait(id)` | workers (§11.2) |
| `qput(v)`, `qget<T>() -> T`, `qpoll<T>() -> T, bool` | the typed queues (§11.2) |
| `to_bytes(a) -> u8[>..]`, `to_bytes(a, out)` | an array's image — a varint byte count then its element region — fresh, or appended to a growable `u8` array (§3.9, `design/serialization.md`) |
| `bytes_of(a) -> u8[:]` | the element region alone, as a view: no copy, and never writable |
| `from_bytes<T[>..]>(b: u8[:]) -> T[>..], bool` | a *verified* array from an untrusted image; empty and `false` if it is not one. Also builds `T[>..<]` and the `T[]` family |
| `embed_shader(path)` or `embed_shader(stage, source, …) -> const u8[:]` | compile-time GLSL compilation to a static shader blob; graphics extension, detailed in the implementation notes, section 3.14 |

And the array members, ordinary functions of their receiver per UFCS
(`a.push(v)` is `push(a, v)`):

| Member | On | Meaning |
|---|---|---|
| `.len -> i64` | every array kind and slices | the element count (§3.3) |
| `.cap -> i64` | limited arrays | the capacity (§3.3) |
| `.push(v) -> T&` | limited, grow-only, grow-shrink | one element, constructed in place (§3.3) |
| `.append(src)` | limited, grow-only, grow-shrink | an array or slice of elements (§3.3) |
| `.pop() -> T`, `.resize(n, v?)`, `.clear()` | limited, grow-shrink; grow-only where §5.1 allows a shrink | shrinking, and growing with a fill value (§3.3, §5) |
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
SIMD/alignment annotations; dynamic stacks; labeled break; namespace
privacy, re-exports and nesting (§11.1).

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
The newest, highest-priority items first; items since resolved are kept at
the end, each with where its resolution lives.

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
    could extend §10.5 by making index validity a type property, rather
    than relying only on dataflow analysis.
0f. **Bounds-check analysis, known gaps** (§10.5) — a loop's exit condition
    is a disjunction (`!(i < n && p)`), which a difference-constraint domain
    cannot represent, so post-loop bounds rest on the inferred invariants
    instead; LLVM meets the same wall and works around it by duplicating
    blocks so the conditions never merge, which is available here too. A
    value loaded from an array has no known range, so indirect indexing
    (`dist[out[k]]`, `bench/goose/graph_csr.goose`) keeps its check — see
    0e. Release-mode wrapping (§6.2) is deliberate and stays, so the
    analysis proves absence of wrap explicitly where it needs to.
0c. **Pointee writes through optionals** — a narrowed optional writes
    through fine, but there is no way to write through an optional without
    narrowing; and rebinding to a plain reference first (`let r: T& = o;`)
    is the only escape hatch. Possibly fine; revisit with usage.
0d. **Lifetime precision, remaining cases** — two checker conservatisms
    still exceed the spec: long-distance returns (§7.9) only carry
    references to globals/static data (precise rule: rooted at or above the
    target's frame), and the recursive-cycle store rule (§7.8) admits only
    globals, free variables and pool parameters as roots of stored
    references — a reference to a caller's fixed-size local is still
    pass-down-only inside a cycle.
    (One-root-per-reference-variable and the single agreed return root are
    language rules, §9.2. Writability follows `const` types; storage no longer
    removes read-only restrictions, §9.5.)
2. **Error propagation sugar** — `return from` is the mechanism; revisit
   whether a convention/sugar layer (a `try`-alike) is wanted once idioms
   emerge.
3. **Move operation for resizables** — assign-and-leave-source-empty, as the
   one sanctioned "move".
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
13. **Labels for `break`** — if early-out patterns demand them.
15. **Wasm fallback** — index-based reference representation details.
16. **Relative-reference region tracking** — copies of values containing
    *self-relative* references are currently rejected outright (§3.9; the
    `in pool` form already copies); track the region an offset ranges over so
    whole-region copies (and serialization moves) can be proven safe.
### Resolved

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
    by variables only; a shrink is an error while one is live, naming
    it and where it was bound; specializations record what they shrink so
    that calls are checked the same way.
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
4. **`[>..<]` interior-reference relaxation** — DONE, see §5.2: the
   liveness test of §5.1, plus per-specialization shrink summaries for
   the shrinks it cannot see directly (through a reference, or of a global).
11. **FFI** — DONE, see §7.10: `extern fn` binds a Goose signature to a C
    function, and what crosses is the flat fixed-size types by value,
    references and slices to them, and `u8[>..]&` builders; everything else
    is rejected at the declaration.
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
    view, for a save that copies nothing. What v1 does not admit: references
    into a *field* of an element, `from_bytes` into a fixed or limited array,
    and a big-endian host (which aborts rather than writing bytes only it can
    read).

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
frame — `{ base: byte*, len: int64 }` — plus the element region on the data
stack. `&v` yields the header's address (a fat reference is that pointer
plus the stack identity); with the stack identity known statically (or
carried as a fat reference / hidden argument), push compiles to
`*(T*)S.top = e; S.top += size; h.len++`.

A resizable-tailed struct whose other fields are fixed-size (and hold no
relative reference) is a **frame object**: a C struct of those fields
followed by the tail's own header (or, for a tail that is itself such a
struct, its frame object), held in the owning frame exactly as a fixed
value would be, with only the innermost tail's elements on the data stack.
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
and `len` the tail's count; a reference to such a nested tail is a compile
error — reference the owning variable.

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
program     := namespace? topdecl*             // imports may precede namespace
namespace   := "namespace" ident ";"
topdecl     := import | struct | enum | typealias | fndecl | globaldecl
import      := "import" "."? ident ("." ident)* ";"
declname    := ident | ident "::" ident | "::" ident   // the namespace, else the file's
qname       := ident | ident "::" ident | "::" ident   // a declaration reference
struct      := "struct" declname generics? "{" fieldlist "}"
enum        := "enum" declname generics? "{" variant ("," variant)* ","? "}"
variant     := ident ( "{" fieldlist "}" )?
fieldlist   := field ("," field)* ","?
field       := ("let" | "const")? ident ":" type ("=" expr)? | "pad" intlit?
typealias   := "type" declname "=" type ";"
generics    := "<" ident (":" type)? ("," ident (":" type)?)* ">"

fndecl      := ("extern" strlit?)? "recursive"? ("fn" | "thread_fn") declname
               generics? "(" params? ")" ("->" rettypes)? (blockexpr | ";")
                                                 // nested: ident only
params      := param ("," param)* ","?
param       := "var"? ident (":" type)?          // untyped => generic
rettypes    := type ("," type)*                  // no parens in declarations
globaldecl  := ("reusable" ("[" "]")?)? ("let" | "var" | "const") declname (":" type)? "=" expr ";"

type        := "const"? (prim | qname tyargs? | "(" type ")") postfix*
                                                 // const: the first & or [:] (§9.5)
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

stmt        := decl | assign | incdec | exprstmt
decl        := ("reusable" ("[" "]")?)? ("let" | "var" | "const") identlist (":" type)?
               (("=" | ".=") exprlist)? ";"     // .= binds by reference (§3.8)
assign      := lvalue assignop expr ";"          // = .= += -= *= /= %= &= |= ^= <<= >>=
incdec      := lvalue ("++" | "--") ";"
exprstmt    := expr ";"

expr        := control | binary
control     := ifexpr | matchexpr | blockexpr | loops | jumps | guardstmt
ifexpr      := "if" expr blockexpr ("else" (ifexpr | blockexpr))?
guardstmt   := "guard" expr ("else" blockexpr | ";")
loops       := ("while" expr | "for" forbind "in" iter | "loop") blockexpr
forbind     := "&"? ident ("," ident)?
iter        := expr | expr ".." expr
jumps       := "return" exprlist? ("from" qname)? | "break" expr? | "continue"
matchexpr   := "match" expr "{" arm ("," arm)* ","? "}"
arm         := pattern "=>" expr
pattern     := ident ("&"? ident)? | "-"? intlit (".." "-"? intlit)? | "_"
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
structlit   := (qname tyargs? | varianttype) "{" (fieldinit ("," fieldinit)* ","?)? "}"
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
same type as `T?`, and `?` on an already-optional type is an error; `from`
is not reserved — `return E from f` is recognized by the identifier that
follows it, so `from` remains usable as a name.

---

## Appendix E. Implementation notes (informative)

What the current compiler does where the text above leaves it a choice. The
compiler's own description, pass by pass and analysis by analysis, is
`implementation.md`.

* **Two C backends.** The generated C is normally written to a file for a C
  compiler to build (`-o out.c`). Where the compiler was built with TinyCC
  (the `third_party/tinycc` submodule), giving no output file compiles that
  same C inside the compiler's own process instead and calls its `main`, so
  a program runs with no C toolchain present and no file written. The
  program shares the process: its exit status becomes the compiler's, and
  the compiler's own progress lines move to stderr to leave stdout to the
  program. It runs on the compiler's main thread, whose stack the compiler
  reserves at 64 MB on Windows and macOS, where an executable's main thread
  gets 1 MB and 8 MB by default; on Linux it is the stack limit the process
  started with (`ulimit -s`), as for an executable. Nothing about the
  generated C differs between the two, and `-D` is written into that C
  rather than passed to a backend so that stays true. TinyCC does not
  optimize and cannot place thread-local storage in an in-memory run, so a
  program using workers (§11.2) is refused there
  (docs/design/jit_backend.md).
* **Fat references.** A callee that grows a resizable through a reference
  (`push` through a `f64[>..]&`) must know which data stack to bump. The
  compiler does not pin that stack per call site (§10.2): every reference
  to a resizable-class value carries the stack identity beside the header
  address (C.2), and a reference with reusable-pool provenance (§5.4)
  carries the freelist as well. There is no surface syntax; the form is per
  instantiation and invisible to the program.
* **Stack assignment** is the hidden-argument strategy §10.3 permits: every
  function that uses data stacks takes its base index as a hidden argument
  and addresses its nonfixed locals at constant offsets from it, callees
  start above its in-use watermark, and stacks are reserved lazily as the
  depth first reaches them. The globals of a program instance, with the
  dedicated stacks of the resizable ones, form one struct reached through a
  single thread-local pointer (a plain one in a program without workers):
  main's is a static instance, a worker's is allocated at its start and
  filled from the spawn image, which carries the used globals behind the
  arguments. The only C statics are string literals and `const` globals
  with compile-time initializers. Every generated function is shared
  between thread programs and the main program.
* **Element-run results** (§7.3) exist for variable *array* results
  (`T[]`): an element-run receiver compiles the callee a second time in run
  form (raw elements plus a count out-parameter), so `v.append(f())` is
  contiguous with no copy, and a named result pays the specified single
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
  limited-array slots is zero-initialized. Both work around MSVC 19.44–19.51,
  which at any optimization level miscompiles a read through a copy of a
  struct holding the address of a block-scoped local (a slice of a fixed
  array declared in a loop body, passed to an inlined callee), and exploits
  the indeterminate bytes of a copied partially-written struct. Neither
  changes what the program means.
* **The store record.** Every store of a reference, slice, or value holding
  one into a variable, field, element or global is logged with the stored
  value's root, exactness, and source container, program-wide; a
  specialization also summarizes the stores into its parameters' pointees,
  replayed onto the caller's containers at each call. A grow-only shrink
  (§5.1) asks this record whether any live value holds a reference into
  the array, following source links (a copy holds what its source holds; a
  global source is judged by its type), and a shrink inside a loop is
  re-asked at the loop's end for stores the rest of the body made.
* **Liveness at a shrink.** The checker keeps the statement index of every
  open block; a holder is live when its name occurs in a later statement
  of an open block at or inside its scope, in that block's tail, or
  anywhere in an enclosing loop it outlives. Nested functions in scope are
  followed by name from the calls in that code.
* **Literal parameters** (§7.7). Which parameters are literals is part of
  the specialization key, their values are not. A read of one is a value
  with no constant folding but the literal's adaptability; every
  adaptation of the bare parameter is recorded on the specialization with
  its line, a literal parameter passed on as a literal records a link to
  the callee's parameter, and each call site's literal is verified against
  the closure of those records after the whole program is checked. The
  parameter is passed at its nominal type (`int64_t`, `double`) and each
  use casts to the type it adapted to, which the verification proved
  exact.
