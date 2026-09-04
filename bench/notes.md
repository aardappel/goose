## Reading the Rust rows

Rust gets one target here rather than the two tiers C++ gets, because the
language points much more clearly at one way of doing things and its community
is unusually firm about what that is. Where a benchmark genuinely has two
idiomatic shapes, both are listed, and the reason is never "one of them is
faster":

* `strlist` has `Vec<String>` and `Vec<&str>`, which differ in *ownership*. Only
  the first can outlive the text it came from, which is the semantics the Goose
  row has.
* `tree`, `interp`, `sexp`, `scene`, `calc` and `bintrees` have an
  owning-pointer row (`Box`) and an arena row (`Vec` plus `u32` indices). Both
  are safe and both are idiomatic; the arena is what the community recommends
  for anything pointer-heavy, and `design.md` predicted it would be the real
  comparison point rather than `Box`. It is, by 2.6x (`calc`) to 14x
  (`bintrees`) on the six benchmarks that have both.
* `graph` has three rows for the same reason the C++ side does: `Vec<Vec>`, CSR,
  and the one-pass index-linked arena. CSR wins there in every language and is
  index-based in every language, so `graph` is the one place where the
  references-versus-indices question does not show up in the winning row.
* `words` and `lru` have a `HashMap` row and a hand-rolled open-addressed
  table, because std's default hasher is a security policy rather than a speed
  choice, and the usual Rust answer to that -- a hasher crate -- is outside a
  std-only suite. In `lru` both rows share the same `Vec` arena and free list
  for the nodes: std has no intrusive list and `LinkedList` hands out no node
  identity, so the idiomatic Rust LRU is *already* the arena, and the only
  thing the two rows differ in is the map.
* `respond` has a DTO row (`String` and `Vec<Item>` built per request, rendered
  by a function returning a fresh `String`) and a streaming row that writes the
  JSON straight into one reused `String`, mirroring the two Goose rows.
* `blur` has the obvious index-everything row and the row-slice/`windows(3)`
  iterator style a Rust programmer switches to when a profiler shows bounds
  checks -- which, it turns out, it would not here (see the bounds-check
  section).

Everything else is a single row. The aggregate table compares Goose against
whichever Rust row *won*, so those ratios are against the best safe Rust here,
not the most flattering one.

Build flags are `-O -C codegen-units=1`. The codegen-units setting is what makes
the comparison fair rather than generous: every C++ row is one translation unit
compiled whole, and the rustc default of 16 units would deny Rust the same
cross-function view. Everything else is left where a shipped Rust binary leaves
it -- bounds checks on, unwinding panics, no `target-cpu` beyond the x86-64
baseline the C++ rows also build for. Goose and Rust are therefore both checked,
with C++ as the unchecked baseline.

### The layouts, since most of the memory column follows from them

Measured with `size_of` and `sizeof`, or read off the packed declarations:

| | Rust | C++ | Goose |
|---|---:|---:|---:|
| `tree` node | 12 arena / 24 `Box` | 12 arena / 24 `unique_ptr` | 12 |
| `interp` node | 12 | 12, both `variant` and union | 5 leaf, 9 binary |
| `sexp` node | 24 arena / 32 + heap | 24 arena / 64 + heap | variable, text inline, 4-byte links |
| `records` event | 32 + heap for `Say` | 48 + heap / 24 buffer | variable |
| string header | `String` 24, `&str` 16 | `std::string` 32 | none: inline varint |
| `lru` node | 16 arena, both rows | 16 arena / 24 `std::list` node + a map node, each malloc'd | 16 |
| `lru` map slot | 8 (`HashMap` adds control bytes) | 8 / a 32-byte `unordered_map` node | 8 |
| `scene` node | 120 arena / 128 + a `Vec` per node | 120 arena / 128 + a `vector` per node | 117 |
| `calc` node | 12 arena / 24 `Box` + heap | 16 arena / 32 `unique_ptr` + heap | 2-3 number, 3 negate, 3 paren, 6 binary |
| `bintrees` node | 8 arena / 16 `Box` + heap | 8 arena / 16 `new` + heap | 8 |
| `respond` item | 24 `String` + 16 + heap | 32 `std::string` + 8 + heap | sku inline, 1 + 6 + 2 varint bytes |

