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
  (values above `int.max` are carried as bit patterns, §6.2). Character
  literals `'a'` are integer constants.
* Float: with `.` and/or a decimal exponent (`1.5`, `2.5e-3`); C99-style hex
  floats with a mandatory binary exponent (`0x1.8p3`). A `.` starts a
  fraction only when a digit follows, so `1..2` lexes as a range.
* String `"..."`, with escapes `\n \t \r \0 \\ \" \' \xNN` (two hex digits).
* `null` — the empty value of any optional type `T?` (§3.8).

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
`Shape..` variable-mode ADT.

Evaluation order is left-to-right everywhere (operands, arguments, field
initializers — which named struct literals keep aligned with construction
order by requiring declaration order, §4.2). Overlapping copies have
memmove semantics.

A grammar sketch and precedence table are in Appendix D.

---

## 3. Types

### 3.1 Scalars

* `int` — the default integer type: signed two's-complement 64-bit, on every
  platform. All integer arithmetic happens at this type. Use for all indices,
  sizes, and any integer without precise layout needs.
* `i8 i16 i32 i64 u8 u16 u32 u64` — integer **storage types**. They exist
  only inside compound types (fields, array elements) and reads widen to
  `int` immediately (zero- or sign-extended per signedness). Expressions
  have exactly one integer type: `int`. Individual variables, parameters,
  and returns are always plain `int` (never a storage spelling).
* `varint` — variable-length integer storage type (§3.6). Reads widen to
  `int`; writable only at construction.
* `flt` — 64-bit IEEE float, the default float type. `f64` is its storage
  spelling (fields/elements only, like the integer storage types).
* `f32` — 32-bit float storage type (fields/elements only, like the rest).
  Unlike integers, `f32` converts to `flt` *lazily, within an expression*: a
  subexpression whose operands are all `f32` (loads of f32 storage, float
  literals, `as f32` results) computes in 32 bits; contact with a `flt`
  operand promotes, and *binding any variable/parameter/return widens to
  `flt`* — so an f32 computation lives entirely inside one expression,
  typically ending in a store back to f32 storage. Float literals count as
  either (they adapt to an `f32` context).
* `bool` — 1-byte storage, values `true`/`false`. Produced by comparisons;
  required by `if`/`while` conditions (no int-to-bool coercion).

Stylistically `i64`/`f64` are used only where the width is the point; `int`/
`flt` elsewhere.

Narrowing (storing an `int` into a smaller storage type, or `flt` into `f32`)
is never implicit, with one exception: a *literal* whose value statically
fits the destination storage type stores directly (`let x: u8 = 255;` is
fine, `= 256` is a compile error). See conversions §6.3.

### 3.2 Structs

