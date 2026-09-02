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
| `sexp` node | 24 arena / 32 + heap | 24 arena / 64 + heap | variable, text inline |
| `records` event | 32 + heap for `Say` | 48 + heap / 24 buffer | variable |
| string header | `String` 24, `&str` 16 | `std::string` 32 | none: inline varint |
| `lru` node | 16 arena, both rows | 16 arena / 24 `std::list` node + a map node, each malloc'd | 16 |
| `lru` map slot | 8 (`HashMap` adds control bytes) | 8 / a 32-byte `unordered_map` node | 8 |
| `scene` node | 120 arena / 128 + a `Vec` per node | 120 arena / 128 + a `vector` per node | 117 |
| `calc` node | 12 arena / 24 `Box` + heap | 16 arena / 32 `unique_ptr` + heap | 2-3 number, 3 negate, 6 binary, 9 paren |
| `bintrees` node | 8 arena / 16 `Box` + heap | 8 arena / 16 `new` + heap | 8 |
| `respond` item | 24 `String` + 16 + heap | 32 `std::string` + 8 + heap | sku inline, 1 + 6 + 2 varint bytes |

The `records` row is where Rust beats C++ outright and still loses to Goose by
4.0x. Rust's 32 bytes against MSVC's 48 comes from two things: `String` is
three words where `std::string` is four, and the enum tag hides in the pointer's
niche rather than needing its own word. But it is still a *fixed* enum, so every
one of the 16M elements is sized for `Say` whether it is a `Say` or not, and the
text is a second allocation on top. That is the thing no amount of Rust skill
removes, and it is what `records_fixed.goose` is the control for.

The new rows add two layouts worth a sentence. A `scene` node in Goose is a
117-byte record whose four child links are 4-byte offsets in a limited array
inside the record (spec 5.3, 3.9): the whole scene is one contiguous,
position-independent block. The arena rows in both other languages reach the
same 120 bytes with `uint32` indices -- and their idiomatic rows pay a heap
`vector`/`Vec` per node for the children on top of 128. A `calc` binary node
is 6 bytes: a tag, an operator byte and two 2-byte relative references,
because a per-input tree never spans more than a few hundred bytes; the
arenas' `u32` links make it 12-16 and the owning-pointer rows 24-32 plus a
heap block per node.

## Reading the toolchain table

The two backends are within 10% on most rows and neither dominates. The gaps
that remain:

* **clang vectorises what v145 does not.** `blur cpp row pointers` is the
  largest gap in the suite at 1.81x (552 vs 305 ms): the `__restrict` rows
  vectorise under clang and stay scalar under v145. `particles cpp SoA` is
  the same story at 1.64x. The Goose `blur_rows` form is 1.15x for the same
  reason, and it is the one place where the bounds-check pass matters more
  under v145 than under clang (see the next section).
* **v145 is better at `std::vector::push_back`.** `push cpp vector+reserve` is
  0.64 -- v145 196 ms against clang 306. clang also trails on both `interp`
  arena rows.
* **clang is consistently 10-20% ahead on string, hash and float-and-pointer
  work**: `words`, `strlist`, `records variant+string`, `sexp unique_ptr`,
  `respond` (both C++ rows, 1.17-1.22x), `calc cpp arena` (1.15x), and
  `scene`, where it is 1.24x on the Goose row and 1.09x on the C++ arena.
* On Goose's own rows the picture is unchanged from last round: `records` and
  `interp` favour v145 (0.85 and 0.97), everything string-shaped favours clang
  by 4-8%, and `lru` favours clang by 1.18x. No row's conclusion changes with
  the backend, except that `scene` is a tie against Rust under clang and a
  24% loss under v145.

## Bounds-check elimination, measured

The compiler proves most index and slice checks away (spec 10.5).
`bench/bce_ab.ps1` builds each benchmark with and without `--no-bce` under both
toolchains, at the size baked into each source file rather than the report's
`large` one. `--no-bce` switches off more than the checks: the loop-view hoist
decides whether a loop's array length is invariant by asking BCE's kill
summary, so with the pass off the view is re-read every iteration too; the A/B
measures both together.

| benchmark | index checks elided | slice |
|---|---|---|
| sexp | 11/12 | 0/1 |
| graph_csr | 11/21 | 0/0 |
| graph | 5/9 | 0/0 |
| words | 2/10 | 3/3 |
| strlist | 2/4 | 1/1 |
| records | 0/1 | 0/0 |
| calc | 20/22 | 0/0 |
| scene | 9/13 | 0/0 |
| respond | 1/1 | 3/3 |
| lru | 0/19 | 0/0 |
| blur | 0/10 | 0/0 |
| blur_rows | 10/10 | 0/4 |
| sum, push, tree, interp, particles, bintrees | no index expressions at all | |

