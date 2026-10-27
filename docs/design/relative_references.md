# Relative references: self-relative or base-relative?

<details>
<summary>Table of contents</summary>

- [1. What each encoding means](#1-what-each-encoding-means)
- [2. Speed](#2-speed)
- [3. Expressivity](#3-expressivity)
- [4. Compiler complexity](#4-compiler-complexity)
- [5. Options](#5-options)
- [Recommendation](#recommendation)
- [Outcome](#outcome)

</details>

A relative reference (`T&<u8>`, `T&<u16>`, `T&<u32>`, `T&<u64>`, `T&<varint>`,
spec §3.9) is a reference stored as a narrow offset. This note compares two
encodings. At the time of the experiment, the spec defined the first; the second
was built behind a flag on the branch `opt/pool-relative` and measured
(`bench/adoption.md` 3.1). The comparison covers semantics, cost, and supported
uses. The Outcome section records the subsequent implementation of both forms.
The numbers are from the benchmark suite at the `large` size unless stated
otherwise.

## 1. What each encoding means

**Self-relative** (the original form): the stored value is `target - &field`,
the distance from the field's own address to the target, signed. Loading is
`&field + offset`; storing needs both addresses. Zero means null for the
optional form. The invariant the checker enforces is that the target lies in
the same root array as the field (§3.9). This invariant is what makes a
structure linked this way *position independent*: if the whole array is
moved or mapped anywhere, every link still holds, because both ends moved
together.

**Base-relative**: the stored value is `target - base(P)` where `P` is the
pool the target lives in, plus one so that zero stays null. Loading is
`base(P) + offset - 1`; storing is `target - base(P)`. The invariant is that
the target lies in `P`, and that `P` is known wherever the value is stored or
loaded. A structure linked this way is position independent *as a pool*: if
the pool is mapped anywhere, the links still hold, because they are measured
from the pool's start. The value is effectively an index: `&P[i]` encodes as
`i * sizeof(T)` without involving the base, and the index is recovered with
one shift.

The two agree on width, on null, on what may be pointed at, and on the
serialization of a whole array. They differ in what the address arithmetic
needs to know (the field's address, or the pool's base), and that difference
explains every row below.

## 2. Speed

The baseline compiler was measured against the flag, on both toolchains
(v145 / clang):

| row | base-relative vs self-relative | why |
|---|---:|---|
| lru (relinks six links per hit, converts index to reference and back) | 1.04 / **1.41** | store needs no field address and no range check; `&pool[i]` is `i * 16` |
| graph (one-pass linked adjacency, pointer chase) | 1.03 / 1.18 | load is `base + off` with `base` loop-invariant in a register |
| bintrees, tree, interp, scene (build once, walk) | 0.93-0.96 / 0.93-1.00 | a hot recursion had to carry the base as a hidden argument |
| calc | does not compile | a plain reference read back out of a container has no base (§4 below) |

Per operation, on the pointer-chasing path:

* **Load.** Self-relative: `p = &n->next; p += *(int32_t *)p`, two dependent
  operations on the address that was just computed. Base-relative:
  `p = base + *(int32_t *)&n->next - 1`, one load and one add against a value
  that was in a register before the loop started. clang schedules the second
  form better; v145 does not, which is the whole difference between the two
  columns.
* **Store.** Self-relative: `*(int32_t *)&n->next = (int32_t)(t - &n->next)`,
  a subtraction of two addresses, plus the range check where the reservation
  exceeds the width (gone since the 2 GB harness reservation for `u32`,
  §3.9). Base-relative: `t - base`, or for `&pool[i]`, just `i * 16`.
* **Index conversions.** Any code that keeps indices elsewhere (`lru`'s map,
  every arena-style workload) converts at each use. Base-relative makes the
  conversion a shift; self-relative makes it a subtraction against the
  field's address. This is where `lru`'s 1.41x comes from, not from the
  chase itself.
* **Hidden base.** Where the base is not a global's constant, a local, or a
  fat reference's header, base-relative has to pass it as a hidden argument
  on every call whose reference parameter's class is not pinned to one pool.
  This slows the recursion in `bintrees` and `tree` by 4-7%. Self-relative
  needs no hidden argument.

Over the suite, the net effect is small, and its direction depends on the
workload: relink-heavy rows gain, build-once rows lose a few percent, and
walkers are level. Under clang the gains are large; under v145 they are
within noise except for `lru`.

## 3. Expressivity

* **What a width bounds.** Self-relative: the *distance* between field and
  target. A `u16` link is fine in a multi-megabyte pool as long as the two
  ends are within 32 KB of each other, which is what `calc` relies on (2-byte
  links in a per-input tree that is never far from itself). Base-relative:
  the *size of the pool*. A `u16` link caps the pool at 64 KB, whatever the
  distance; `calc`'s per-input pool fits, but a `u16` link in `sexp`'s pool
  would not. Narrow widths therefore mean different things, and code written for
  one encoding can be wrong under the other without any type changing.
* **Cross-array links.** Self-relative *could* encode a link from one array
  into another (the arithmetic does not care), and the spec forbids it
  (§3.9) so that a structure stays a self-contained blob. Base-relative can
  link from anywhere into a *named* pool (for example, a map slot in `slots`
  that points into `pool` using 4 bytes), and the blob property becomes
  "relative to that pool", which is the only sensible meaning for such a
  link. This is the one thing self-relative cannot express. Today `lru`'s map
  holds indices, because a 4-byte reference from `slots` into `pool` cannot
  be a self-relative reference.
* **Position independence and copies.** Whole-array moves, maps and saves
  work for both. Copying a *sub-region* to another place (spec TODO 16)
  preserves self-relative links whose targets are inside the region, with no
  other work needed. Base-relative links need rebasing unless the region
  lands at the same offset in a pool of the same shape. Threads' queues
  (TODO 9) raise the same question.
* **Who must know the pool.** Self-relative needs to know nothing at load
  time, because the field's address is already available. Base-relative
  needs the target's pool at every load and store, and the checker must know
  that pool *exactly*. Root tracking provides it for `&pool[i]`, for `push`
  results, and for parameters whose class is one pool. With the read-back
  rule of §9.5, it also provides it for a reference read back out of a
  container when only one variable in scope can hold the reference's type.
  It cannot provide it when two pools of the same element type are in scope,
  or when the container came from a caller. Self-relative works in all of
  those cases, since it only needs the lifetime bound.
* **References in the map, measured.** With exact read-back roots, `lru`
  can hold `Node&?` in its map slots and relink through them (measured as
  `lru_refslots.goose`, since removed). Under self-relative this is *slower*
  than the index form: 1.11-1.13x under v145 and 1.09x under clang. The
  slots double to 16 bytes, each node carries its index for `free`, and the
  relative stores still subtract the field address. This is the shape
  base-relative would make cheap, since a `Node&<u32>` into `pool` from a
  slot is then the index itself.
* **Sentinels, self, null.** These are the same in both: `self` stores
  `-(field offset)` in one and `-(offset of the value in the pool)` in the
  other, and zero is null in both.
* **Reading the value as data.** A base-relative offset is meaningful on its
  own: it is the element's position in the pool, so it can be printed,
  hashed, compared across nodes, or used as an index without any address
  arithmetic. A self-relative offset means nothing without its own address.

## 4. Compiler complexity

* **Self-relative** is implemented. Its only costs are root tracking, which
  the checker does anyway for lifetimes, and a store-time check that is now
  elided where the width covers the reservation.
* **Base-relative** needs three things the compiler did not have, all built
  on the flag branch (`+405 -41` in `src/`):
  * a way to resolve a root to a base expression in codegen (a global's
    stack, a local's, a fat reference's header, or a hidden argument
    threaded through calls whose parameter class is not pinned to one pool);
  * a checker fixpoint that determines which parameter classes stand for
    exactly one pool at every call site;
  * the exactness bit on roots, so that a reference read back out of a
    container is never resolved against the wrong base.

  The third item is worth having for its own sake and is now being done for
  the spec's rooting rule; the first two exist only to serve this encoding.
  Every future feature that moves data (sub-region copies, queues, save and
  load) has to know about rebasing.
* **Both, per field** needs the syntax and a second code path in the
  relative store and load emitters, but no new analysis beyond the exactness
  bit. A base-relative field is only legal where its pool's base is known,
  and the checker already knows whether it is.

## 5. Options

**A. Keep self-relative only.** This is the simplest option. It is position
independent per array, works with inexact roots, and the store-time check is
gone. It leaves `lru` 1.45x behind the Rust arena under v145 and 1.19x
behind under clang. It also leaves "4-byte link from a map into a pool"
inexpressible, so such programs keep indices, which is also what the Rust and
C++ arenas do.

**B. Switch to base-relative.** This is the fastest option for relink and
index-conversion workloads, and it gives cross-array links and offsets that
are indices. It adds hidden-argument overhead to recursion, changes what
every narrow width means, makes copies and queues need rebasing, and makes a
reference read back out of a container unusable wherever two pools of its
type are in scope. The last point is a language-level restriction the
current encoding does not have.

**C. Both, chosen per field.** Self-relative stays the default and the only
form without a named pool. A field may instead be declared relative to a
named pool (a global pool, or a parameter or local that is a pool). Stores
and loads for that field then use that pool's base, cross-array links are
allowed into that pool, and the width bounds the pool. The checker admits a
store into such a field only from a reference whose root is exactly that
pool. This option needs the syntax, two emitter paths, and the exactness
rule. It needs no fixpoint and no hidden arguments, because the pool is
named at the declaration rather than discovered per call site. `lru`'s map
would hold `Node&<u32 in pool>` in 8-byte slots and relink through them,
and every build-once structure would stay self-relative at no extra cost.

**D. Compiler's choice per array.** This is not viable: the encoding changes
what a declared width means, so it has to be visible in the source.

## Recommendation

Option C preserves existing programs and their meaning while allowing links
between arrays. It improves relinking against a named pool without adding
overhead to recursion elsewhere. The only new analysis it needs, exact roots
for read-back references, is the rooting change already in progress. The
syntax is the open question. `T&<u32 in
pool>` reads naturally for a global pool. For a local or parameter pool, the
name is in scope at the declaration of the field's struct only when the
struct is declared inside the function. This suggests that the pool-named
form is mostly a global-pool feature, and that is where the relink-heavy
structures live.

## Outcome

Option C was implemented: `T&<u32 in pool>` where `pool` is a global
grow-only array (spec §3.9), plus `a.index_of(r)` (§3.3). The pool is named
at the field's declaration and is part of the type, so a store measures from
that global's base and a load is rooted exactly there. There is no fixpoint
over parameter classes and there are no hidden arguments. A call site that
passes a reference rooted in a different pool is a different specialization,
as every other root difference already is. The `in pool` form is restricted
to global pools, which is where this note expected the relink-heavy
structures to be. A local or parameter pool keeps the self-relative form.
This is why `graph` stays self-relative (its pool is a local of `main`), as
intended.

`lru` was rewritten to use it. The map holds `Node&<u32 in pool>?` in 8-byte
slots and relinks through them, `pool.free(pool.index_of(n))` turns a
reference back into a slot to free it, and the pool's base is held in one
local per function. The index form is kept as `lru_indices.goose`. Compared
with the index form, the reference form is level under v145 and 1.31x
faster under clang. No other benchmark's generated C changed. In the suite,
the row is 3,255/2,133 ms, against 2,237 for the Rust arena and 2,413/2,261
for the C++ arena. That is level with both under clang, and 1.45x behind
under v145, where the base-plus-offset load is scheduled worse.