The `records` row is where Rust beats C++ outright and still loses to Goose by
4.0x. Rust's 32 bytes against MSVC's 48 comes from two things: `String` is
three words where `std::string` is four, and the enum tag hides in the pointer's
niche rather than needing its own word. But it is still a *fixed* enum, so every
one of the 16M elements is sized for `Say` whether it is a `Say` or not, and the
text is a second allocation on top. That is the thing no amount of Rust skill
removes, and it is what `records_fixed.goose` is the control for.

Three of the Goose layouts have no counterpart at all. A `sexp` node is as big
as its variant needs, with its symbol bytes inline behind a varint length and
its two links 4-byte offsets into the pool -- 1.3x smaller than the same
program with 8-byte links, which is what a language that cannot make a null
link relative would be stuck with. A `scene` node is a 117-byte record whose
four child links are 4-byte offsets in a limited array inside the record (spec
5.3, 3.9), so the whole scene is one contiguous, position-independent block;
the arena rows in both other languages reach the same 120 bytes with `uint32`
indices, and their idiomatic rows pay a heap `vector`/`Vec` per node for the
children on top of 128. A `calc` binary node is 6 bytes -- a tag, an operator
byte and two 2-byte relative references, because a per-input tree never spans
more than a few hundred bytes -- where the arenas' `u32` links make it 12-16
and the owning-pointer rows 24-32 plus a heap block per node.

## Reading the toolchain table

The two backends are within 10% on most rows and neither dominates. The gaps
that remain:

* **clang vectorises what v145 does not.** Goose's flat `blur` is the largest
  gap in the suite at 7.5x (2,042 against 271 ms), and the next two are the
  C++ `__restrict` row of the same kernel at 1.83x and `particles cpp SoA` at
  1.60x. The Goose `blur_rows` form is 1.15x for the same reason. What the
  bounds-check section takes apart is that these are two different effects
  wearing one number: v145 will not vectorise a checked loop at all, and clang
  will, but neither hoists a fat reference's header out of a loop that stores
  through another one.
* **v145 is better at recursion into a bump allocator, and at
  `std::vector::push_back`.** `bintrees` reads 0.82 on the Goose row and 0.72
  on the C++ `new`/`delete` one; `push cpp vector+reserve` is 0.64 (197 ms
  against 306). clang also trails on both `interp` arena rows and on `sum`.
* **clang is consistently 5-25% ahead on string, hash, allocator and
  float-and-pointer work**: `words` (1.18 on both hand-rolled and
  `unordered_map`), `strlist` (1.05-1.09), `records variant+string` (1.14),
  `sexp unique_ptr` (1.14), `respond` (1.14 and 1.23 on the two C++ rows),
  `calc cpp arena` (1.16), and `scene`, where it is 1.24 on the Goose row and
  1.10 on the C++ arena.
* On Goose's own rows: `records` and `interp` favour v145 (0.89 and 0.94), the
  string- and pool-shaped rows favour clang by 6-10%, `graph`'s CSR row by
  1.19, and `lru` by 1.70 -- its pool-relative loads are a base-plus-offset
  that clang schedules and v145 does not.

Four conclusions change with the backend, which is why every ratio in this
report is given per backend: against the best safe Rust row, `lru` is 0.68x
under v145 and 1.16x under clang, `graph` 0.99x and 1.18x, `scene` 0.77x and
0.95x, and `bintrees` 0.98x and 0.81x -- the last of those the only one where
v145 is the favourable side.

## Bounds-check elimination, measured

The compiler proves most index and slice checks away (spec 10.5).
`bench/bce_ab.ps1` builds each benchmark with and without `--no-bce` under both
toolchains, at the size baked into each source file rather than the report's
`large` one. `--no-bce` switches off more than the checks: the loop-view hoist
decides whether a loop's array length is invariant by asking BCE's kill
summary, so with the pass off the view is re-read every iteration too. The A/B
measures both together, which matters on exactly one row and is stated there.