What the A/B is worth, per side, best of the reps: `graph_csr` +5 to +17%
under v145 and +0 to +4% under clang, `blur_rows` **+138% under v145 and
nothing under clang**, and everything else inside noise: `graph` +0.8% and
+1.1%, `sexp` +2.2% and -1.8%, `strlist` +0.6% and +3.7%, `words` -2.8% and
-3.8%, `records_var` -0.6% and +1.6%, `scene` -1.4% and +2.6%, `calc` -1.7% and
+1.1%, `respond` +0.5% and +0.3%, `blur` 0.0% and -2.9%. `lru` reads -14% and
-12% -- the pass-on binary *slower* -- with the two C files byte-identical
(nothing is elided in it), which puts the noise floor of that cache-bound row
at about 15% and says nothing about the pass.

`blur_rows` is the first row where the pass is worth more than a few points,
and the two backends explain it between them. Three variants of the kernel at
W=2048, 16 passes, ms, best of three:

| | v145 | clang |
|---|---:|---:|
| `blur`: `src[y*W + x]` through the array reference (0/10 elided) | 133 | 131 |
| the same over a whole-array slice taken once (0/10 elided) | 132 | 31 |
| `blur_rows`: row slices with `assert(len == W)` (10/10 elided) | 33 | 31 |

Under v145 the nine checks per pixel are what blocks vectorisation: with the
checks gone the loop runs 4x faster, and where they are kept it does not
matter whether the array is reached through a reference or a slice. Under
clang the checks cost nothing -- it vectorises the checked slice loop as
fast as the unchecked one, exactly as it does for the Rust `blur_index` row
(10 `panic_bounds_check` sites in the asm and the same time as the checkless
`windows` row) -- and the 4x is lost somewhere else: `src` and `dst` are fat
references, every access reloads `src.hdr->base` and `src.hdr->len`, and a
byte store through `dst` may alias those loads, so clang cannot hoist them.
The loop-view hoist (`7a7ac87`) does not fire here because the array is a
reference parameter rather than a local or global. Both problems are fixed by
writing the kernel over row slices, which is the first item on the compiler
list below.

**Every check that survives elsewhere is an index that came out of memory.**
`dist[u]` where `u = q[qh]`, `dist[w]` where `w = cur.to`, `out[fill[s]]` in
the CSR fill, `pool[slots[at].idx]` throughout `lru`, `n.kids[i]` in `scene`
(the loop bound is the same length, but the recursive call in the body is an
unknown callee that may reach `n`, so the fact is killed). The analysis gets
everything derived from loop counters, lengths, constant masks and reductions,
and nothing that was loaded from a data structure -- which is what
`test/bce.goose` documents. `blur` adds a second class: an index that is a
*product* of two counters. The domain is difference constraints, so
`y * W + x < W * W` is not expressible even though `y < W - 1` and `x < W - 1`
are both known.

Four rewrites were tried last round to see whether any natural form unlocks
the loaded-index checks, and none is worth adopting: making `graph`'s arrays
globals (still 5/9: the invariant would have to hold on array *contents*),
deriving `words`'s mask from `slots.len` (3/10), using the documented `%`
reduction (3/10: the range fact does not survive the loop back-edge), and one
`assert` at the top of the probe loop (9/10) -- all four timing within noise
of each other, because those checks are perfectly predicted branches on values
already in registers. The blur measurement above is the counterexample: a
check per load in a loop that would otherwise vectorise is not free at all.

## What this round changed in the compiler

Nothing in this round was an optimisation. Writing the six new benchmarks
found one typechecker restriction that made two of them inexpressible and five
codegen bugs, all fixed in this commit and all covered by tests
(`test/errors_tc/cycle_pool_swap.goose`, `cycle_local_store.goose`, and the
`layouts()` section of `test/codegen_exec.goose`):

* **Pool parameters may be stored inside a recursive cycle.** The cycle store
  rule (spec 7.8) was implemented as "only globals", so a recursive builder
  could only build into a global pool: `calc`'s per-input arena and
  `bintrees`' per-tree arena could not be written with references at all. A
  reference parameter whose pointee is resizable-class cannot point at any
  cycle function's own storage (no cycle function may own one), so such a
  parameter root class is now exempt; the back edge must pass the same pools
  the entry call did, which the checker enforces.
