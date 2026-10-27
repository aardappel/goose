## October 16, 2026 refresh

The main tables are a fresh run of all sixteen benchmarks, at all three
sizes, under MSVC, clang-cl, rustc and Goose's TinyCC backend. The compiler
was rebuilt in Release from `f6d31d0e`, with the benchmark source changes in
this refresh and no optional native packages:

```powershell
cmake -S . -B build-bench-refresh -DGOOSE_BUILTIN_LIBS=
cmake --build build-bench-refresh --config Release --parallel 1 -- /nodeReuse:false
python bench/run_bench.py --exe build-bench-refresh/Release/goose.exe
```

The default measurement policy is unchanged: two warm-ups, then the best
of three whole-process runs, with checksum agreement across implementations.
Input construction, validation, traversal, and teardown remain timed. No
bounds checks are disabled in the headline measurements. The JIT rows also
include compiling the program, and their peak memory includes this minimal
compiler build; compare them separately from native executables.

All 528 measurements succeeded. The 48 benchmark/size combinations agree
across implementations and with the saved previous run's checksums (after
normalizing line endings). The original and revised graph implementations
also agree at V=1, V=2 and V=17.

### Index types and checkfree kernels

Both graph implementations now store destinations and queue entries as
`u32 in dist`. The CSR implementation additionally stores prefix offsets as
`u32 upto out`. The arrays still have four-byte elements and perform the
same graph construction and four breadth-first searches. `dist` and `out`
are initialized before constructing the typed values they validate.

Both `bfs` functions are `checkfree`. A length assertion on the companion
head/offset array states its relationship to `dist` once per search. Every
access in traversal is then proven, including an index loaded from another
array. The CSR successor offset widens the vertex to i64 before adding one,
so the proof does not have to allow wrapping u32 arithmetic.

At the source files' default sizes, `--bce-lines` reports:

| source | index checks elided | slice checks elided | contract |
|---|---:|---:|---|
| graph | 12/12 | 0/0 | BFS |
| graph_csr | 23/27 | 0/0 | BFS; four checks remain in construction |
| blur | 20/20 | 0/0 | flat kernel |
| blur_assert | 20/20 | 0/0 | flat kernel with explicit precondition |
| blur_rows | 20/20 | 8/8 | row slices and stencil |
| scene | 12/12 | 0/0 | composition and recursive update |

The blur and scene annotations only enforce bounds-check elimination that
the compiler already did; they do not change the computation. An attempted
LRU rewrite could not prove masked probe indices from a dynamic table
length, so it was not adopted. This refresh makes no claim that every
benchmark is entirely check-free.

`bench/bce_ab.py` still supports the annotated sources: its `--no-bce`
diagnostic copy removes only the `checkfree` annotations, since their
purpose is to reject that build. It retains runtime bounds checks and
index-type validation. The ordinary source and all headline builds retain
their contracts.

### Paired comparison of the graph rewrite

Original sources from `f6d31d0e` and the revised sources were built with
the same fresh Release compiler and C toolchains. Each pair ran in
alternating order, with two discarded warm-up pairs and five measured
pairs. The table reports the best time of each version at V=2,000,000
(16,000,000 edges, four searches); checksums agree in every run.

| implementation | backend | original ms | revised ms | speedup |
|---|---|---:|---:|---:|
| graph | v145 | 4,708.4 | 3,883.8 | 1.21x |
| graph | clang | 4,822.5 | 4,050.0 | 1.19x |
| graph_csr | v145 | 809.6 | 749.1 | 1.08x |
| graph_csr | clang | 744.0 | 633.2 | 1.17x |

This measures the complete source rewrite, including global array placement,
unsigned index storage and extraction of BFS. It does not isolate the cost
of removing bounds checks alone. The entry widths and storage requirements
are unchanged. The separate small-size CSR trials were not consistently faster;
the improvement claim is for these large inputs.

Raw full-suite measurements are saved locally in `bench/gen/results.json`;
the paired samples and source snapshots are in `bench/gen/refresh/`.
As with other generated benchmark files, that directory is ignored by Git.

## Historical analysis through October 10, 2026

**Everything below records earlier measurements and compiler experiments.**
Those A/B studies were not all repeated for the October 16 refresh. Their
times, elision counts, causal explanations and references to "this report"
describe the older run, not the current tables. In particular, the old
conclusion that graph indices loaded from memory cannot be proven is
superseded by the index-type implementation above. Use the current generated
tables and `summary.md` for present-day comparisons; the historical material
is retained as a record of the experiments.

## Reading the Rust rows (historical)

The Rust comparisons use safe, idiomatic implementations. C++ has separate
idiomatic and expert tiers. Where two Rust implementations differ in ownership
or data structure, both are listed:

* `strlist` has `Vec<String>` and `Vec<&str>`, which differ in *ownership*. Only
  the first can outlive the text it came from, which is the semantics the Goose
  row has.