```goose
struct X { a: i8[3], b: i32, c: int = 0 }
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
* A field may declare a default value (`c: int = 0` above); constructors may
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
Shrink operations (`pop`, `resize` downward, `clear`) exist only on `[>..<]`
and limited arrays.

Built-in members: `.len` (always, returns `int`), `.cap` (limited arrays),
`.push(v)` (returns a reference to the new element on grow-only and limited
arrays — the idiomatic way to link up just-built data; on grow-shrink arrays
it returns nothing, since no interior references exist there, §5.2),
`.append(src)` (src an array/slice of the element type), `.pop()`,
`.resize(n, v)` (grow with fill value `v`, or shrink), `.resize(n)` (shrink
only), `.clear()` per the rules above. Growth always supplies element
values — no operation can expose uninitialized slots (§5.3). Per UFCS these
are ordinary functions: `a.push(v)` is `push(a, v)`.

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
    Circle { r: flt },
    Rect { w: flt, h: flt },
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
* Everywhere else (struct fields, ADT payloads, relative-reference offsets):
  **signed**, zigzag-transformed (`(v << 1) ^ (v >> 63)`), so small
  negatives are as compact as small positives. The transform sits on the
  value path, not the length/advance path, so it costs no decode latency.

Because the two encodings differ, varint values are never copied byte-wise
between contexts: any varint-to-varint construction (e.g. a struct varint
field initialized from `arr.len`) goes through `int` — decode, re-encode.

Restrictions: a `varint` field/element is written only at construction of
its containing value; changing it means reconstructing the container. Reads
widen to `int`. A struct containing `varint` fields is variable-class.
References to `varint` fields are always read-only (§3.8).

### 3.7 Strings

There is no built-in string type. A "string" is any array-family type with
element `u8`: `u8[]` (immutable flat string), `u8[varint]` (compact), `u8[>..]`
(string builder), etc. String literals are static constant `u8` data,
implicitly copyable into any of these representations and usable as `u8[:]`
slices directly (with non-writable provenance, §9.5). No encoding is
enforced; UTF-8 is a library-level convention.

### 3.8 References

`T&` is a reference to a `T`: one machine address, no pointer arithmetic, no
casts, never dangling.

**References are transparent** (like C++ references): an expression that
denotes a reference behaves as its pointee in every value context — reads,
arithmetic, comparisons (`r1 == r2` compares the pointees), passing to a
by-value parameter, `print(r)` — all operate on the target. There is no
dereference operator and no `copy` builtin; the load is implicit.

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
  the reference value. Where a type is inferred (`let x = r;`, untyped
  generic parameters), the reference decays to a pointee copy — except an
  explicit `&lvalue` initializer, which infers the reference type. To bind a
  reference-returning call, annotate: `let e: T& = pool.push(v);`.
* References to `varint` fields are always read-only (varints are written
  only at construction, §3.6).

**Optionals.** References are non-nullable by default. `T?` is an *optional
T*: represented as a nullable reference to T (null = address 0, no space
cost), with all reference semantics and restrictions (roots, lifetimes,
writability). This composes with any type — pass `int?` for an optional
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
through a reference (e.g. `push` through a `flt[>..]&`), it must know which
data stack to bump. Where the compiler cannot pin that stack statically per
call site (§10.2), such a reference costs two pointers instead of one — the
address plus the stack identity, passed as a fat reference or hidden
argument. There is no surface syntax; this is purely an implementation
choice per instantiation.

**What references may point to.** Anything *except*:

* the interior (elements/fields) of a grow-shrink array `[>..<]` (§5.2);
* the interior of a fixed-mode ADT payload (§3.5).

References into a grow-only resizable `[>..]` remain valid for the owner's
entire life: grown memory never moves and never shrinks. References into
limited arrays `[..k]`/`[..]` are also allowed and stay type-valid across
pop/push reuse (§5.3).

### 3.9 Relative references

A relative reference is a storage form of reference constrained to point
within the *same enclosing array/pool* as the location storing it. Spelled
`T&<u8>`, `T&<u16>`, `T&<u32>`, `T&<u64>`, `T&<varint>` — widths are spelled
unsigned (or `varint`), like array length field types, even though the
stored offset is signed.

* Stored as a self-relative signed offset of the given width (`varint`
  offsets use the signed zigzag encoding, §3.6). Loading one yields an
  ordinary `T&` (base = address of the offset field itself). Storing one
  requires the compiler to see that both the reference and the destination
  location derive from the same root array (§9.2 root tracking); the offset
  is range-checked at the store (abort on overflow of the width).
  Varint-width relative references are written only at construction, like
  varint fields (re-encoding could change the byte length); fixed widths may
  be re-stored with `.=`/`=`.
* Copying a *value that contains* relative references (assignment from an
  lvalue, a by-value argument, a by-value match binder, an element copy) is
  a compile error: the copied offsets would still be measured from the
  source location. Construct such values in place (literals), and bind their
  match payloads by reference. (TODO 16: track the region a relative
  reference ranges over, so provably whole-region copies can be allowed.)
* Optional spelling `T&<u8>?` uses offset 0 as null (a relative reference
  to itself is meaningless).
* Because they are position-independent, structures linked by relative
  references are trivially serializable / mappable.

This is the mechanism for ultra-compact trees: single-byte links to nearby
nodes (see A.2).

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
* Slices obey the same creation restrictions as interior references (§3.8):
  no slices of grow-shrink arrays.
* A slice of a grow-only resizable taken before growth remains valid (it just
  doesn't see the new elements).

---

## 4. Values, copying, and assignment

### 4.1 Everything is a value

Goose has uniform **by-value semantics**. Assignment, argument passing,
returning, and pushing into arrays all *copy the full inline representation*
of the value — however large that is. The only types whose copy is shallow
are references and slices (the copy is the reference/slice itself, never the
pointee).

There is no ownership transfer, no move semantics, no destructors, no `Drop`,
no reference counting. Deallocation is exclusively scope exit resetting stack
pointers. (A future *move* operation for resizable arrays — assign + leave
source empty — is anticipated but not in v1.)

Copies of variable/resizable values are real and cost O(size); they occur
only at the well-defined construction contexts below, and idiomatic code
passes large read-only data as slices instead.

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
  context; `[v; n]` fill form for fixed arrays;
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
* string literals (§3.7).

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
  clear-then-copy; the bump pointer resets to the array's element start and
  the new contents are copied in;
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
(`o == null` is the null test). Ordering `< <= > >=` exists on `int` and
`flt` only.

---

## 5. The resizable/shrink rules

### 5.1 Grow-only `[>..]`

* `push`/`append` bump the stack; all prior interior references/slices stay
  valid forever (memory below the top never moves or gets reused while the
  owner lives).
* May contain variable-size elements (build strings/ADTs in place, §7.3).
* No shrink operations of any kind.

This is the workhorse type: arenas, pools, string builders, tree storage.

### 5.2 Grow-shrink `[>..<]`

* Fixed-size elements only.
* `pop()` returns the element by value; `resize`/`clear` allowed.
* **No interior references or slices into it may ever be created.** Access is
  by whole-array reference + indexing (bounds-checked against current
  length). Rationale: after a pop, that stack-top memory can later be reused
  by *different types* (other locals, other pushes), so references into it
  cannot be allowed to survive; the coarse ban needs zero analysis. Flow-
  sensitive relaxations are future work (TODO).
* Iterating with `for` uses indices under the hood (the `&x` binding form is
  unavailable); mutating length during iteration is legal and merely a logic
  hazard (bounds checks keep it safe).

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

* `a.alloc_index(v) -> int` — index of a slot: a freelist slot if available,
  else a fresh `push`.
* `a.alloc_ref(v) -> T&` — same, returning `&a[i]`.
* `a.free(i)` — records slot `i` for reuse. The element remains a valid,
  live value of its type forever.

Semantics, not just implementation: *all elements remain valid at all times*.
A reference to a freed-then-reused slot reads a different (same-typed) value —
type-safe reuse, never memory corruption. This is the language's answer to
tree-mutation workloads that would otherwise need an allocator.

---

## 6. Expressions and arithmetic

### 6.1 Operators

C/Rust set: `+ - * / %` (`%` on `flt` is C `fmod`), comparisons, `! && ||`
(short-circuit, `bool` only), bitwise `~ & | ^ << >>` (on `int`), assignment
statements `=`, `+=` etc. on assignable lvalues, `.=` (reference rebinding,
§3.8), and `++`/`--` as statements on integer lvalues (no expression form).
Precedence: Appendix D. Range expressions `a..b` appear only in slicing
brackets, `for` headers, and match arms.

**Elementwise math**: the arithmetic operators apply memberwise to any two
values of the *same* struct/fixed-array type whose scalar leaves are all
integers or all floats; the result has that same type. This covers vector
math without an operator-overloading feature. The standard library supplies
`float3` and friends; named vector ops (`dot`, `cross`, `normalize`, …) are
ordinary stdlib overloads per math type, not language builtins.

### 6.2 Integer semantics

* All integer arithmetic is 64-bit (`int`). Mixed-width source operands are
  already widened at load, so C's promotion zoo does not exist.
* Overflow: aborts in debug builds; wraps two's-complement (defined) in
  release. Division/modulo by zero: aborts always. `int.min / -1`: aborts.
  Shift counts are masked to 0–63 (defined).
* The *only* dangerous operation is a narrowing store, and it never happens
  implicitly (§3.1 literal exception aside, which is statically checked).
* **Unsigned 64-bit values** are carried in `int` as bit patterns; there is
  no separate unsigned expression type. For the few operations where
  signedness changes the result bits, builtins are provided:
  `unsigned_div(a, b)`, `unsigned_mod(a, b)`, `unsigned_shr(a, n)`,
  `unsigned_less(a, b)`. Their use should be rare (e.g. updating a `u64`
  storage field in a specific way); everything else defaults to signed.

### 6.3 Conversions

* Implicit: storage-type loads widen to `int`; `f32` promotes to `flt` on
  contact with `flt`; literals narrow when they statically fit (§3.1).
* `x as T` — explicit conversion, **range-checked in debug** (abort on value
  change), truncates/rounds-toward-zero silently in release: int → narrower
  ints, int ↔ flt, flt → f32.
* `x as! T` — always-unchecked wrap/truncate, for when losing bits is what
  is intended, even in debug.
* flt → int (both forms in release, `as!` always) is defined exactly:
  truncate toward zero, then wrap modulo 2^64; NaN yields 0. The common
  in-range case is one compare and a hardware conversion; only the
  out-of-range tail pays for the defined wrap.
* No implicit int↔flt mixing in expressions.

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
(`return`, `break`, `continue`, or a call to an aborting function); code
after the guard proceeds with `c` known true — including flow-narrowing of
`T?` (`guard r else { return; }` leaves `r: T&`). The bare form `guard c;`
is shorthand for `guard c else { break }` inside a loop or `block`, and for
`guard c else { return }` otherwise; it is valid only where that implicit
exit requires no value.

### 6.5 `for`

Built-in iteration only (no iterator protocol):

* `for i in a..b` — integer range, half-open `[a..b)`.
* `for i in n` — sugar for `0..n`.
* `for x in arr` — element copies (fixed-size elements; storage types widen
  as always).
* `for &x in arr` — element references (required form for variable-size
  elements, whose walk is sequential; also the mutation form). Unavailable
  for `[>..<]` (§5.2).
* `for x, i in arr` / `for &x, i in arr` — with index.

All array-family types and slices are iterable. Custom access patterns are
provided by HOFs taking static function values (§7.6), which compile to
plain loops.

---

## 7. Functions

### 7.1 Declarations and generics

```goose
fn name(a: int, var b: flt, xs: int[:]) -> int { ... }
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
  coercions (array→slice §3.10, literal fit §3.1), else tag dispatch (§8.2),
  else error. Ambiguity is an error.
