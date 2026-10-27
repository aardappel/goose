# Index types and bounds-check-free functions

<details>
<summary>Table of contents</summary>

- [1. The check no analysis removes](#1-the-check-no-analysis-removes)
- [2. Index and bound types](#2-index-and-bound-types)
- [3. Why it is sound](#3-why-it-is-sound)
- [4. What bounds-check elimination gets from it](#4-what-bounds-check-elimination-gets-from-it)
- [5. Index types and relative references](#5-index-types-and-relative-references)
- [6. `checkfree` functions](#6-checkfree-functions)
- [7. Worked example: CSR breadth-first search](#7-worked-example-csr-breadth-first-search)
- [8. Implementation](#8-implementation)
- [9. Testing](#9-testing)
- [10. Measurements](#10-measurements)
- [11. Open questions](#11-open-questions)

</details>

This proposal adds two features that are meant to ship together. An *index
type* `u32 in nodes` is an integer that is a valid index of the global array
`nodes` wherever it can be named, so indexing with it needs no check. A
`checkfree fn` is a function the compiler rejects if any bounds check is left
in it. Together they let a programmer move the bounds checks of a hot loop to
where its data enters the program, and have the compiler confirm that the
loop is left with none. This was TODO 0e of the spec, and it addresses the
indirect-indexing gap listed in TODO 0f. Both features are implemented
(spec §3.11 and §7.14). This note keeps the reasoning behind them; §8 to §10
record how they were built, what building them turned up, and what was
measured.

## 1. The check no analysis removes

The bounds-check pass (§10.5, `src/bce.h`) proves most checks in ordinary
loops. The common case it keeps is an index read out of another array:

```goose
fn relax(adj: i32[>..]&, dist: i32[>..]&, u: i64) {
    for k in adj.len {
        let w = adj[k];                              // elided
        if dist[w] < 0 { dist[w] = dist[u] + 1; }    // the first dist[w] is kept
    }
}
```

The same shape appears in graph edges (`dist[adj[k]]`), mesh index buffers
(`verts[tri.a]`), sort permutations (`vals[order[i]]`), union-find
(`parent[x]`) and handle tables. `goose --check --bce-lines` shows that
`bench/goose/graph_csr.goose` keeps a check per edge on `out[k]` and on
`dist[w]`, per vertex on `dist[u]` and on `start[u + 1]` (an `i32` sum,
which may wrap), and two in the build loop, on `cursor[s]` and
`out[cursor[s]]`.

Proving `dist[w]` requires `∀k: 0 <= adj[k] < dist.len`, a fact about the
*contents* of one array relative to the *length* of another. A smarter
analysis would not be enough:

* Where `adj` came from outside the program (an edge list or a mesh read from
  a file), the fact is false until something checks it. That check is the
  program's input validation. It can be moved, but not removed, and only the
  programmer knows where it belongs (once at load, or at every traversal).
* Where the program built `adj` itself, a proof needs an invariant over every
  write into `adj`, wherever `adj` is reachable. The invariant must relate
  those writes to a length that is often a run time value and belongs to an
  array allocated elsewhere. A summary of the range of values stored into
  each array would be enough for `graph_csr`, where the bound is a constant
  `V` and every pushed value is `% V`, but not for the general case.

Pool-relative references (§3.9) already cover part of this. With `adj:
Node&<u32 in nodes>[>..]&` and `dist` moved into `Node`, `relax` compiles
with no checks, because a link is validated where it is stored and a load
is `base + off - 1`. However, pool-relative references cannot reach a
*parallel* array: `dist`, `visited` and `parent` are indexed by the same
node id and are usually allocated per query, not part of the node.

## 2. Index and bound types

### 2.1 Syntax

```
type        := "const"? (prim | qname tyargs? | "(" type ")") postfix*
             | indextype
indextype   := ("u8" | "u16" | "u32" | "u64") ("in" | "upto") qname
```

`u32 in nodes` is an *index type* of `nodes`, `u32 upto edges` a *bound type*
of `edges`. An index type takes no postfix directly: an array of them is
`(u32 in nodes)[>..]`, a cast to one is `x as (u32 in nodes)`. An alias reads
better at every use and is the expected style:

```goose
type NodeId = u32 in nodes;
var edges: NodeId[>..] = [];
```

`upto` is a keyword only directly after an unsigned type name in a type, as
`in` already is inside `T&<u32 in pool>`. `qname` resolves as a pool's name
does, in the namespace of the declaration the type is written in.

### 2.2 What a value means

A value of `uN in a` is an integer `v` with `0 <= v < a.len`, at every point
of the program where it can be observed. A value of `uN upto a` has `0 <= v
<= a.len`: the end of a range, such as a CSR offset.

`a` names a global `var` or `reusable var` of a grow-only type `T[>..]` whose
elements are fixed-size, so that it can be indexed. Anything else is a
compile error: a local or parameter cannot be named in a field's type (as
for `in pool`), a grow-shrink or limited array can shrink, and a sequential
array cannot be indexed. Unlike `in pool`, the element type is unrestricted.

The array is part of the type's identity: `u32 in nodes` is neither `u32`
nor `u32 in edges`, and two widths of one array are different types. The
representation is the plain unsigned integer. The array is not stored, and
the type does not appear in the generated C.

### 2.3 Making one

* **A conversion.** `x as (uN in a)`, for `x` of any integer type, aborts
  unless `0 <= x < a.len`, with the ordinary index-out-of-bounds error that
  `a[x]` would give. `x as (uN upto a)` is the same with `x <= a.len`. If a
  value passes that check but does not fit in `uN`, it then converts as any
  `as` does: checked in debug and wrapped in release. The wrapped value is
  smaller, so it is still in range.
  * Unlike every other `as`, this one is checked in every build: it is a
    bounds check that has been moved, not a debug conversion check.
  * The bounds-check pass treats it as it treats `a[x]`, and elides it
    wherever the check of `a[x]` would be elided.
  * There is no unchecked form: `x as! (uN in a)` is an error.
* **A read.** Reading storage of the type (an element, a field, a variable)
  gives a value of the type, as does a call whose result has the type.
* **A loop.** `for i in lo..hi`, where both bounds are index or bound types
  of the same `a`, gives `i` the index type `uN in a` at the bounds' joined
  width. Every value it takes satisfies `lo <= i < hi <= a.len`. A count loop
  `for i in n` with `n` of an index or bound type of `a` gives the same.
* **Within one array's types.** A wider width of the same array converts
  implicitly (`u16 in a` to `u32 in a`), and so does an index type to the
  bound type of the same array (`u32 in a` to `u32 upto a`). The other
  directions, a narrower width and `upto` to `in`, take an `as` and are
  checked as above.
* **Nothing else.** A constant never has an index type and never adapts to
  one, since its value says nothing about `a.len`.

### 2.4 Using one

* **Indexing.** `a[v]`, and `&a[v]`, with `v` of an index type of `a` need no
  check, whatever the analysis knows. Indexing another array `b[v]` is
  checked unless the analysis proves `a.len <= b.len` (§4), which it does
  from a counted fill, a `resize`, an `assert` or call-site facts. A slice
  `a[lo..hi]` with bound-type ends still checks `lo <= hi` unless proven.
* **As a number.** It converts implicitly to its plain type, and from there
  wherever that converts (`u32 in a` to `u32`, `u64` and `i64`). It is
  compared, printed, hashed, passed to an `i64` parameter and used in
  arithmetic as that type.
* **Arithmetic** works on the plain value only. `w + 1`, `w << 3`, `w & m`,
  `-w` and `~w` are plain integers, and so is `w + 0`. Compound assignment and
  `++`/`--` on a place of an index type are errors, since their result would
  be a plain integer stored back.

### 2.5 How values pass through

* **Declarations.** A `let` or `var` with no written type takes the index
  type of its initializer. A later `x = 0` to such a `var` is then an error,
  and the message suggests writing the plain type (`var x: u32 = adj[k];`).
* **Calls.** Parameters and results may have index types, and arguments and
  `return` values are checked against them like any other type.
* **Branches.** An `if`, a `match`, a block or a `break` value has an index
  type where every branch value has an index or bound type of the same `a`.
  It takes the widest of the branches' widths, and is `upto` if any branch
  is. Otherwise it has the join of the plain types. `if c { 0 } else { w }`
  is a plain `u32`.
* **Storage.** Fields, ADT payloads, array elements, locals and globals may
  have index types, and every store into one needs a value of that type.
* **References and slices.** A reference to index-typed storage is `(u32 in
  a)&`, a slice of it `(u32 in a)[:]`. They are not `u32&` or `u32[:]`,
  since a write through those could store any `u32`. A `u32[:]` parameter
  does not accept a `NodeId[:]` argument.
* **Generics.** A type argument may be an index type, and is inferred as one.
  Each instantiation is checked with it (§7.7). A generic body that moves `T`
  values works (`swap_at`, `fill`, `sort`, `max`). A generic body that stores
  an arithmetic result into a `T` slot, or needs `default<T>()`, does not
  instantiate with an index type, and the error names the instantiation.
* **No null.** Goose's optionals are references. A "no link" value is an ADT
  (`enum Link { None, To { n: NodeId } }`) or a sentinel in plain storage
  that is converted at use.

### 2.6 What may not hold or make one

* **Defaults.** An index type has no default value: `default<T>()`, a
  literal's `..`, a field default and a global without an initializer all
  need one (`HasDefault`). The array may be empty, so not even 0 is valid. A
  local declared without an initializer is fine, since it must be assigned
  before it is read.
* **Byte writers.** `format` and `to_bytes(a, out)` reject an output array of
  an index type (`(u8 in x)[>..]`). `bytes_of` views are read-only already.
* **Images.** `from_bytes` accepts element types holding index types, and
  its verifier checks each such value against the named array's length at
  verification time, rejecting the image otherwise. This differs from `in
  pool` links, which it rejects (`design/serialization.md` §4): an index does
  not depend on the element layout, so checking it once at load is
  meaningful. Load time is where data from outside the program should be
  checked.
* **Threads.** Each thread program has its own copy of every global it uses
  (§11.2), and after the spawn the copies of `a` can grow to different
  lengths. `qput`, `qget`, `qpoll`, `qlimit` and `thread_spawn` arguments
  reject types that hold index types.
* **C.** An `extern fn` or `export fn` signature holds no index type at any
  depth, since C code is not checked against the array (`ExternValueOk`).
  By-value parameters to C would be harmless; v1 does not tell them apart.

### 2.7 The array never shrinks

An array that an index or bound type names is never shrunk. `pop`, `pop_n`,
`resize`, `clear` and a whole assignment of it are errors, anywhere in the
program, directly or through a reference parameter. The per-specialization
shrink summaries that §5.1 already keeps for globals find every such shrink.
The error names the index type that pins the array. A shrink through a
reference whose root only bounds the storage (§9.5) is rejected if any
array that root may reach has the type of a named array. This is
conservative. Element writes, `push`, `append` and a `reusable` pool's
`free` (whose slot stays a valid element, §5.4) are unaffected.

## 3. Why it is sound

The invariant is that every live value of `uN in a` is less than `a.len`
(`<=` for `upto`).

* **It is established** wherever a value is made: the conversion checks it, a
  loop's values lie between bounds that satisfy it, and a read gives what a
  store put there.
* **It is preserved** because:
  * `a.len` never decreases (§2.7). A grow-only global's element region also
    never moves.
  * every store into storage of the type needs a value of the type, which
    only §2.3 makes;
  * arithmetic yields plain integers (§2.4);
  * there is no default value and no byte-level write (§2.6);
  * images are verified, and values are not passed between threads and do
    not come from C.
* **Global initialization.** A conversion reads `a.len`, so the
  initialization order of globals (§11.1) treats it as a use of `a`.

Nothing here involves lifetimes. Rust needs generative lifetimes to keep a
branded index from outliving its container (the `indexing` crate), and Ada's
index subtypes (`array (Vertex) of T`) fix the bound when the type is
elaborated. In Goose, the grow-only/grow-shrink split gives a bound that
only grows, which is the same monotonicity §10.5 already relies on.

The contents of `a` can change freely, since only its length matters.

## 4. What bounds-check elimination gets from it

* **Facts from the type.** Any term whose expression has type `uN in a` gets
  `0 <= t <= len(a) - 1` at `a`'s *current* generation, and `upto` gets `t <=
  len(a)`. Using the current generation is always correct, because `len(a)`
  never decreases, so no bridge facts are involved. These facts are also
  attached to one-shot bases: a `for` range's end (`n.hi`) and a field
  read.
* **Turning a content fact into a length fact.** `dist[w]` with `w: u32 in
  nodes` reduces to `len(nodes) <= len(dist)`. That is one relation between
  two lengths, which the difference domain can already express and track.
  * The relation is lost in exactly the case where it should be: if `nodes`
    grows after `dist` was sized, the generation of `len(nodes)` moves on
    and the check stays.
* **The conversion** is an index check of `a` and is elided like any other
  index check of `a`: in `for v in nodes.len { edges.push(v as NodeId); }`
  no check remains.
* **Not yet: `index_of(r)` and `alloc_index(v)`.** Their results could get
  `0 <= t < len(recv)`, which they already satisfy, so that
  `a.index_of(r) as (u32 in a)` would need no check. Today that conversion
  is checked.
* **Typed kills.** The worked example (§7) turned up a gap the facts above
  depend on: a `q.clear()` through a reference parameter, or a call that
  pushes through one, killed every global's length, whatever its type. Each
  array has one type, and a reference reaches only an array of its own type
  (array kinds do not convert at a reference). So a growth or shrink now
  kills only places of the receiver's type, and a callee's write through an
  argument that is an array of scalars kills only places of that array's
  type (spec §10.5).

## 5. Index types and relative references

Both are links that are checked once, when stored, and trusted when used.
Both rest on the same argument: the target is a grow-only global, and
nothing live can observe it shrinking. They differ in what the link names. A
relative reference names an *element*: it can only be followed, and it is a
`T&` with a root, constness, `.=` relinking and a null that needs no extra
storage. An index names a *position* in a numbering: it is a number that
reaches several arrays and can be computed with, compared, sorted, hashed
and sent out of the process. With an index type, `&nodes[w]` turns an index
into a reference without a check.

| | `Node&<u32 in pool>` | `u32 in nodes` |
|---|---|---|
| Reaches | the one array it points into, or a field inside an element | `a` and any array the analysis knows is at least as long |
| Load | `base + off - 1`, one addressing mode | `base + i * sizeof(T)`, one addressing mode only for element sizes 1, 2, 4 and 8 |
| Null | the optional form, offset 0 | none: an ADT or a sentinel |
| Arithmetic, order, hashing | no (via `index_of`) | yes, as a plain number |
| `u16` covers | 64 KB of pool bytes | 65,535 elements of any size |
| Images | rejected by `from_bytes` | verified by `from_bytes` |
| Variable-size elements | yes (§3.3's index of links) | no: such an array cannot be indexed |
| Local or parameter arrays | self-relative form | no (v1) |

**Prefer relative references** where a link is only ever followed to one
element, and maybe relinked or null: linked lists and trees in a pool, map
slots pointing at nodes (`bench/goose/lru.goose`). **Prefer indices** where
the id is used as a number: to reach parallel arrays, to compute neighbours
or ranges (`i + W`, CSR offsets), to compare or sort, or to leave the
process.

A mesh is a case where both work and indices are the better choice. With
`verts: Vertex[>..]` of 32-byte vertices, `Vertex&<u32 in verts>` links in
`Tri` serve CPU code that reads positions. Indices are still better, for four
reasons. The index buffer goes to the GPU as it is. It loads from file
formats as it is, with `from_bytes` doing the only check. `u16` indices, the
norm below 65,536 vertices, would reach only 2,048 vertices as `u16 in pool`.
And per-vertex attributes added later (normals, skinning weights, SoA columns
for SIMD) are parallel arrays the same index reaches.

## 6. `checkfree` functions

### 6.1 Meaning

```
fndecl := ("export" strlit? | "extern" strlit?)? "checkfree"? "inline"? "simd"? "recursive"?
          ("fn" | "thread_fn") ...
```

`checkfree fn f` is a compile error if a bounds check is left anywhere in
`f`'s body after bounds-check elimination. That covers every copy of the
body: each specialization, each `simd` version and each inlined copy. The
annotation changes nothing about what the program computes or how it is
compiled. `checkfree` is a keyword only directly before `inline`, `simd`,
`recursive`, `fn` or `thread_fn`, so it stays usable as a name.

*Bounds checks* are the index and slice checks of §9.3, conversions into
index and bound types (§2.3), and `free(i)`'s index check. The other aborts
of §9.3 are not:
* limited-array capacity;
* shrinking below empty;
* debug overflow and debug `as` checks;
* division by zero;
* relative-reference offset overflow;
* `assert` and `abort`.

`assert` in particular is how a `checkfree` function states its
preconditions once, outside its loops.

### 6.2 Calls

Every call in a `checkfree` body is to a `checkfree` function, a builtin or
an `extern`.
* **Indirect calls.** A call through a function value counts as a call to
  each function it may be. `print`, `str` and `format` count as calls to the
  user `format` overloads they reach.
* **Nested code.** Nested functions declared in a `checkfree` function are
  `checkfree` themselves. A trailing block is part of the body, and the
  function it is passed to must be `checkfree` too.
* **No inlining exception.** `inline` does not exempt a callee. Inlining is a
  request the implementation may not grant (§7.13, and never at `-O0`), and
  a call that stays a call runs the callee's own checks inside the loop.

So the guarantee is transitive: no bounds check runs while a `checkfree`
function is executing. Small helpers called from hot loops (`swap`, `min`,
`max`, vector math) are expected to be `checkfree` themselves.

### 6.3 Where its facts come from

A function's entry facts come from its call sites (§10.5). A `checkfree`
function relying on them can stop compiling when a new caller elsewhere
fails to establish them. In that case the error names that call site and the
fact it does not establish. To keep a function independent of its callers, use:
* index types in the signature;
* entry `assert`s for the length relations the body needs, like
  `assert(dist.len >= nodes.len)`.

Each is one check per call, outside the loop, and reads as documentation of
the precondition.

### 6.4 What every implementation must prove

§10.5 says that nothing about a program's meaning depends on how much the
analysis proves. With `checkfree`, whether a program compiles does depend on
it, so the spec fixes a *core*: checks every conforming implementation must
elide inside a `checkfree` function. Accepting anything beyond the core is
implementation-defined. A program written to the core compiles under every
version of the analysis.

A *kill* of a fact about `x` is any of the following between the fact and
the check: a write to `x`, a shrink of an array in the fact, a growth of the
array an index type names, or a call whose effect summary says it may do
one of these. The core:

1. A constant index into a fixed array, in range, and an index whose type's
   whole range fits a fixed array (a `u8` into a `[256]`).
2. An index of an index type of `a`, into `a`.
3. An index of an index type of `a` into `b`, where `b.len >= a.len` (or
   `==`) was asserted or established by a counted fill, with no kill in
   between.
4. A `for` index into the array its count or range end is the length of
   (`for i in a.len`, `for x, i in a`, `for i in lo..a.len` with `lo >= 0`
   known), with no shrink of `a` in the body.
5. `a[i]` where a condition (`while`, `if`, `guard` or `assert`) established
   `i < a.len`, with no kill in between, for an `i` known to be non-negative:
   an unsigned type, an index type, or a local that starts non-negative and
   is written only by `++` and `+=` of a non-negative constant.
6. A check identical to one already passed on the same path, with no kill in
   between.

Everything else the analysis proves (products, reductions with `%` and `&`,
facts from callers, effect summaries, step counters) is best-effort.

### 6.5 Diagnostics

There is one error per remaining check, reported at its index with the
source line. The error says why the check remains, in the terms of
`docs/implementation.md` §9.1's "Kept" list. The implemented reasons are:

* **A value read out of an array, a field or a call.** The error suggests
  an index type for what is stored there.
* **A `u64` index variable.**
* **A term the domain cannot relate to the length**, or a length it does
  not track there.
* **Which bound failed:** nothing shows `x >= 0`, or nothing shows
  `x < a.len`. For an index type of another array the error names the
  `assert` that would settle it:

  ```
  checkfree fn bfs keeps a bounds check: nothing shows u < dist.len; u is a
  u32 in nodes, so `assert(dist.len >= nodes.len)` where neither changes
  after it would settle it
  ```
* **Slices:** which of the three comparisons failed.

Two reasons are not implemented yet, since they need the pass to keep
provenance it does not keep today:
* **A length that was invalidated:** `dist`'s length was invalidated by the
  call to `f` at line N, or by `q.pop()`. That needs the statement behind
  each generation of a length base.
* **A fact that came from callers:** the call at file:line passes `src`
  without `src.len == W * W`. That needs the pass to know which site's meet
  dropped an entry fact.

### 6.6 Flags and combinations

* **Flags.**
  * Under `--no-bce`, `checkfree` is not verified. One warning says so, and
    every check is present as usual.
  * Debug and release builds run the same analysis and get the same verdict.
  * At `-O0` nothing is inlined. A function whose checks are settled by its
    own types and asserts gets the same verdict there. The worked example
    passes at `-O0`, `-O1` and `-O2`, also with an inlined helper.
* **Combinations.**
  * `checkfree` is an error with `extern`, which has no body.
  * It may be combined with `inline`, `simd`, `recursive`, `thread_fn` and
    `export`.
  * A thread entry and an export get no call-site facts, which the core
    never relies on anyway (§8.3).

## 7. Worked example: CSR breadth-first search

This is `graph_csr.goose` written with index types. The graph is global and
built once. The traversal does no check per edge and none per vertex.

```goose
struct Node { lo: u32 upto edges, hi: u32 upto edges }

var nodes: Node[>..] = [];
type NodeId = u32 in nodes;
var edges: NodeId[>..] = [];

// Builds the graph from an edge list nothing has validated: these checks are
// the only ones the traversal will ever need.
fn build(nv: i64, src: i32[:], dst: i32[:]) {
    let empty = 0 as (u32 upto edges);            // 0 <= edges.len always
    for v in nv { nodes.push(Node { lo: empty, hi: empty }); }
    var first: i64[>..] = [];                     // CSR offsets
    for v in nv + 1 { first.push(0); }
    for e in src.len { first[src[e] + 1]++; }
    for v in nv { first[v + 1] += first[v]; }
    var next: i64[>..] = [];
    for v in nv { next.push(first[v]); }
    let some = 0 as NodeId;                       // aborts for an empty graph
    for e in dst.len { edges.push(some); }
    for e in src.len {
        let s = src[e];
        edges[next[s]] = dst[e] as NodeId;        // one check per edge, here
        next[s]++;
    }
    for v in nv {
        nodes[v].lo = first[v] as (u32 upto edges);
        nodes[v].hi = first[v + 1] as (u32 upto edges);
    }
}

// The sum of distances from src; dist holds an entry per node.
checkfree fn bfs(dist: i32[>..]&, q: NodeId[>..<]&, src: NodeId) -> i64 {
    assert(dist.len >= nodes.len);
    var total = 0;
    q.clear();
    dist[src] = 0;                     // core 3
    q.push(src);
    var qh = 0;
    while qh < q.len {
        let u = q[qh];                 // core 5; u: NodeId
        qh++;
        let d = dist[u];               // core 3
        total += d;
        let n = nodes[u];              // core 2
        for k in n.lo..n.hi {          // k: u32 in edges (§2.3)
            let w = edges[k];          // core 2; w: NodeId
            if dist[w] < 0 {           // core 3
                dist[w] = d + 1;       // core 6
                q.push(w);
            }
        }
    }
    return total;
}
```

Notes on the example:
* **Core 3 holds through the loop** because nothing in it grows `nodes` or
  shrinks `dist`. `q` is a different array of a different type.
* **`build` keeps its checks.** They run on input data, at build time, once
  per edge and vertex.
* **The order in `build` matters.** The nodes exist before the edges that
  name them, and the offsets are set once the edges are all there.
* **The graph can't be rebuilt** in the same run. `edges` can never be
  cleared (§2.7), which suits a graph loaded once. A graph rebuilt per phase
  needs the grow-shrink extension of §10.

## 8. Implementation

`docs/implementation.md` §3.13 and §5.5 to §5.12 describe the implementation
pass by pass. This section records how it went against the plan.

### 8.1 The plan held

Every typed store already goes through `TypeCheck::FitsAt`, which is also
where `in pool` checks roots. Making `FitsAt` accept only a value of the
same array therefore covered nearly every way an element can be written.
The risk was integer code that picks a result type by storage width, or by
copying an operand's `TypeExpr *`. As planned, two choke points made such
code fail closed: the brand is stripped in `TypeCheck::Operand`, which every
operator operand passes through, and every branded destination or join
requires the same array. The sites that copied a type along were the ones
predicted: `MergeVals`' constant `adapt`, `UnifyBranch`, `FitsAt`'s
constant and literal cases, and `Converts` for the cast judgements. The
brand stayed a field on `TY_INT` rather than a kind of its own.

### 8.2 What the plan missed

* **The optimizer folds casts.** `AsCast::Opt` turned `5 as NodeId` into the
  constant 5, dropping the check. A conversion into an index type now never
  folds.
* **Two codegen paths look inside casts.** `DivisorKey` and `DivisorValue`
  hoist a divisor's conversion checks ahead of a loop, and `Speculatable`
  evaluates a cast unconditionally where it cannot fail. Both now treat a
  conversion into an index type as the check it is.
* **The worked example needed typed kills** (§4): without them the fact from
  the entry `assert` of `bfs` was killed at `q.clear()`.
* **Attributing a check to its text.** The inliner places an argument
  expression inside the callee's block, so tracking which function a walk is
  in attributed the argument's checks to the callee. Source-line spans, as
  `--bce-test` uses, attribute every copy correctly.
* **A dump of a `for` binder** needs the same parentheses that the parser
  requires.

### 8.3 A soundness bug found on the way

The `checkfree_parallel` fixture first used an `export fn`, and the check
that should have stayed was elided. The facts of a Goose caller were granted
to an `export fn`'s entry, but its C wrapper calls the same specialization
with arguments nobody checked. For example,
`export fn api(d: i32[:], i: i64) -> i32 { d[i] }` called once from Goose
with a known length and index compiled without its check, and C could then
read past the end. This predated index types. It is fixed separately
(2f7e1b6b): an export, like a thread entry, gets no call-site facts.

### 8.4 Size

The estimate was 400–600 lines. The change was +685 −44 in 19 source files,
including the typed kills and `checkfree`'s messages, plus 28 rejection
fixtures, three parse-error fixtures and six programs with blessed output.

## 9. Testing

* **Rejections** (`test/errors_tc`, one per rule; `test/errors` for the
  parse errors):
  * a constant, an arithmetic result, compound assignment and `++` into an
    index type; a branch joining one with a constant;
  * one array's index into another's slot, and a bound into an index slot;
  * a view of index-typed storage as plain integers;
  * `default<NodeId>()` and a literal's `..`;
  * `as!` into one, and a float converted to one;
  * `format` into `(u8 in x)[>..]`, `qput`, a thread argument, an `extern`
    result;
  * each shrink of a named array, `pop`, `clear` and whole assignment, and
    one through a reference parameter;
  * an invalid named array: a local, a grow-shrink array, a `let`, a
    sequential array; a signed index type; the missing parentheses;
  * `checkfree`: a call to an unannotated function, a block written outside
    one, an index read out of an array, an index of another array without
    the length relation, and `checkfree extern`.
* **Programs** (blessed output, run at -O0 and -O2, natively and through the
  JIT):
  * `test/typing/index_types.goose`: loads, joins, widths, calls, ADT
    payloads, generic code (`sort`, `swap_at`, `max`), `for` binders, and
    `from_bytes` accepting and rejecting images;
  * `test/optimizer/bce_index_types.goose`: what the types prove, including
    the keep after `nodes` grows past where `dist` was sized;
  * `test/optimizer/bce_typed_kills.goose`: kills of same- and
    other-typed arrays, by the operation and through a callee;
  * `test/optimizer/checkfree_bfs.goose`: §7 with an inlined `checkfree`
    helper, checked against a plain-integer version of the same BFS;
  * `test/codegen/index_type_abort.goose` and `bound_type_abort.goose`: the
    conversions' aborts and their messages.
* **Fuzzing.** `bench/bce_fuzz.py` (400 programs, seed 7) still finds no
  mismatch with the typed kills. It does not generate index-typed shapes
  yet.

## 10. Measurements

The programs were built by `goose build` with clang-cl 22 for x64 at its
default target and run on the development machine. The figures are the best
and median of 9–11 interleaved runs. These are rough numbers, not a
benchmark-suite row.

* **The BFS of §7** at a million nodes, eight million edges and eight
  sources, compared with the same program using plain `u32` storage and the
  same layout: 0.606 s against 0.628 s for the searches (medians, build time
  subtracted). That is within noise. The walk is memory-bound, and the
  checks it drops are well-predicted branches, as `docs/implementation.md`
  §5.12 found for an earlier analysis of array contents.
* **A gather kernel**, `out[i] = verts[idx[i]] * 2.0 + 1.0` over four
  million indices, 50 passes: 0.220 s against 0.232 s (best), 0.224 s
  against 0.247 s (median), so 5–9% for the whole run.
* An earlier draft of the comparison used `i64` fields and edges for the
  plain version and measured 25% in favour of index types. That difference
  came from the halved memory traffic of the narrower layout, which the
  plain version can use too. Index types are not a reason to narrow the
  layout, but they make the narrow layout as safe to index as the wide one.

So on clang, the value of index types lies more in the guarantee, and in
moving the check to where data enters the program, than in speed. Two
setups are untested: MSVC, where a kept check prevents vectorization
outright (`blur` was 1.75x slower with nine checks), and wider instruction
sets with gather instructions.

## 11. Open questions

* **Names.** `checkfree` overpromises slightly, since other aborts remain.
  `proven` and `boundsfree` are alternatives. Names that read as permission
  are ruled out (Odin's `#no_bounds_check` and Julia's `@inbounds` disable
  checks unsafely). `upto` might instead be `in ..= a`.
* **`var` inference.** Should a `var` without a written type take the plain
  type, so that reassigning it works? `var u = q[qh]` would then lose the
  index type, and every loop that walks with a `var` would need the type
  written out.
* **Locals and parameters as `a`.** A local array could be named in local
  declarations, and a reference parameter's array settled per call site as
  pool parameters' are (§3.9). The obstacle is only a field's type, which
  cannot name a local.
* **Shrinkable arrays as `a`.** A grow-shrink array could shrink where no
  index value of it is live, as §5.2 does for references. That is what a
  graph rebuilt per phase needs.
* **Signed index types.** `i64 in a` would let `for i in a.len` and `for x, i
  in a` give index types without a cast. Giving existing loops new types
  changes which generic instantiations existing programs make, so v1 does
  not.
* **Read-only views.** A `const u32[:]` could accept a `NodeId[:]`, since
  nothing writes through it. That needs a subtyping rule `TypeEq` does not
  have.
* **An optional form** biased by one, as `in pool` is, would give a null
  that needs no extra storage.
* **Thread spawn arguments** are probably safe, since the worker's copy of
  `a` is taken at the spawn, provided the worker uses `a`. v1 rejects them.
* **`index_of` and `alloc_index`** could return index types instead of
  `i64`. That would break code doing arithmetic on the result in place, so
  the plan is instead to add the fact described in §4, which is not
  implemented yet.
* **The two missing diagnostics** of §6.5.
* **Index-typed shapes in `bench/bce_fuzz.py`**: loads, joins, generic
  pass-through and growth between sizing and use.
* **Fixed arrays as `a`** (`u8 in table` for a `T[256]`) are sound trivially
  and mostly cover what the type's range already gives.