* `tree`, `interp`, `sexp`, `scene`, `calc` and `bintrees` have an
  owning-pointer row (`Box`) and an arena row (`Vec` plus `u32` indices). Both
  are safe and both are idiomatic; the arena is what the community recommends
  for anything pointer-heavy, and `design.md` predicted it would be the real
  comparison point rather than `Box`. That prediction held: the arena row is
  2.5x (`calc`) to 14x (`bintrees`) faster than the `Box` row on the six
  benchmarks that have both.
* `graph` has three rows for the same reason the C++ side does: `Vec<Vec>`, CSR,
  and the one-pass index-linked arena. CSR is the fastest in every language
  and is index-based in every language, so `graph` is the only benchmark where
  the references-versus-indices question does not show up in the fastest row.
* `words` and `lru` have both a `HashMap` row and a hand-written open-addressed
  table. std's default hasher prioritizes security over speed. Rust programmers
  commonly substitute a hasher crate, but this suite allows only std. In `lru`,
  both rows use the same `Vec` arena and free list for the nodes; only the map
  differs. The arena is also the idiomatic choice here: std has no intrusive
  list, and `LinkedList` provides no way to refer to an individual node.
* `respond` has a DTO row (`String` and `Vec<Item>` built per request, rendered
  by a function returning a fresh `String`) and a streaming row that writes the
  JSON directly into one reused `String`, mirroring the two Goose rows.
* `blur` has a version that indexes every element and a version using row slices
  and `windows(3)` iterators. The latter is a common way to avoid bounds checks
  when profiling shows they are costly. They are not costly here (see the
  bounds-check section).

Everything else is a single row. The aggregate table compares Goose against
the fastest Rust implementation in this suite.

Build flags are `-O -C codegen-units=1`. Using one codegen unit matches the
C++ compilation model: every C++ row is one translation unit compiled as a
whole, and with the rustc default of 16 units Rust would not get the same
cross-function view. All other settings are the defaults a shipped Rust
binary uses: bounds checks on, unwinding panics, and no `target-cpu` beyond
the x86-64 baseline that the C++ rows also build for. Goose and Rust are
therefore both checked, with C++ as the unchecked baseline.

### Data layouts and memory use

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
4.1x. Rust's 32 bytes against MSVC's 48 comes from two things: `String` is
three words where `std::string` is four, and the enum tag is stored in the
pointer's niche rather than needing its own word. But it is still a *fixed*
enum, so every one of the 16M elements is sized for `Say` whether it is a
`Say` or not, and the text needs a second allocation as well. This is a
property of Rust's enum layout. `records_fixed.goose` provides a control
with a fixed-size representation.

Three Goose layouts differ substantially from the corresponding C++ and Rust
implementations:

* A `sexp` node is only as big as its variant needs. Its symbol bytes are
  stored inline after a varint length, and its two links are 4-byte offsets
  into the pool. This makes the program 1.3x smaller than the same program
  with 8-byte links, which is what a language that cannot make a null link
  relative would have to use.
* A `scene` node is a 117-byte record whose four child links are 4-byte
  offsets in a limited array inside the record (spec 5.3, 3.9), so the whole
  scene is one contiguous, position-independent block. The arena rows in C++
  and Rust both reach 120 bytes with `uint32` indices, and their idiomatic
  rows add a heap `vector`/`Vec` per node for the children on top of 128
  bytes.
* A `calc` binary node is 6 bytes: a tag, an operator byte and two 2-byte
  relative references, which are enough because a per-input tree never spans
  more than a few hundred bytes. The arenas' `u32` links make the same node
  12-16 bytes, and the owning-pointer rows make it 24-32 bytes plus a heap
  block per node.

## Reading the toolchain table

The two backends are within 10% on most rows and neither dominates. The gaps
that remain:

* **clang vectorises what v145 does not.** Goose's flat `blur` is the largest
  gap in the suite at 3.64x (930 against 256 ms), and the next two are the
  C++ `__restrict` row of the same kernel at 1.83x and `particles cpp SoA` at
  1.62x. Every check in the flat kernel is proven, so what stops v145 from
  vectorising it is aliasing: MSVC reports a possible loop-carried dependence
  between the loads through `src` and the stores through `dst` (C5002,
  reason 1200) and leaves the loop scalar. It does vectorise the row-slice
  form, where the gap is 1.12x.
* **v145 is better at `std::vector::push_back` and at dispatch.** `push cpp
  vector+reserve` is 0.61 (185 ms against 301), and clang trails on every
  `interp` row (0.84-0.91) and on `sum` (0.93 on the reserved vector, 0.97
  on Goose).
* **clang is consistently 5-25% ahead on string, hash, allocator and
  float-and-pointer work**: `words` (1.10-1.12 on the C++ rows), `strlist`
  (1.06-1.13), `records variant+string` (1.14), `sexp unique_ptr` (1.16),
  `respond` (1.16 and 1.23 on the two C++ rows), `calc cpp arena` (1.16), and
  `scene`, where it is 1.21 on the Goose row and 1.10 on the C++ arena.