| benchmark | index checks elided | slice | v145 gain | clang gain |
|---|---|---|---:|---:|
| blur_assert | 10/10 | 0/0 | **+75%** | **+345%** |
| blur_rows | 10/10 | 0/4 | **+147%** | +6% |
| blur | 0/10 | 0/0 | -5% | **+303%** |
| graph_csr | 10/20 | 0/0 | 0% | **+31%** |
| graph | 4/8 | 0/0 | +5% | +11% |
| scene | 12/12 | 0/0 | +6% | +1% |
| sexp | 11/12 | 0/1 | +4% | +1% |
| calc | 20/22 | 0/0 | +1% | +1% |
| records | 0/1 | 0/0 | +1% | -1% |
| respond | 1/1 | 3/3 | 0% | 0% |
| strlist | 0/2 | 1/2 | -1% | +5% |
| words | 0/8 | 3/4 | -12% | -5% |
| lru | 0/15 | 0/0 | -13% | +10% |
| sum, push, tree, interp, particles, bintrees | no index expressions at all | | | |

`lru` and `words` read the pass-on binary *slower*, and `lru`'s two C files are
byte-identical (nothing is elided in it), so that -13% is the noise floor of a
cache-bound random-access row rather than a result. Everything between -5% and
+6% is in the same category.

**The rows where it matters are the image kernel and the CSR traversal**, and
the two backends want different things from the pass. At W=2048, 16 passes, ms,
best of four:

| | v145 | clang |
|---|---:|---:|
| `blur`: `src[y*W + x]`, nothing elided | 141 | 33 |
| `blur_assert`: the same plus `assert(src.len == W * W)`, 10/10 | 77 | 29 |
| `blur_rows`: row slices with a length assert each, 10/10 | 32 | 30 |
| any of the three with `--no-bce` | 134-79 | 131 |

Under v145 the nine checks per pixel are what blocks vectorisation: proving
them away is worth 1.85x on the flat kernel and another 2.4x on the row-slice
form, and where they are kept the loop stays scalar however the array is
reached. Under clang the checks cost nothing at all -- it vectorises the
checked loop as fast as the unchecked one, exactly as it does for the Rust
`blur_index` row, which keeps ten `panic_bounds_check` sites in its assembly
and runs at the speed of the checkless `windows` row. What clang needs instead
is the *loop-view hoist*: `src` and `dst` are fat references, so without it
every access reloads `src.hdr->base` and `src.hdr->len` and a byte store
through `dst` may alias those loads. That is the +303% on the `blur` row, where
no check is elided at all, and it is why the flat kernel is level with flat
Rust under clang and 7x behind it under v145.

`blur` stays at 0/10 under v145 for one reason: `src.len == W * W` is a fact
about the caller's array that `blur(src: u8[>..]&, ...)` never sees. One
`assert(src.len == W * W)` at the top of the kernel proves all ten and takes
the row from 141 to 77 ms; carrying such lengths across calls is the first
item on the compiler list below.

**Every check that survives elsewhere is an index that came out of memory.**
`dist[u]` where `u = q[qh]`, `dist[w]` where `w = cur.to`, `out[cursor[s]]` in
the CSR fill, `pool[slots[at].node]` throughout `lru`. The analysis gets
everything derived from loop counters, lengths, constant masks and reductions,
and nothing that was loaded from a data structure -- which is what
`test/bce.goose` documents. `blur` adds a second class, an index that is a
*product* of two counters; the difference-constraint domain carries one-shot
product and two-term-sum bases for that idiom, which is also what takes `scene`
to 12/12.

Rewrites were tried to see whether any natural form unlocks the loaded-index
checks, and none is worth adopting: making `graph`'s arrays globals (the
invariant would have to hold on array *contents*), deriving `words`'s mask
from `slots.len`, using the documented `%` reduction (the range fact does not
survive the loop back-edge), and one `assert` at the top of the probe loop --
all four timing within noise of each other, because those checks are perfectly
predicted branches on values already in registers. Array-contents invariants
themselves were implemented and measured, and bought elisions on the two
`graph` rows with no measurable time on either, so they were dropped. The
`blur` measurement above is the counterexample: a check per load in a loop that
would otherwise vectorise is not free at all.

## What the standard library costs the benchmarks

Eight of the sixteen benchmarks reach into `stdlib/` wherever the library says
what the hand-written code said. Each adoption was measured on its own, at the
`large` size, both toolchains, best of three interleaved runs, against the
hand-written form it replaced (above 1.00 means the library form is faster):

