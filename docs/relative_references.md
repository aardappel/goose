# Relative references: self-relative or base-relative?

A relative reference (`T&<u8>`, `T&<u16>`, `T&<u32>`, `T&<u64>`, `T&<varint>`,
spec §3.9) is a reference stored as a narrow offset. Two encodings are on the
table. The spec defines the first; the second was built behind a flag on the
branch `opt/pool-relative` and measured (`bench/adoption.md` 3.1). This note
states what each one means, what it costs and what it can express, and ends
with a recommendation. The numbers are from the benchmark suite at the
`large` size unless stated otherwise.

## 1. What each encoding means

**Self-relative** (the spec today): the stored value is `target - &field`,
the distance from the field's own address to the target, signed. Loading is
`&field + offset`; storing needs both addresses. Zero means null for the
optional form. The invariant the checker enforces is that the target lies in
the same root array as the field (§3.9), which is what makes a structure
linked this way *position independent*: move or map the whole array anywhere
and every link still holds, because both ends moved together.

**Base-relative**: the stored value is `target - base(P)` where `P` is the
pool the target lives in, plus one so that zero stays null. Loading is
`base(P) + offset - 1`; storing is `target - base(P)`. The invariant is that
the target lies in `P`, and that `P` is known wherever the value is stored or
loaded. A structure linked this way is position independent *as a pool*: map
the pool anywhere and the links hold, because they are measured from its
start. The value is an index in disguise: `&P[i]` encodes as `i * sizeof(T)`
with no base in sight, and the index is recovered by one shift.

The two agree on width, on null, on what may be pointed at, and on the
serialization story for a whole array. They differ in what the address
arithmetic needs to know (the field's address, or the pool's base), and that
one difference is behind every row below.

## 2. Speed

Measured, baseline compiler against the flag, both toolchains (v145 / clang):

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
  fat reference's header, base-relative has to carry it: a hidden argument
  on every call whose reference parameter's class is not pinned to one pool.
  `bintrees` and `tree` pay 4-7% for it on their recursion. Self-relative
  carries nothing.

The net over the suite is small and one-sided by workload: relink-heavy rows
gain, build-once rows lose a few percent, walkers are level. Under clang the
gains are large; under v145 they are within noise except `lru`.

## 3. Expressivity

* **What a width bounds.** Self-relative: the *distance* between field and
  target. A `u16` link is fine in a multi-megabyte pool as long as the two
  ends are within 32 KB of each other, which is what `calc` relies on (2-byte
  links in a per-input tree that is never far from itself). Base-relative:
  the *size of the pool*. A `u16` link caps the pool at 64 KB, whatever the
  distance; `calc`'s per-input pool fits, a `u16` link in `sexp`'s pool would
  not. Narrow widths therefore mean different things, and code written for
  one encoding can be wrong under the other without any type changing.
* **Cross-array links.** Self-relative *could* encode a link from one array
  into another (the arithmetic does not care), and the spec forbids it
  (§3.9) so that a structure stays a self-contained blob. Base-relative can
  link from anywhere into a *named* pool -- a map slot in `slots` pointing
  into `pool` with 4 bytes -- and the blob property becomes "relative to that
  pool", which is the only sensible meaning for such a link. This is the one
  thing self-relative cannot express at all: today `lru`'s map holds indices
  because a 4-byte reference from `slots` into `pool` is not a self-relative
  reference.
* **Position independence and copies.** Whole-array moves, maps and saves
  work for both. Copying a *sub-region* to another place (spec TODO 16) keeps
  self-relative links whose targets are inside the region and needs nothing
  else; base-relative links need rebasing unless the region lands at the
  same offset in a pool of the same shape. Threads' queues (TODO 9) are the
  same question.
* **Who must know the pool.** Self-relative needs to know nothing at load
  time: the field's address is in hand. Base-relative needs the target's
  pool at every load and store, which the checker must know *exactly*. Root
  tracking gives that for `&pool[i]`, for `push` results, for parameters
  whose class is one pool, and, with the read-back rule of §9.5, for a
  reference read back out of a container when one variable in scope can
  hold its type; it cannot give it when two pools of the same element type
  are in scope, or when the container came from a caller. Self-relative
  works in all of those cases, since it only needs the lifetime bound.