* On Goose's own rows: `records` and `interp` favour v145 (0.89 and 0.91),
  `respond` streaming, `particles` elementwise and `blur_rows` favour clang
  by 12-14%, `words` by 1.28-1.33, and `lru` by 1.53, because its
  pool-relative loads are base-plus-offset loads, which clang schedules and
  v145 does not.
  `bintrees`, which favoured v145 at 0.84, is now level.

Three conclusions change with the backend, which is why every ratio in this
report is given per backend: against the best safe Rust row, `lru` is 0.72x
under v145 and 1.10x under clang, `scene` 0.83x and 1.00x, and `blur` 0.98x
and 1.10x. `graph` and `bintrees`, which used to be on this list, are on the
same side of the Rust row under both backends now.

## Reading the JIT table

TinyCC keeps a C local in a stack slot and holds values in registers only
within one expression. The generated C reads each operand into a temporary of
its own before later operands run, so that a later operand cannot change what
an earlier one read. clang and MSVC optimize those temporaries away, but
TinyCC turns each one into a store and a reload. This accounts for most of
the increase in the JIT rows from 2.96x their compiled time in the previous
report to 3.48x in this one. On `blur`, whose kernel is one long expression
per pixel, bisecting the JIT slowdown points to the commit that introduced
this operand ordering; that commit took the W=2048 row from 365 to 666 ms.

## Bounds-check elimination, measured

The compiler proves most index and slice checks away (spec 10.5).
`bench/bce_ab.py` builds each benchmark with and without `--no-bce` under both
toolchains, at the size baked into each source file rather than the report's
`large` one. `--no-bce` disables more than check elimination: the loop-view
hoist decides whether a loop's array length is invariant by asking BCE's
kill summary, so with the pass off the view is re-read every iteration too.
The A/B measures both together. The counts are per generated function, so a
kernel inlined at two call sites counts its checks twice.

| benchmark | index checks elided | slice | v145 gain | clang gain |
|---|---|---|---:|---:|
| blur | 20/20 | 0/0 | **+82%** | +3% |
| blur_assert | 20/20 | 0/0 | **+81%** | -1% |
| blur_rows | 20/20 | 8/8 | **+191%** | -1% |
| lru | 6/20 | 0/0 | +2% | +7% |
| scene | 12/12 | 0/0 | +5% | 0% |
| respond | 3/3 | 3/3 | +4% | -2% |
| strlist | 1/2 | 1/2 | +4% | -2% |
| calc | 19/22 | 0/0 | +1% | -1% |
| records | 0/1 | 0/0 | 0% | +1% |
| sexp | 11/12 | 0/1 | 0% | 0% |
| graph_csr | 15/21 | 0/0 | 0% | -3% |
| graph | 7/9 | 0/0 | 0% | -6% |
| words | 60/67 | 11/12 | -7% | +1% |
| sum, push, tree, interp, particles, bintrees | no index expressions at all | | | |

For `words` under v145 and `graph` under clang, the binary with the pass on
measures *slower*. This is the noise floor of a cache-bound random-access
row, not a real result. Everything between -7% and +7% falls in the same
category, which now includes `graph_csr`'s clang column (+31% in the
previous round).

**The row where it matters is the image kernel, and only under v145.** At
W=2048, 16 passes, ms, best of four:

| | v145 | clang |
|---|---:|---:|
| `blur`: `src[y*W + x]`, 20/20 | 68 | 23 |
| `blur_assert`: the same plus a now redundant `assert(src.len == W * W)`, 20/20 | 68 | 23 |
| `blur_rows`: row slices with a length assert each, 20/20 | 26 | 23 |
| any of the three with `--no-bce` | 124-74 | 23-24 |

Under v145 the nine checks per pixel keep the loop scalar: proving them away
makes the flat kernel 1.8x faster and the row-slice form 2.9x faster. The
flat kernel stays scalar even with every check gone, because MSVC cannot
rule out that the stores through `dst` feed later loads through `src`. The
row-slice form does vectorise, which accounts for the remaining 2.6x between
the two. Under clang the checks have no measurable cost: clang vectorises
the checked loop to run as fast as the unchecked one, as it does for the
Rust `blur_index` row, which keeps ten `panic_bounds_check` sites in its
assembly and runs at the speed of the checkless `windows` row. In the
previous round clang also needed the *loop-view hoist*, which accounted for
most of a +327% on the `blur` row, because `src` and `dst` were fat
reference parameters whose headers a byte store through `dst` might change.
The kernel is now inlined at both of its calls in `main`, where the two
arrays are locals whose headers no store can reach, and the clang build is
as fast with the pass off as with it on.