| row | what it uses | v145 | clang |
|---|---|---:|---:|
| `strlist` | `each_split` | 1.04 | 1.07 |
| `words` | `each_split`, `hash`, `next_pow2`, `push_n` | 1.01 | 1.03 |
| `graph`, `graph_csr` | `fill` | 0.98 | 1.04 |
| `lru` | `next_pow2`, `push_n` | 1.03 | 0.98 |
| `sexp` | `format_int` | 1.00 | 1.01 |
| `respond`, `respond_stream` | `hash` | 1.00 | 1.00 |
| `particles`, `particles_scalar`, `scene` | `vec`'s `float3` | 0.99 | 1.00 |

So the library is free where it was adopted, and on two rows slightly better
than the loop it replaced. `words` additionally keeps a whole second row,
`words_dictionary.goose`, which throws the hand-rolled table away and counts
into `dictionary<u8[:], i32>`: that one is a data-structure comparison, not a
notation one, and the per-benchmark table has it.

Three things were measured and *not* adopted, which is the other half of the
result:

* **`format_int` in `respond` and `respond_stream`** costs 3-5% under v145
  against the hand-written digit loop, and nothing under clang. Most of the
  original gap was `format_uint` dividing by its `base` parameter -- a real
  division per digit where the hand-written loop divides by the constant 10 --
  and giving base ten its own loop in the library recovered about half of it.
  What is left is the extra call layer and the padding branch on a row that
  renders four numbers per request and does nothing else expensive. `sexp`,
  which formats one number per node among much other work, does not notice.
* **`push_n` in `graph` and `graph_csr`** costs 2-3%: the generic `push` loop
  does not compile to the specialised one, and it also makes the destination
  array's length unprovable to the bounds-check pass a few statements later.
  `fill` alone, which is where the readability was, is free.
* **`push_n` of a value containing self-relative references** does not compile
  at all -- `push_n<A, T>(xs: A&, v: T, n: i64)` takes its value by value, and
  such a value cannot be copied (spec 3.9). `graph`'s edge-head fill stays a
  loop for that reason. A `T&` parameter would not help either: the library
  would still be copying out of it.

The library also has to be reachable from the benchmarks' own names. There is
one namespace, so a local named `fill` shadows `std`'s and the error lands at
the call rather than at the declaration; `graph_csr`'s CSR write cursor is
called `cursor` for that reason, which is also what it is.

## Where Goose loses

**Relinking through relative references is the mutation-heavy case, and the
pool-relative form is what makes it competitive.** `lru` is the one benchmark
in the suite that mutates links rather than building them once. With
self-relative links and index slots it is 1.5x behind the Rust and C++ arenas;
with spec 3.9's `in pool` form -- 4-byte offsets from the pool's base in the
nodes and in the 8-byte map slots, `pool.free(pool.index_of(n))` closing the
loop -- the row is 3,751/2,205 ms against the Rust arena's 2,563 and the C++
arena's 2,568/2,382: 1.16x ahead of both under clang, 1.46x behind under v145,
on the least memory of the three. The index form is kept as
`lru_indices.goose`; back to back it is level under v145 and 1.31x slower
under clang. The same program written more ways, at the `large` size, all with
the map holding indices:

| links | node | v145 | clang |
|---|---:|---:|---:|
| `Node&<u32>?` self-relative | 16 | 3,738 | 3,189 |
| `Node&<u64>?` self-relative, no range check | 20 | 3,573 | 2,881 |
| `Node?` plain references | 24 | 2,557 | 2,352 |
| `i32` indices, `pool[i]` bounds-checked (the Rust shape) | 16 | 2,397 | 2,266 |

Plain references and indices are level, so it is not references that cost
and not the `reusable` pool (every variant frees and reuses through the same
freelist, 1.3M times). It is the self-relative encoding: an offset computed
from two addresses and stored on every relink -- six per hit -- and, on every
load, a null test and an add before the next load can issue. The range check
is a tenth of it under clang and nothing under v145; the rest is the
arithmetic on the pointer-chasing critical path. The links were made relative
for the 8 bytes they save per node against plain references, which `lru`
never gets to enjoy: the index shape is the same 16 bytes. On the build-once
benchmarks (`tree`, `interp`, `sexp`, `scene`, `bintrees`) the same links cost
nothing measurable against indices, so this is specifically the relink
pattern. The pool-relative form replaces the field-address subtraction with
one from a base held in a register, and the load with `base + off`; clang
schedules that far better than v145, which is the 1.70x between the two
backends on this row and the whole of what is left. The build-once benchmarks
keep self-relative links, whose position independence they want and whose cost
against indices there is nothing measurable.