* Multiple return values: `fn f() -> A, B`; received as `let a, b = f();`.
  There is no tuple *type* — multiple returns are a calling convention;
  structs are the way to keep data together. (Function *types* with
  multiple returns need parens — `fn(int) -> (A, B)` — to disambiguate the
  comma; declaration headers don't.) Nonfixed types are allowed in any
  return position; each nonfixed result gets its own destination per §7.3
  (possibly distinct stacks).

Declarations at top level are order-independent (whole-program compile);
only global initializers have ordered semantics (§11.1).

### 7.2 Parameter passing

Uniform with `=`: **all parameters pass by value** (full inline copy).
References/slices are values too, so "by reference" is spelled explicitly on
*both* ends: `T&` in the signature, `&x` at the call site — forgetting both
is a (possibly slow) copy, forgetting one is a type error, never a silent
semantic change. (The one shorthand is array→slice at call sites, §3.10,
which is cheap and copy-free by definition.)

By-value passing of a variable/resizable argument reserves the statically
assigned stack region for that parameter at the call site, constructs/copies
the value there (§4.3), and the callee owns it like any local. This is a
deliberate, visible cost; slices are the idiom for large read-only data,
references for mutation.

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

> **NOT YET IMPLEMENTED:** the dual request kinds. The current compiler has
> one form per return class: resizable results already emit raw elements
> plus an out-of-band count (the element-run behavior), so
> `v.append(f())` of a resizable-returning `f` is contiguous with no copy;
> *variable* results (`T[]` etc.) construct their self-describing value
> form, and appending one slides its length prefix out with one memmove.

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
fn foo<F: fn(int)>(a: int) { F(a); }
foo(1) { print(it) }                  // trailing-block sugar, implicit `it`
foo(1) { x => print(x) }              // named block params
fn h(v: int) { print(v) }  foo(1, h)  // or pass a named function
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

**Call-site type arguments.** Type arguments are inferred from the argument
types whenever they appear in the parameter list: `fn foo<T>(x: T)` is
called as `foo(1)`, never `foo<int>(1)` — the typechecker must support this
for both `<T>`-style and untyped (implicitly generic) parameters. An
explicit list `f<int>(x)` is allowed, and *needed* only when no argument
mentions the parameter (e.g. `qget<int>()`). Syntactically, `f<` commits to
a type argument list only when the `<…>` is immediately followed by `(`,
by `{` for a struct literal (`Pair<int> { … }`), or by `.ident {` for a
variant literal (`Opt<int>.Some { … }`); otherwise `<` is the comparison
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
unrestricted. (Refinements are future work, TODO.)

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
convention** (bool, enum, string, int — application's choice; by custom the
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
* Over integers: constant and half-open-range arms; `_` required.
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
fn area(c: Shape.Circle) -> flt { c.r * c.r * 3.14159 }
fn area(r: Shape.Rect) -> flt   { r.w * r.h }
fn area(p: Shape.Point) -> flt  { 0.0 }

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

Every reference/slice value has a static **root**: the local or global
variable whose owned storage contains the target. Compilation in call-graph
order with per-instantiation specialization means roots are always
statically known — parameters' roots come from each call site, and functions
are specialized per distinct root (Rust-lifetime precision via
monomorphization, with zero syntax).

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
  checker single-pass over loop bodies.
* All `return`s of one function must agree on the returned reference's root
  (v1 simplification; use one source or split the function).
* Inside recursive cycles the stricter §7.8 cycle store rule applies.

Violations are compile errors. There is no escape hatch in v1.

### 9.3 What the runtime still checks

Aborts (message + exit; not catchable):

* array/slice indexing out of bounds (elidable by optimizer; can be disabled
  wholesale in a designated unsafe-fast build);
* limited-array capacity overflow;
* relative-reference offset overflow at store;
* debug only: integer overflow, `as` range violations;
* division by zero (always);
* `assert` failures;
* guard-page hits (stack budget exceeded) — safe abort, never corruption.

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
  language's const-cast. (Root tracking is unaffected: a read-back reference
  is conservatively rooted at the container, which its true root is known to
  outlive.)

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

Compilation target: C/C++ first, LLVM later. Representation and calling
convention: Appendix C.

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
* Entry point: `fn main() { }`.

### 11.2 Concurrency

No shared mutable memory, ever. The model is a statically typed cousin of
the Linda tuple-space / coordination style.

* A worker entry point is declared `thread_fn worker(a: int, ...) { }`.
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
  `thread_spawn(worker, args…) -> int` reserves a fresh stack block, copies
  the args, starts the worker, and returns its id. `hardware_threads() ->
  int` exists for sizing. `thread_wait(id)` blocks until that worker
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
slices, `assert`, `hardware_threads`, the unsigned builtins of
§6.2, and the math types of §6.1 are assumed); FFI details (an `extern fn`
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
enum Node { Num { v: flt }, Add { l: Node..&<u16>, r: Node..&<u16> } }
recursive fn eval(n: Node.Num&) -> flt { n.v }
recursive fn eval(n: Node.Add&) -> flt { eval(n.l) + eval(n.r) }
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

0a. **Storage-to-storage transfer ergonomics** — reads widen to `int` and
    narrowing stores are never implicit, so `a.x = b.x` between two `u8`
    fields requires `as` even though no information can be lost. Loosening
    this for provably width-preserving field-to-field transfers needs a rule
    that doesn't reopen implicit narrowing in general.
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
    target's frame), and the recursive-cycle store rule (§7.8) rejects
    storing any non-global-rooted reference inside a cycle. (Container-read
    writability laundering, one-root-per-reference-variable, and the single
    agreed return root are now deliberate language rules, §9.2/§9.5.)

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
4. **`[>..<]` interior-reference relaxation** — flow-sensitive analysis to
   allow temporary references between shrinks.
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
    performance; consider opt-in aligned types if so. Related: f32-lane
    elementwise ops (§6.1) should vectorize; verify the 64-bit `int`
    semantics don't block it.
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
    relative references are currently rejected outright (§3.9); track the
    region an offset ranges over so whole-region copies (and serialization
    moves) can be proven safe.

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
* Reference: one pointer. Optional: 0 = null. Relative: signed offset of the
  declared width, relative to the offset field's own address; 0 = null.
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
  (§7.5), destination/target stack identities; the `return from`
  discriminant is a second return value (an int: 0 = normal, k = propagate
  to target k).

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
prim        := "int"|"flt"|"bool"|"varint"|"i8"|…|"u64"|"f32"|"f64"
             | "fn" ( "(" types? ")" ("->" (type | "(" types ")"))? )?
tyargs      := "<" type ("," type)* ","? ">"
uint        := "u8"|"u16"|"u32"|"u64"|"varint"
postfix     := "[" expr "]"                      // fixed array (const expr)
             | "[" uint? "]"                     // variable array
             | "[" ".." expr? "]"                // limited (const expr)
             | "[" ">" ".." "<"? "]"             // resizable
             | "[" ":" "]"                       // slice
             | "&" ("<" uint ">")?               // reference / relative
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
primary     := literal | ident | "null" | "(" expr ")"
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