`blur` proves its checks from what `main` itself knows: that both arrays are
`W * W` long. Before the kernel was inlined, the same facts reached it
through its call sites. Every call site of a specialization records what it
proves about the lengths and integers it passes, and the body starts with
the meet of those facts (spec 10.5). The fact stays valid between the two
calls because a call does not kill it: a callee's effects on its callers'
arrays are summarized, and a kernel that only reads and writes elements has
no such effects. So at the second call in `blur(a, b); blur(b, a)` the
compiler knows as much as at the first. The `assert` in `blur_assert.goose`
is redundant, and the two rows time the same. The row slices in
`blur_rows.goose` also lose their slice checks, since each slice's upper
bound is compared against a length the kernel knows.

**Every check that survives elsewhere is an index that came out of memory.**
Examples are `dist[u]` where `u = q[qh]`, `dist[w]` where `w = cur.to`,
`out[cursor[s]]` in the CSR fill, and `pool[slots[at].node]` throughout
`lru`. The analysis proves the checks on every index derived from loop
counters, lengths, constant masks and reductions, and on none that was
loaded from a data structure, as `test/optimizer/bce.goose` documents.
`blur` adds a second class of index, the *product* of two counters. For that
idiom the difference-constraint domain has one-shot product and
two-term-sum bases, which is also what gets `scene` to 12/12.

Four rewrites were tried to see whether any natural form lets the compiler
prove the loaded-index checks, and none is worth adopting:

* making `graph`'s arrays globals (the invariant would have to hold on array
  *contents*);
* deriving `words`'s mask from `slots.len`;
* using the documented `%` reduction (the range fact does not survive the
  loop back-edge);
* one `assert` at the top of the probe loop.

All four time within noise of each other, because those checks are
perfectly predicted branches on values already in registers. Array-contents
invariants themselves were implemented and measured. They removed more
checks on the two `graph` rows but saved no measurable time on either, so
they were dropped. The `blur` measurement above is the counterexample: a
check per load in a loop that would otherwise vectorise has a large cost.

## What the standard library costs the benchmarks

Seven of the sixteen benchmarks use `stdlib/` functions in place of
hand-written code with the same behavior. Each adoption was measured on its
own against the hand-written form it replaced, at the `large` size, under
both toolchains, best of three interleaved runs (above 1.00 means the
library form is faster):

| row | what it uses | v145 | clang |
|---|---|---:|---:|
| `strlist` | `each_split` | 1.04 | 1.07 |
| `words` | `each_split`, `hash`, `next_pow2`, `push_n` | 1.01 | 1.03 |
| `graph`, `graph_csr` | `fill` | 0.98 | 1.04 |
| `lru` | `next_pow2`, `push_n` | 1.03 | 0.98 |
| `sexp` | `format_int` | 1.00 | 1.01 |
| `particles`, `particles_scalar`, `scene` | `vec`'s `float3` | 0.99 | 1.00 |

The adopted library functions add no measurable cost; on two benchmarks they
are slightly faster than the loops they replace. The `words` figures were
measured before std's hash started reading 8 bytes per step. Compared with
the byte-wise FNV-1a that std's hash used to be, which is also what the C++
and Rust tables hash with, the current hash measures 0.94 under v145 and
1.19 under clang on that row. `respond` and `respond_stream` also used
`hash`, as the checksum of each rendered response, and now compute FNV-1a
in their own code instead: every row of a benchmark has to print the same
checksum, and the other languages' rows compute FNV-1a. `words` also keeps
a second row, `words_dictionary.goose`, which replaces the hand-written
table with `dictionary<u8[:], i32>`. That row is a data-structure
comparison rather than a comparison of notation, and its results are in the
per-benchmark table.

Three other substitutions were tested but not adopted:

* **`format_int` in `respond` and `respond_stream`** costs 3-5% under v145
  against the hand-written digit loop, and nothing under clang. Most of the
  original gap came from `format_uint` dividing by its `base` parameter:
  that is a real division per digit, where the hand-written loop divides by
  the constant 10. Giving base ten its own loop in the library recovered
  about half of the gap. What is left is the extra call layer and the
  padding branch, on a row that renders four numbers per request and does
  nothing else expensive. `sexp`, which formats one number per node among
  much other work, shows no measurable difference.
* **`push_n` in `graph` and `graph_csr`** costs 2-3%: the generic `push` loop
  does not compile to the specialised one, and it also makes the destination
  array's length unprovable to the bounds-check pass a few statements later.
  `fill` alone, which is where the readability gain was, has no measurable
  cost.
* **`push_n` of a value containing self-relative references** does not
  compile: `push_n<A, T>(xs: A&, v: T, n: i64)` takes its value by value,
  and such a value cannot be copied (spec 3.9). `graph`'s edge-head fill
  stays a loop for that reason. A `T&` parameter would not help either: the
  library would still be copying out of it.