**Recursive construction is where the C backends can fall behind rustc.**
`bintrees` builds and discards 34M nodes in recursive calls: Goose lands at
232 ms under v145 against the Rust arena's 228 and the C++ arena's 388, and at
282 under clang against the same 228. The C++ arena is behind Goose under both
backends, so what the clang column measures is what LLVM makes of rustc's
recursion against what it makes of the same shape written in C. `tree` -- the
same node built once and summed eight times -- has Goose 3-5% ahead of the
Rust arena. `calc` is the same story at a smaller scale, 0.82-0.86x of the
Rust arena: what remains there is the global cursor, the `return from`
discriminant and a varint decode on values that mostly need two bytes.

**Flat scalar kernels lose to v145's refusal to vectorise a checked loop, and
to nobody on data.** `blur` in its obvious form is 1.9x behind flat C++ under
v145 and 1.07x *ahead* of flat Rust under clang (271 against 291 ms), for the
two backend-specific reasons the bounds-check section takes apart, and level
with both once written over row slices. `particles` is 2-5% behind Rust on
identical memory, which is within the noise of the row. There is no Goose
advantage on either and the design did not predict one.

**Pointer-linked adjacency loses to CSR, in all three languages.** On `graph`
the one-pass linked build is 5x slower to traverse than two-pass
count-then-fill, in every language. The Goose CSR row exists to show this is
a data-structure effect rather than a language one, and Goose writes CSR
level with both -- 943/794 ms against C++'s 882/855 and Rust's 935.

**And v145 is a worse backend for the float-and-pointer mix than clang.**
`scene` is 5% behind the Rust arena under clang and 30% behind under v145,
with the C++ arena 10% behind its own clang build on the same row; padding the
117-byte node to 120 changes nothing, so it is not the packed layout.

## Caveats on these numbers

* Whole-process wall clock, so input generation is inside every measurement.
  Where generation is a large share of the work (`sexp`, `words`, `graph`,
  `calc`) the ratios are diluted toward 1.0 and the gap on the interesting
  phase is larger than the table shows.
* Best of three runs after two discarded warm-ups. The warm-ups are not
  optional: on this machine a freshly written executable costs up to 10x its
  steady-state time on the first execution and takes about three runs to
  settle, and it hits clang-linked binaries far harder than MSVC-linked ones.
  Without them the toolchain comparison measures the malware scanner.
* One machine, no core pinning. Differences under ~10% are not differences,
  and on the two cache-bound random-access rows, `graph`'s linked rows and
  `lru`, not under ~15%: two byte-identical `lru` binaries measured 14% apart
  in the bounds-check A/B.
* **The allocator-heavy rows are the least reproducible in the suite, and they
  are the ones the headline ratio rests on.** Running the whole harness twice
  back to back, the Goose rows land within 3% of themselves, and so do the
  arena and CSR rows in every language -- but the malloc-bound rows move by up
  to 23%. A difference in the "vs idiomatic" geometric mean of less than about
  10% means nothing at all, and a single suite run is not enough to attribute
  a change to the compiler; that wants a per-commit A/B built from the same
  sources minutes apart, with `-falign-loops=32` under clang so a shifted hot
  loop cannot masquerade as a codegen change.
* This CPU has a very large L3, so `small` and `medium` can sit entirely in
  cache. The `large` rows are the ones that stress the memory subsystem, and
  `calc`, `respond` and `bintrees` never leave it by design: their working
  set is one input, one request, one tree.
* The process-start floor is not constant between runs, and it is inside every
  `small` cell, so read those as differences from the floor and read ratios
  only at `large`.
* Rust rows are a single rustc time, not one per backend. rustc is LLVM, so
  the clang column is the one they are most directly comparable with; the
  Goose-vs-Rust ratios are reported against both Goose builds anyway.

## What the benchmarks say about the language