* **Pushing a reference into a limited array of relative references stored
  the raw pointer** (`scene`'s `nd.kids.push(c)`); it stores the offset now.
* **An inlined call used directly as a varint field initialiser** hit an
  assertion in codegen (the value temp took the varint's storage type).
* **A `T[]` call result landing in a `T[varint]` slot kept the callee's
  layout** (`respond`'s `items: make_items(...)`), and the same mismatch
  through the inliner wrote the wrong prefix; the receiver now re-prefixes the
  element run. While fixing it: a resizable local returned by NRVO as a
  variable array had never had its length prefix written in the non-inlined
  value form -- latent in every `fn f() -> u8[]` with a `u8[>..]` local that
  was too big to inline.
* **A fixed struct holding a plain reference to its own type** (`lru`'s
  plain-reference variant, `struct Node { next: Node? }`) emitted a C typedef
  that used its own name before declaring it. Struct-like types now get a
  forward typedef when first named, so a pointer to one needs only the name.

The existing ten benchmarks were re-run through the same compiler and every
row landed within the documented noise of the previous update: the Goose rows
within 4% at the `large` size, the arena and CSR rows likewise, the
malloc-bound rows by up to 19% (`interp cpp virtual`), which is the
reproducibility caveat below rather than a change. The machine itself was
slower to start a process this time -- the empty-program floor was 13 ms
against 4.6, for Goose and C++ alike -- which moved every `small` cell by
about 8 ms and nothing else.

Last round's per-commit A/B of the codegen work (`171bf6a` to `dc992a2`)
stands: caching each data stack's top in a local (`432f8ce`) was worth 6-14%
on the push-heavy rows under both backends and cost both float kernels about
6% under clang, and dropping the null test on non-optional relative loads
(`a8c9f69`) about 5% on `push` under v145. Those numbers were measured with
`-falign-loops=32` on both sides and are not repeated here.

## Recommended compiler work, in order of measured payoff

Each was measured by transforming the generated C by hand and timing it, or by
writing the same benchmark two ways in Goose, so these are upper bounds on what
doing it properly in the compiler would buy.

1. **Hoist the array view of a reference parameter out of loops that only
   store elements.** Worth **4x on `blur` under clang** and, combined with the
   next item, 4x under v145: the row-slice form of the same kernel is the
   proof. The loop-view hoist already exists for locals and globals; through a
   fat reference the base and length are reloaded per access and a byte store
   through another reference pins them.
2. **Prove indices that are products of counters.** `y * W + x` with
   `y < H - 1` and `x < W - 1` keeps all nine checks in `blur`, and under v145
   that alone is 4x. Difference constraints cannot say it; a special case for
   `i * c + j` with `j < c` (the 2D idiom) would.
3. **Stop caching the stack top where the backend already does it.** Still
   worth the 6% lost on `particles` and `particles_scalar` under clang at
   `432f8ce`, without giving back the 6-14% the same transform wins on the
   push-heavy rows. This run has `particles` 2% behind Rust under v145 and 6%
   behind under clang, the same gap as when it was introduced.
4. **Make relative links cheaper to relink, or say when not to use them.**
   `lru`'s 1.5x against indices (table below) is the offset arithmetic on the
   pointer-chasing path: a subtract and a range check per store, a null test
   and an add per load. Two things would help. The optional form is what
   forces the null test on every load, and a sentinel-linked list never has a
   null link, but a non-optional `T&<u32>` cannot be constructed before its
   target exists, so the sentinels themselves cannot use it; a way to
   construct a self-referential sentinel would let the whole list be
   non-optional and drop the test (`a8c9f69` already loads those without
   one). And the range check could be skipped where the pool's reservation
   is known to fit the width, which for a `u32` link it always does on a data
   stack of under 2 GB. Short of that, the honest guidance is that relative
   references are for structures built once and walked, and a
   `reusable`-pool structure that relinks is better off with plain
   references or indices.
5. **Seed a cycle function's return root before its back edges.** A recursive
   call's result takes whatever root the callee has recorded so far, which is
   nothing for the entry of a mutually recursive parser until its first
   return; `calc` keeps a plain-reference Paren node for exactly that reason
   (spec TODO 0g). The fix is a syntactic scan of the cycle's returns for the
   pool they push into.
6. **Do accumulator tail-recursion elimination in the Goose optimizer.** Worth
   7.5% under v145 on `tree` when it was measured two rounds ago; clang
   already does it. `bintrees`' 1.6x against the Rust arena, which neither C
   backend closes, is the place to measure what else LLVM does with rustc's
   recursion that it does not do with the generated C.
7. **Extend the bounds-check invariant pass from scalars to array contents.**
   Worth the 5-6% left in `graph` and about 10% in `graph_csr`, and it is the
   only thing that would touch `lru`'s 19 checks.
8. **Merge repeated dispatch on the same scrutinee** (`sexp`'s `walk_chain`):
   pure duplication, not separately measured.

Three plausible-sounding ideas were measured earlier and should **not** be
done: dropping `#pragma pack(1)` where the layout is already gap-free (no
difference on either backend), fusing an *optional* relative-reference load
with its null test (both backends get worse), and rewriting benchmark code so
the bounds-check pass can prove loaded indices (buys nothing).

## Where Goose loses

**Relative references lose to indices when the workload is relinking.** `lru`
is the first benchmark in the suite that mutates links rather than building
them once, and the Goose row is 1.5x behind the Rust and C++ arenas
(3,568/3,035 ms against 2,170 and 2,309/2,154) while using the least memory
of the three. The same program written three more ways, at the `large` size:

| links | node | v145 | clang |
|---|---:|---:|---:|
| `Node&<u32>?` self-relative (the row) | 16 | 3,738 | 3,189 |
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
pattern.

**Recursive construction is where the C backends fall behind rustc.**
`bintrees` builds and discards 34M nodes in recursive calls; Goose and the
C++ vector arena land together (353-397 ms) and the Rust arena is 1.6x ahead
of both, in the build (153 vs 243 ms) and in the check (74 vs 115). Nothing on
Goose's side moves it: a global pool never reset is slower (430 ms, it never
reuses memory), 8-byte links time the same as 4-byte ones, and pushing
through the fat reference is worth at most the 8% it costs `push` when the
same loop is moved behind one. `tree` -- the same node built once and summed
eight times -- has Goose 4% ahead of the Rust arena, so the difference is in
the recursion rather than the data. `calc` is the same story at a smaller
scale: 7% behind the C++ arena under v145, 21-24% behind Rust's, and a
never-reset global pool times the same, leaving the `return from`
discriminant on every call, the global cursor and the varint decode as the
suspects.

**Flat scalar kernels lose to C++ on aliasing and to nobody on data.** `blur`
in its obvious form is 1.8x behind flat C++ and 6.4x behind flat Rust for the
two backend-specific reasons the bounds-check section takes apart, and level
with both once written over row slices. `particles` is 2% behind Rust under
v145 and 6% under clang, on identical memory, which is the stack-top caching
cost from last round. There is no Goose advantage on either and the design
did not predict one.

**Pointer-linked adjacency loses to CSR, in all three languages.** On `graph`
the one-pass linked build is 5-6x slower to traverse than two-pass
count-then-fill, in every language. The Goose CSR row exists to show this is
a data-structure effect rather than a language one, and Goose writes CSR
level with both.

**And v145 is a worse backend for the float-and-pointer mix than clang.**
`scene` is level with the Rust arena under clang and 24% behind under v145,
with the C++ arena 9% behind its own clang build on the same row; padding the
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
  to 23%. A change in the "vs idiomatic" geometric mean of less than about
  10% between rounds means nothing at all, and a single suite run is not
  enough to attribute a change to the compiler; that wants a per-commit A/B
  built from the same sources minutes apart, with `-falign-loops=32` under
  clang so a shifted hot loop cannot masquerade as a codegen change.
* This CPU has a very large L3, so `small` and `medium` can sit entirely in
  cache. The `large` rows are the ones that stress the memory subsystem, and
  `calc`, `respond` and `bintrees` never leave it by design: their working
  set is one input, one request, one tree.
* The process-start floor is not constant between runs: 13 ms this time
  against 4.6 last time, for every language alike. It is inside every `small`
  cell, so read those as differences from the floor, and read ratios only at
  `large`.
* Rust rows are a single rustc time, not one per backend. rustc is LLVM, so
  the clang column is the one they are most directly comparable with; the
  Goose-vs-Rust ratios are reported against both Goose builds anyway.

## What writing these taught us about the language

Findings from implementing and maintaining the benchmarks, independent of how
fast anything ran. The six new benchmarks were chosen to reach the parts of
the language the first ten did not -- `reusable` pools, per-input arenas
inside recursion, `return from` as a taken error path, inline child arrays,
DTOs with nested variable-size parts -- and most of what follows came out of
that.

**Fixed this round** (details in the compiler section above): recursive
builders can build into a pool passed by reference; references push correctly
into limited arrays of relative references; call results and inlined values
land in `varint`-length slots with the right layout; a fixed struct may refer
to its own type.

**Fixed earlier, by the sized-integer change:** `varint` fields accept
computed values; compound assignment to narrower storage works; field
initialisers need no `as` from a value the compiler can see is in range.

**Still open, re-verified against the current compiler:**

* **Reference laundering defeats relative references.** A reference read back
  out of a container is conservatively rooted at that container (spec 9.5), so
  it cannot then be stored into a relative-reference field belonging to the
  array it actually points into. `graph` keeps its per-vertex list heads in
  the same pool as the edges to work around it, and `lru`'s map holds pool
  *indices* rather than `Node?` references for the same reason: a node
  reached through the map could not be linked relatively into the pool.
* **A recursive result's root is whatever the callee has recorded so far.**
  A `recursive fn` must return something rooted at the pool *before* the back
  edge whose result it stores, so the base case goes first (`scene`,
  `bintrees`, `tree`); in a mutually recursive parser the entry function has
  no return before the back edge at all, so `calc` wraps the parenthesised
  subexpression in a Paren node holding a plain reference. Spec TODO 0g.
* **A `null`-reachable optional is rooted at static data**, which is why
  `sexp`'s links are still plain 8-byte references.
* **Inline child arrays need their count at construction.** The A.2 shape
  `(T&<u32>)[varint]` is only reachable when the children exist before the
  parent is built and their number is known; a builder that discovers them
  as it goes uses a limited `[..k]` array inside a fixed-size node (`scene`,
  17 bytes for four links) or sibling links (`sexp`).
* **Elements that are relative references iterate by index.** `for k in
  n.kids` is a copy of a relative reference and is rejected; `for &k` yields a
  reference to the slot, so the loop is written `for i in n.kids.len {
  f(n.kids[i]) }`.
* **Grow-only arrays have no `clear`.** A scratch buffer refilled per iteration
  must be grow-shrink (`[>..<]`), which then cannot hand out references or
  slices at all. `calc` reads its input through a global grow-shrink buffer by
  index for that reason; a per-input local is the alternative when the buffer
  can be a fresh allocation each time.
* **Arithmetic at the operands' width bites on `u8` data.** The first version
  of `blur` summed nine `u8` taps in `u8` and was a very fast non-blur; every
  tap needs `as u16`. C promotes to `int` silently and Rust would panic in a
  debug build; Goose wraps, as the spec says it will (6.2), but nothing in the
  source points at it.
* **Two parses to know about.** `x as u64 & mask` reads `u64&` as a reference
  type (parenthesise the cast), and an `if`/`else if` chain that ends without
  an `else` in tail position is an error (write early returns).
* **Relative references trade speed for size in code that relinks.** They
  cost nothing measurable against indices on the five benchmarks that build
  a structure once and walk it, and 1.5x on `lru`, which relinks six links
  per hit. The right link type is a per-structure decision, and the suite now
  has one benchmark on each side of it.
* **A fixed struct can hold references to its own type** (`struct Node {
  next: Node? }`) -- the generated C had declared such a struct after its own
  first use and did not compile; fixed this round with a forward typedef.
* **Thread queues cannot carry a relative-reference structure** (not flat,
  TODO 9), and save/load needs the whole-region copy of TODO 16, so the two
  design.md advantages that remain unbenchmarked are unbenchmarkable today.

**What worked without any friction:** grow-only pools with interior references,
relative references inside a single root, variable-mode ADTs with inline
variable-size payloads, case functions as the dispatch mechanism, `reusable`
pools with sentinel-linked lists, limited arrays of relative references inside
pool elements, a DTO with a variable-size string and an inline list of
variable-size records built by a function returning straight into the field,
`return ... from` taken on a third of all inputs from three levels of
recursion, and per-input local pools reset by scope exit.

## What writing the Rust benchmarks taught us

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
them; the Goose flat row under the same LLVM is 4x slower for a different
reason (the fat-reference reloads above). Conversely the Goose row-slice form
elides everything at compile time and MSVC, which does not vectorise checked
loops, then runs it 4x faster.