Benchmark code also has to avoid names that hide library functions. The
`std` functions are global, so a local named `fill` shadows the library
function, and the error appears at the call rather than at the declaration.
`graph_csr`'s CSR write cursor is called `cursor` for that reason, and the
name also describes what it is.

## Where Goose loses

**Relinking through relative references is the mutation-heavy case, and the
pool-relative form is what makes it competitive.** `lru` is the only
benchmark in the suite that mutates links rather than building them once.
With self-relative links and index slots it is 1.5x behind the Rust and C++
arenas. With spec 3.9's `in pool` form, the nodes and the 8-byte map slots
hold 4-byte offsets from the pool's base, and `pool.free(pool.index_of(n))`
turns a link back into a freelist index. In that form the row takes
3,163/2,065 ms against the Rust arena's 2,274 and the C++ arena's
2,607/2,437: 1.10x and 1.18x ahead of them under clang and 1.39x and 1.21x
behind under v145, while using the least memory of the three. The index
form is kept as `lru_indices.goose`; measured back to back in an earlier
round, it was level under v145 and 1.31x slower under clang. The table below
shows the same program written in more ways, at the `large` size, all with
the map holding indices, also from an earlier round:

| links | node | v145 | clang |
|---|---:|---:|---:|
| `Node&<u32>?` self-relative | 16 | 3,738 | 3,189 |
| `Node&<u64>?` self-relative, no range check | 20 | 3,573 | 2,881 |
| `Node?` plain references | 24 | 2,557 | 2,352 |
| `i32` indices, `pool[i]` bounds-checked (the Rust shape) | 16 | 2,397 | 2,266 |

Plain references and indices are level, so the cost comes neither from
references as such nor from the `reusable` pool (every variant frees and
reuses through the same freelist, 1.3M times). It comes from the
self-relative encoding. Every relink (six per hit) computes an offset from
two addresses and stores it, and every load needs a null test and an add
before the next load can issue. The range check accounts for a tenth of that
cost under clang and none of it under v145; the rest is the arithmetic on
the pointer-chasing critical path. The links were made relative to save 8
bytes per node against plain references, but `lru` gets no benefit from
that, because the index form is also 16 bytes. On the build-once benchmarks
(`tree`, `interp`, `sexp`, `scene`, `bintrees`) the same links have no
measurable cost against indices, so the cost is specific to the relink
pattern. The pool-relative form replaces the subtraction of the field's
address with a subtraction of a base held in a register, and replaces the
load with `base + off`. clang schedules that much better than v145; this
accounts for the 1.53x between the two backends on this row, and for all of
the remaining gap. The build-once benchmarks keep self-relative links,
because they want the position independence and the links have no
measurable cost against indices there.

**Recursive construction is no longer one of the losses, except in `calc`.**
`bintrees` builds and discards 34M nodes in recursive calls, and used to trail
the Rust arena by 24% under clang (282 against 228 ms). The builder takes its
pool by reference, and the compiler now keeps that pool's top and count in
locals across the recursion instead of going through its header at every
push. With that, Goose takes 196/191 ms against the Rust arena's 221 and the
C++ arena's 378/391. On `tree`, which builds a tree of the same nodes once
and sums it eight times, Goose is 6-10% ahead of the Rust arena. `calc` runs
at 0.84-0.87x of the Rust arena; the remaining costs there are the global
cursor, the `return from` discriminant and a varint decode on values that
mostly need two bytes.

**Flat scalar kernels depend on backend optimization, with no memory
advantage.** The flat, indexed form of `blur` is 3.3x slower than flat Rust
under v145. MSVC leaves the loop scalar because it cannot rule out `dst`
aliasing `src`. Under clang, the same form is 1.10x *faster* than Rust (256
against 282 ms). Written over row slices, it is level with Rust under both
backends. `particles` is 2-4% ahead of Rust with identical memory use, which is
within the noise of that row. Neither benchmark shows a Goose advantage beyond
noise, and the design did not predict one.

**Pointer-linked adjacency loses to CSR, in all three languages.** On `graph`
the one-pass linked build is 5-6x slower to traverse than two-pass
count-then-fill, in every language. The Goose CSR row exists to show this is
a data-structure effect rather than a language one, and the Goose CSR
implementation is level with both: 791/793 ms against C++'s 803/824 and
Rust's 833.

**v145 is also a worse backend than clang for the float-and-pointer mix.**
`scene` is level with the Rust arena under clang and 21% behind under v145,
with the C++ arena 10% behind its own clang build on the same row. Padding
the 117-byte node to 120 changes nothing, so the packed layout is not the
cause.

## Caveats on these numbers

* Whole-process wall clock, so input generation is inside every measurement.
  Where generation is a large share of the work (`sexp`, `words`, `graph`,
  `calc`) the ratios are diluted toward 1.0 and the gap on the interesting
  phase is larger than the table shows.