Findings from writing and maintaining the sixteen benchmarks, independent of
how fast anything ran. Between them they reach `reusable` pools, per-input
arenas inside recursion, `return from` as a taken error path, inline child
arrays, DTOs with nested variable-size parts, and the standard library.

**What works without friction:** grow-only pools with interior references,
relative references inside a single root, variable-mode ADTs with inline
variable-size payloads, case functions as the dispatch mechanism, `reusable`
pools with sentinel-linked lists, limited arrays of relative references inside
pool elements, a DTO with a variable-size string and an inline list of
variable-size records built by a function returning straight into the field,
`return ... from` taken on a third of all inputs from three levels of
recursion, per-input local pools reset by scope exit, and a recursive builder
building into a pool it is handed by reference.

**What still bites:**

* **Inline child arrays need their count at construction.** The A.2 shape
  `(T&<u32>)[varint]` is only reachable when the children exist before the
  parent is built and their number is known; a builder that discovers them
  as it goes uses a limited `[..k]` array inside a fixed-size node (`scene`,
  17 bytes for four links) or sibling links (`sexp`).
* **A plain reference parameter cannot be stored relatively outside a
  recursive cycle.** `fn link(prev: Node?) { pool.push(Node { next: prev }) }`
  is rejected at any call site that passes a pool-rooted reference: the
  parameter's root is its own per-call-site class, and nothing identifies that
  class with the global the argument came from. Inside a `recursive` cycle the
  pool-class machinery (spec 7.8) does make the identification, which is why
  `sexp`'s parser can do exactly this. The same identification outside a cycle
  is the natural fix.
* **Arithmetic at the operands' width bites on `u8` data.** The first version
  of `blur` summed nine `u8` taps in `u8` and was a very fast non-blur; every
  tap needs `as u16`. C promotes to `int` silently and Rust would panic in a
  debug build; Goose wraps, as the spec says it will (6.2), but nothing in the
  source points at it.
* **A negative literal through a generic parameter takes `i64`.**
  `push_n(dist, -1, V)` on an `i32[>..]` fails inside the library, where the
  bound `T` is `i64` and the push does not narrow; `push_n(dist, -1 as i32, V)`
  works, and a plain `0` binds fine. The literal's type should follow the
  destination through the instantiation.
* **Two parses to know about.** `x as u64 & mask` reads `u64&` as a reference
  type (parenthesise the cast), and an `if`/`else if` chain that ends without
  an `else` in tail position is an error (write early returns).
* **Self-relative references trade speed for position independence in code
  that relinks; pool-relative ones do not.** Self-relative links cost nothing
  measurable against indices on the five benchmarks that build a structure
  once and walk it, and 1.5x on `lru`, which relinks six links per hit. Spec
  3.9's `in pool` form -- offsets from a named global pool's base -- makes
  the relink a subtraction from a register and lets other arrays hold 4-byte
  links into the pool; `lru` on it is level with the arenas under clang. The
  encoding is a per-field decision: self-relative for blobs and for local
  pools, `in pool` for relink-heavy structures in a global pool.
* **Thread queues cannot carry a relative-reference structure** (not flat,
  TODO 9), and save/load needs the whole-region copy of TODO 16, so the two
  design.md advantages that remain unbenchmarked are unbenchmarkable today.

## What the Rust implementations say about Rust

Findings from implementing the suite in Rust, independent of how fast
anything ran. The headline is that Rust is a much closer competitor than C++ on
speed and a much better-designed language than C++ on most of these axes --
and that the places it still cannot follow Goose are structural, not effort.

**One benchmark has no Rust spelling at all.** `push` keeps a pointer to every
64th element of an array it is still appending to. In Goose that is
`marks.push(items.push(...))` and the reference stays valid for the array's whole
life. In Rust it does not compile: a `&Item` borrows `items` for as long as it is
held, so the following `items.push` is rejected. No std container is both
pointer-stable and O(1)-append. The row uses indices because that is what a
Rust programmer writes; the interesting cost is in the source, not the numbers.