* **References in the map, measured.** With exact read-back roots, `lru`
  can hold `Node&?` in its map slots and relink through them
  (`bench/goose/lru_refslots.goose`). Under self-relative it is *slower*
  than the index form, 1.11-1.13x under v145 and 1.09x under clang: the
  slots double to 16 bytes, each node carries its index for `free`, and the
  relative stores still subtract the field address. This is the shape
  base-relative would make cheap, since a `Node&<u32>` into `pool` from a
  slot is then the index itself.
* **Sentinels, self, null.** Identical: `self` stores `-(field offset)` in
  one and `-(offset of the value in the pool)` in the other; zero is null in
  both.
* **Reading the value as data.** A base-relative offset is meaningful on its
  own -- it is the element's position in the pool, so it can be printed,
  hashed, compared across nodes, or used as an index without any address
  arithmetic. A self-relative offset means nothing without its own address.

## 4. Compiler complexity

* **Self-relative** is implemented. Its whole cost is root tracking, which the
  checker does anyway for lifetimes, plus a store-time check that is now
  elided where the width covers the reservation.
* **Base-relative** needs three things the compiler did not have, all built
  on the flag branch (`+405 -41` in `src/`): a way to resolve a root to a base
  expression in codegen (a global's stack, a local's, a fat reference's
  header, or a hidden argument threaded through calls whose parameter class
  is not pinned to one pool); a checker fixpoint saying which parameter
  classes stand for exactly one pool at every call site; and the exactness
  bit on roots, so that a reference read back out of a container is never
  resolved against the wrong base. The third item is worth having for its
  own sake and is now being done for the spec's rooting rule; the first two
  exist only to serve this encoding. Every future feature that moves data --
  sub-region copies, queues, save and load -- has to know about rebasing.
* **Both, per field** costs the syntax and a second code path in the relative
  store and load emitters, but no new analysis beyond the exactness bit: a
  base-relative field is only legal where its pool's base is known, and the
  checker already knows whether it is.

## 5. Options

**A. Keep self-relative only.** Simplest, position independent per array,
works with inexact roots, and the store-time check is gone. Leaves `lru`
1.45x behind the Rust arena under v145 and 1.19x under clang, and leaves
"4-byte link from a map into a pool" inexpressible: such programs keep
indices, which is also what the Rust and C++ arenas do.

**B. Switch to base-relative.** Fastest for relink and index-conversion
workloads, gives cross-array links, and offsets that are indices. Costs the
hidden-argument tax on recursion, changes what every narrow width means,
makes copies and queues need rebasing, and makes a reference read back out of
a container unusable wherever two pools of its type are in scope. The last
point is a language-level restriction the current encoding does not have.

**C. Both, chosen per field.** Self-relative stays the default and the only
form without a named pool. A field may instead be declared relative to a
named pool -- a global pool, or a parameter or local that is a pool -- and
then stores and loads use that pool's base, cross-array links are allowed
into that pool, and the width bounds the pool. The checker admits a store
into such a field only from a reference whose root is exactly that pool.
Costs: the syntax, two emitter paths, and the exactness rule; no fixpoint and
no hidden arguments, because the pool is named at the declaration rather
than discovered per call site. `lru`'s map would hold `Node&<u32 in pool>`
in 8-byte slots and relink through them; every build-once structure would
stay self-relative and pay nothing.

**D. Compiler's choice per array.** Not viable: the encoding changes what a
declared width means, so it has to be visible in the source.

## Recommendation

C. It keeps every current program and its meaning, adds the one thing
self-relative cannot say, and lands the speed where it is wanted (relinking
against a named pool) without taxing recursion elsewhere. The only new
analysis it needs -- exact roots for read-back references -- is the rooting
change already in progress. The syntax is the open question; `T&<u32 in
pool>` reads naturally for a global pool, and for a local or parameter pool
the name is in scope at the declaration of the field's struct only when the
struct is declared inside the function, which suggests the pool-named form
is mostly a global-pool feature, and that is where the relink-heavy
structures live.