* Best of three runs after two discarded warm-ups. The warm-ups are
  necessary: on this machine a freshly written executable takes up to 10x
  its steady-state time on its first execution and about three runs to
  settle, and the effect is far larger on clang-linked binaries than on
  MSVC-linked ones. Without the warm-ups the toolchain comparison would
  measure the malware scanner.
* One machine, no core pinning. Differences under ~10% are within measurement
  noise. The threshold is ~15% for the two cache-bound random-access cases,
  `graph`'s linked rows and `lru`: two byte-identical `lru` binaries measured
  14% apart in the bounds-check A/B.
* **The allocator-heavy rows are the least reproducible in the suite, and they
  are the ones the headline ratio rests on.** When the whole harness is run
  twice back to back, the Goose rows stay within 3% of their earlier times,
  as do the arena and CSR rows in every language, but the malloc-bound rows
  move by up to 23%. A difference of less than about 10% in the "vs
  idiomatic" geometric mean is not significant, and a single suite run is
  not enough to attribute a change to the compiler. That needs a per-commit
  A/B built from the same sources minutes apart, with `-falign-loops=32`
  under clang so that a shifted hot loop cannot look like a codegen change.
* **Reports are not comparable with each other across machine updates.**
  Between the previous report and this one, most C++ and Rust rows got up to
  6% faster, every row in every language got 3-4 MB smaller, and the
  process-start floor fell from 12.4 to 7.9 ms, with no change to their
  sources. Something outside the compilers changed, and MSVC changed too
  (19.51.36256 to .36257). The malloc-bound C++ rows moved most, `sexp`'s
  `unique_ptr` row by 1.42x. `tree`'s Goose row under v145 went from 956 to
  605 ms, although the C that the previous compiler generated for it runs in
  the same time today as the current compiler's C. Compare compiler versions
  by building both and running them side by side.
* This CPU has a very large L3, so `small` and `medium` can sit entirely in
  cache. The `large` rows are the ones that stress the memory subsystem, and
  `calc`, `respond` and `bintrees` never leave the cache by design: each
  one's working set is a single input, request or tree.
* The process-start floor is not constant between runs, and it is inside every
  `small` cell, so read those as differences from the floor and read ratios
  only at `large`.
* Rust rows are a single rustc time, not one per backend. rustc is LLVM, so
  the clang column is the one they are most directly comparable with; the
  Goose-vs-Rust ratios are reported against both Goose builds anyway.

## What the benchmarks say about the language

These findings come from writing and maintaining the sixteen benchmarks,
independent of how fast anything ran. Together the benchmarks use
`reusable` pools, per-input arenas inside recursion, `return from` as a
taken error path, inline child arrays, DTOs with nested variable-size
parts, and the standard library.

**Features that worked well:**

* grow-only pools with interior references;
* relative references inside a single root;
* variable-mode ADTs with inline variable-size payloads;
* case functions as the dispatch mechanism;
* `reusable` pools with sentinel-linked lists;
* limited arrays of relative references inside pool elements;
* a DTO with a variable-size string and an inline list of variable-size
  records, built by a function returning directly into the field;
* `return ... from` taken on a third of all inputs from three levels of
  recursion;
* per-input local pools reset by scope exit;
* a recursive builder building into a pool it is handed by reference.

**Difficulties encountered:**

* **Inline child arrays need their count at construction.** The A.2 shape
  `(T&<u32>)[varint]` requires the children to exist and their number to be
  known before the parent is built. A builder that discovers children as it goes
  uses a limited `[..k]` array inside a fixed-size node (`scene`, 17 bytes for
  four links), or sibling links (`sexp`).
* **A plain reference parameter cannot be stored relatively outside a
  recursive cycle.** `fn link(prev: Node?) { pool.push(Node { next: prev }) }`
  is rejected at any call site that passes a pool-rooted reference: the
  parameter's root is its own per-call-site class, and nothing identifies that
  class with the global the argument came from. Inside a `recursive` cycle the
  pool-class machinery (spec 7.8) does make the identification, which is why
  `sexp`'s parser can do exactly this. The same identification outside a cycle
  is the natural fix.
* **Arithmetic at the operands' width bites on `u8` data.** The first version
  of `blur` summed nine `u8` taps in `u8` and produced incorrect results; every
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
  that relinks; pool-relative ones do not.** Self-relative links have no
  measurable cost against indices on the five benchmarks that build a
  structure once and walk it, but cost 1.5x on `lru`, which relinks six
  links per hit. Spec 3.9's `in pool` form (offsets from a named global
  pool's base) turns the relink into a subtraction from a register and lets
  other arrays hold 4-byte links into the pool; with it, `lru` is level with
  the arenas under clang. The encoding is a per-field decision:
  self-relative for blobs and for local pools, `in pool` for relink-heavy
  structures in a global pool.