**Indices everywhere Goose has references.** Same story in `tree`, `interp`,
`graph`, `sexp`, `scene`, `calc`, `bintrees` and `lru`: the fast, safe,
recommended Rust shape is a `Vec` arena with `u32` links, exactly as
`design.md` predicted. What that costs is not safety -- an out-of-range index
panics like any other Rust index -- but: the type says `u32`, not what it
points at; `Option<&T>` becomes a sentinel that occupies a real slot; and an
index into a pool that is later reused is stale in a way the compiler cannot
see, where a reference would have been rejected. Goose reaches the same
layouts with `TNode&<u32>?`: checked, nullable, and typed to the thing it
points at.

**The arena is sometimes worse to write than the `Box` row, not just
different.** In `scene_arena.rs`, `update` cannot compose into the node in
place: `&pool[i].local` and `&mut pool[i].world` are two borrows of one `Vec`
and neither `Index` nor `IndexMut` can split them, so the transform is built
in a stack local, copied in, and the child list is copied out of the node
before recursing. `scene_box.rs` has neither problem, because disjoint fields
of one `&mut` borrow independently. Goose composes straight through `Xf&`
references into the pool element.

**The idiomatic LRU is already the arena.** std has no intrusive list and
`LinkedList` hands out no node identity, so O(1) move-to-front by handle needs
`Rc<RefCell<Node>>` (two allocations and a refcount pair per node) or the
`Vec`-plus-indices arena. Both Rust rows use the arena; they differ only in
whether the map is `HashMap` (SipHash) or open addressing.

**Rust enums are the best fixed-tag ADT of the three.** A byte discriminant, a
jump table from `match`, no vtable pointer, and niche optimisation that hides
the tag inside a payload pointer. That is enough to beat `std::variant` on both
time and memory in `records` and `interp`. What it cannot do is stop being
fixed: every element is sized for the largest variant. Variable-mode enums are
the one Goose feature in this suite with no Rust analogue at any level of
effort.

**`Result` plus `?` is a good deal.** In `calc` it reads like the C++
exception row and costs like the error-code row. What the arena version gets
for free -- `Vec::clear()` as an O(1) teardown -- holds only because `Expr`
owns nothing; a node holding a `String` would turn the clear back into a
per-node drop walk, which is the ordinary case and the one Goose never has.

**Rust's idioms are already the fast ones, twice.** `collect()` over a
`TrustedLen` iterator preallocates exactly, so the "did you remember to
`reserve`" gap that `sum` and `push` measure against C++ simply does not exist
against Rust. And borrowing is the default, so `Vec<&str>` and
`HashMap<&str, _>` are the first thing written. Both are real language-design
wins over C++ and both shrink the Goose margin.

**Where Rust pays anyway:** `Box::new(T { .. })` builds the value in a stack
temporary and moves it to the heap (no guaranteed construct-in-place, spec
4.3/7.3); teardown is real and it is the same cache-hostile pointer chase that
building was (`interp`'s `Box` row is behind C++ virtual dispatch; `bintrees`
is nothing else); std's `HashMap` hashes with SipHash-1-3 by policy; and the
borrow that makes `sexp`'s arena row fast works only because the text is
complete before parsing starts.

**LLVM removes what the Goose pass cannot, and keeps what it can.** Rust's
`blur_index` row keeps ten `panic_bounds_check` sites in its assembly and
runs as fast as the checkless `windows` row, because LLVM vectorises around
them; the Goose flat row under v145 is 4x slower for a different reason (the
nine checks stop MSVC vectorising at all). Conversely the Goose row-slice form
elides everything at compile time and MSVC, which does not vectorise checked
loops, then runs it 4x faster.

## Where the Goose-Rust gap comes from, and where it can still move

The sixteen rows split three ways by *why* the Rust comparison lands where it
does, and only two of the three are worth working on.

**Structural, and already banked.** Every row where Goose is clearly ahead is
ahead for a reason Rust has no version of, not because the loop is tighter:

* *Variable-mode enums.* `records` is 2.0-2.3x faster on 4.0x less memory
  because an element costs its own variant rather than the largest one, and a
  `Say`'s text is inline rather than a second allocation. `interp` and `sexp`
  are the same property in a tree. Rust's enum is the best fixed-tag ADT of
  the three languages and still pays max-payload per element; there is no
  amount of skill that changes it.
* *Variable-size parts inside a fixed-size parent.* `respond` builds a DTO
  whose string, item list and per-item sku are all inline, renders it with no
  allocation anywhere, and lands level with the streaming rows that never
  build the object. The Rust DTO allocates per string and per list, per
  request.
* *References into a container that is still growing.* `push` has no safe Rust
  spelling at all; `tree`, `graph`, `scene`, `bintrees` and `calc` have one
  only as `u32` indices. Goose gets the arena's layout with the pointer
  version's types.
* *Links narrower than a pointer.* `sexp`'s nodes link with 4-byte offsets,
  `calc`'s with 2-byte ones, `scene`'s four child links live in a 17-byte
  array inside the node. Rust's arena reaches 4 bytes too -- as an index,
  which is untyped, needs a sentinel slot, and goes stale silently when the
  pool is reused.

**Backend, not language.** `lru` under v145 (0.68x), `scene` under v145
(0.77x), `bintrees` under clang (0.81x), and `blur`'s flat form under v145.
In three of the four the C++ row built by the *same* toolchain loses in the
same direction, so it is what MSVC or clang makes of a C shape, not what Goose
asked for; `blur` under v145 is the bounds checks, which is a Goose pass and is
item 1 below. Nothing here is closed by changing the language, and the ceiling
is visible: each of these rows is already level under the other backend.

**Design cost, paid on purpose.** The bounds checks that survive are indices
loaded out of a data structure, which is exactly where a check is not
redundant; `calc`'s `return from` discriminant is a real error path that the
C++ error-code row also pays and the exception row pays far more for. These
are not gaps to close.

**What would extend the lead, in order:**

1. **Carry array lengths across calls** (item 1 below). Under v145 it is
   worth 1.85x on the flat image kernel, and it is the difference between
   "Goose is fast if you write it in row slices" and "Goose is fast".
2. **Identify a reference parameter's root class with its pool outside a
   recursive cycle** (item 2 below). Narrow links are the memory advantage,
   and today a structure linked by them can only be built by functions inside
   the cycle that builds it. Lifting that makes the advantage available to
   ordinary code rather than to parsers.
3. **Cross-array narrow links, everywhere they typecheck.** `T&<u32 in pool>`
   already lets a side table hold 4-byte links into a pool instead of indices
   -- that is what `lru`'s map does, and a grow-only array of such links works
   the same way, so `sexp`'s root list is a candidate. The idiom deserves to
   be the default advice for a side table, not an `lru` special case.
4. **Keep looking for places a root requirement is not really needed.** A
   sentinel-ended chain does not force plain 8-byte links, because null needs
   no root -- that alone is 1.3x of `sexp`'s memory. The same question is
   worth asking of every rule that currently demands an exact root: what does
   the *representation* actually require?

## Compiler work, in order of measured payoff

1. **Carry array lengths across calls.** `blur` keeps its nine checks per
   pixel only because `src.len == W * W` is known in `main` and not inside
   `blur(src: u8[>..]&, ...)`. One assert inside the kernel proves all ten and
   is worth 1.85x under v145 (141 to 77 ms) and 1.11x under clang; the fix is
   either an interprocedural length fact for reference parameters, or the
   analysis treating a caller-side `assert` as a precondition of the callee.
2. **Identify a reference parameter's root class with the pool it came
   from, outside a recursive cycle too.** A non-recursive function that takes
   a `Node?` and stores it into a `Node&<u32>?` field of the same pool is
   rejected today, though the identical code inside a `recursive` cycle is
   accepted; the cycle machinery already computes what is needed. This is what
   stops a linked structure's helper functions from being factored out of the
   cycle that builds it.
3. **Specialise the library's element loops.** `push_n` costs 2-3% against the
   `for` loop it replaces and blocks a bounds-check elision downstream, which
   is the whole reason two benchmarks kept their loops. The generic form
   should compile to what the loop compiles to.
4. **Inline a base case that is not the first statement** (`scene`,
   `interp`), and match expression-bodied folds (`if c { 0 } else { 1 + f(x) }`)
   in tail-recursion elimination.

Ideas that were measured and should **not** be done: array-contents invariants
for the bounds-check pass (more elisions, no measurable time), merging tag
dispatch (1%), dropping `#pragma pack(1)` where the layout is already gap-free
(no difference on either backend), fusing an *optional* relative-reference load
with its null test (both backends get worse), and rewriting benchmark code so
the bounds-check pass can prove loaded indices (buys nothing).