* **Thread queues cannot carry a relative-reference structure** (not flat,
  TODO 9), and at the time of these measurements, save/load also lacked the
  whole-region copy of TODO 16. Serialization has since been implemented
  (`docs/design/serialization.md`); queues can carry its byte images, but
  cannot directly carry values containing relative references.

## What the Rust implementations say about Rust

These findings come from implementing the suite in Rust, independent of how
fast anything ran. In these measurements, Rust is closer to Goose than
idiomatic C++ on speed. The implementation differences below help explain
the remaining gaps.

**`push` requires indices in the Rust implementation.** `push` keeps a
pointer to every 64th element of an array it is still appending to. In Goose
that is `marks.push(items.push(...))`, and the reference stays valid for the
array's lifetime. In Rust the equivalent does not compile: a `&Item` borrows
`items` for as long as it is held, so the following `items.push` is
rejected. No std container is both pointer-stable and O(1)-append. The row
uses indices because that is what a Rust programmer writes; the interesting
difference is in the source code, not in the timings.

**Several Rust implementations use indices where Goose uses references.**
This applies to `tree`, `interp`, `graph`, `sexp`, `scene`, `calc`,
`bintrees` and `lru`: the fast, safe, recommended Rust form is a `Vec` arena
with `u32` links, as `design.md` predicted. The arena does not give up
safety, since an out-of-range index panics like any other Rust index. Its
costs are these: the type says `u32` rather than what it points at;
`Option<&T>` becomes a sentinel that occupies a real slot; and an index into
a pool that is later reused goes stale in a way the compiler cannot detect,
where a reference would have been rejected. Goose gets the same layouts with
`TNode&<u32>?`, which is checked, nullable, and typed to the thing it points
at.

**The arena is sometimes harder to write than the `Box` row.** In
`scene_arena.rs`, `update` cannot compose into the node in place:
`&pool[i].local` and `&mut pool[i].world` are two borrows of one `Vec`, and
neither `Index` nor `IndexMut` can split them. So the transform is built in
a stack local and copied in, and the child list is copied out of the node
before recursing. `scene_box.rs` has neither problem, because disjoint
fields of one `&mut` borrow independently. Goose composes directly through
`Xf&` references into the pool element.

**The idiomatic LRU is already the arena.** std has no intrusive list, and
`LinkedList` provides no handle to an individual node, so O(1)
move-to-front by handle needs `Rc<RefCell<Node>>` (two allocations and a
refcount pair per node) or the `Vec`-plus-indices arena. Both Rust rows use
the arena; they differ only in whether the map is `HashMap` (SipHash) or
open addressing.

**Rust enums are the best fixed-tag ADT of the three.** They have a byte
discriminant, a jump table from `match`, no vtable pointer, and a niche
optimisation that hides the tag inside a payload pointer. That is enough to
beat `std::variant` on both time and memory in `records` and `interp`. But
a Rust enum is still a fixed enum: every element is sized for the largest
variant. Variable-mode enums are the only Goose feature in this suite that
has no direct counterpart in Rust's builtin enums.

**`Result` and `?` work well in `calc`.** The Rust code reads like the C++
exception implementation and has a runtime cost similar to the error-code
implementation. The arena version gets an O(1) teardown from
`Vec::clear()`, but only because `Expr` owns nothing. If a node held a
`String`, the clear would become a per-node drop walk again; that is the
ordinary case, and one that never arises in Goose.

**In two cases, Rust's idiomatic form is already the fast one.** `collect()`
over a `TrustedLen` iterator preallocates the exact size, so the "did you
remember to `reserve`" gap that `sum` and `push` measure against C++ does
not exist against Rust. And borrowing is the default, so `Vec<&str>` and
`HashMap<&str, _>` are what a Rust programmer writes first. Both are real
language-design advantages over C++, and both reduce the Goose margin.

**Where Rust still pays:**

* `Box::new(T { .. })` builds the value in a stack temporary and moves it
  to the heap (there is no guaranteed construct-in-place; spec 4.3/7.3).
* Teardown is real work, and it is the same cache-hostile pointer chase
  that building was (`interp`'s `Box` row is behind C++ virtual dispatch;
  `bintrees` consists of nothing else).
* std's `HashMap` hashes with SipHash-1-3 by policy.
* The borrow that makes `sexp`'s arena row fast works only because the text
  is complete before parsing starts.

**LLVM removes what the Goose pass cannot, and keeps what it can.** Rust's
`blur_index` row keeps ten `panic_bounds_check` sites in its assembly and
runs as fast as the checkless `windows` row, because LLVM vectorises around
them. The Goose flat row under v145 is 3.3x slower for a different reason:
its checks are all proven, but MSVC cannot rule out that the stores through
`dst` alias the loads through `src`, so it leaves the loop scalar. The Goose
row-slice form, which MSVC does vectorise, runs 3.3x faster than the flat
one.

## Where the Goose-Rust gap comes from, and where it can still move

The sixteen rows fall into three groups by *why* the Rust comparison comes
out as it does, and only two of the three groups are worth working on.

**Gains from data representation.** The clearest gains come from differences
in layout and reference rules:

* *Variable-mode enums.* `records` is 2.1-2.3x faster on 4.1x less memory
  because each element takes the space of its own variant rather than of
  the largest one, and a `Say`'s text is inline rather than a second
  allocation. `interp` and `sexp` show the same property in a tree. Rust's
  enum is the best fixed-tag ADT of the three languages and still uses the
  maximum payload size for every element; changing that requires a
  different representation.
* *Variable-size parts stored inside records.* `respond` builds a DTO
  whose string, item list and per-item sku are all inline, renders it with no
  allocation anywhere, and runs level with the streaming rows, which never
  build the object. The Rust DTO allocates per string and per list, per
  request.
* *References into a container that is still growing.* Safe Rust cannot
  express this at all for `push`, and for `tree`, `graph`, `scene`,
  `bintrees` and `calc` it can express it only as `u32` indices. Goose gets
  the arena's layout with the types of the pointer version.
* *Links narrower than a pointer.* `sexp`'s nodes link with 4-byte offsets,
  `calc`'s with 2-byte ones, and `scene`'s four child links are stored in a
  17-byte array inside the node. Rust's arena also gets down to 4 bytes, but
  as an index, which is untyped, needs a sentinel slot, and goes stale
  silently when the pool is reused.

**Backend, not language.** This group is `lru` under v145 (0.72x), `scene`
under v145 (0.83x), and `blur`'s flat form under v145 (0.98x). In all three
the C++ row built by the *same* toolchain loses in the same direction, so
the gap comes from what MSVC makes of a given C shape, not from what Goose
asked for. Changing the language would not close any of these gaps, and the
upper limit is visible: each of these rows is level or ahead under clang.
`bintrees` under clang (0.81x) was on this list, but turned out not to be a
backend effect: keeping the builder's pool top and count in locals put it
1.16x ahead of the Rust arena.

**Costs required by the chosen semantics.** The bounds checks that survive
are on indices loaded from a data structure, which is where a check is not
redundant. `calc`'s `return from` discriminant serves a real error path;
the C++ error-code row also pays this cost, and the exception row pays far
more. These are not gaps to close.

**What would extend the lead, in order:**

1. **Identify a reference parameter's root class with its pool outside a
   recursive cycle** (item 1 below). Narrow links provide the memory
   advantage, and today a structure linked by them can be built only by
   functions inside the cycle that builds it. Removing that restriction
   would make the advantage available to ordinary code, not only to parsers.
2. **Cross-array narrow links, everywhere they typecheck.** `T&<u32 in pool>`
   already lets a side table hold 4-byte links into a pool instead of
   indices. `lru`'s map does this, and a grow-only array of such links works
   the same way, so `sexp`'s root list is a candidate. This idiom should be
   the default advice for a side table, rather than an `lru` special case.
3. **Keep looking for places a root requirement is not really needed.** A
   sentinel-ended chain does not force plain 8-byte links, because null
   needs no root; that alone accounts for 1.3x of `sexp`'s memory. The same
   question is worth asking of every rule that currently demands an exact
   root: what does the *representation* actually require?

## Compiler work, ordered by measured benefit

The first item of the previous round, carrying array lengths across calls,
is done. A call site's facts about the arguments now reach the callee's
entry, and a call kills only facts about arrays the callee can actually
resize. As a result `blur` proved all ten checks from what `main` knows and
ran at the speed of the asserted kernel under both toolchains (1.75x under
v145, 141 to 77 ms at the time). What remains:

1. **Identify a reference parameter's root class with its source pool outside
   recursive cycles too.** A non-recursive function that takes a `Node?` and
   stores it into a `Node&<u32>?` field of the same pool is rejected today. The
   identical code inside a `recursive` cycle is accepted; the cycle machinery
   already computes the information needed. This restriction prevents
   linked-structure helpers from being factored out of the cycle that builds the
   structure.
2. **Specialise the library's element loops.** `push_n` costs 2-3% against the
   `for` loop it replaces and blocks a bounds-check elision downstream, which
   is why two benchmarks kept their loops. The generic form
   should compile to what the loop compiles to.
3. **Inline a base case that is not the first statement** (`scene`,
   `interp`), and match expression-bodied folds (`if c { 0 } else { 1 + f(x) }`)
   in tail-recursion elimination.

Measurements ruled out the following ideas:

* Array-contents invariants for the bounds-check pass: more checks removed, but
  no measurable time saved.

* Merging tag dispatch: a 1% gain.

* Dropping `#pragma pack(1)` where the layout is already gap-free: no difference
  on either backend.

* Fusing an *optional* relative-reference load with its null test: both backends
  get worse.

* Rewriting benchmark code so the bounds-check pass can prove loaded indices: no
  measurable gain.
