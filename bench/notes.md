## Reading the Rust rows

Rust gets one target here rather than the two tiers C++ gets, because the
language points much more clearly at one way of doing things and its community
is unusually firm about what that is. Where a benchmark genuinely has two
idiomatic shapes, both are listed, and the reason is never "one of them is
faster":

* `strlist` has `Vec<String>` and `Vec<&str>`, which differ in *ownership*. Only
  the first can outlive the text it came from, which is the semantics the Goose
  row has.
* `tree`, `interp` and `sexp` have an owning-pointer row (`Box`) and an arena row
  (`Vec` plus `u32` indices). Both are safe and both are idiomatic; the arena is
  what the community recommends for anything pointer-heavy, and `design.md`
  predicted it would be the real comparison point rather than `Box`. It was, by
  2.8x to 5.9x.
* `graph` has three rows for the same reason the C++ side does: `Vec<Vec>`, CSR,
  and the one-pass index-linked arena. CSR wins there in every language and is
  index-based in every language, so `graph` is the one place where the
  references-versus-indices question does not show up in the winning row.
* `words` has `HashMap<&str, i32>` and a hand-rolled open-addressed table,
  because std's default hasher is a security policy rather than a speed choice,
  and the usual Rust answer to that -- a hasher crate -- is outside a std-only
  suite.

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

Measured with `size_of` and `sizeof`:

| | Rust | C++ | Goose |
|---|---:|---:|---:|
| `tree` node | 12 arena / 24 `Box` | 12 arena / 24 `unique_ptr` | 12 |
| `interp` node | 12 | 12, both `variant` and union | 5 leaf, 9 binary |
| `sexp` node | 24 arena / 32 + heap | 24 arena / 64 + heap | variable, text inline |
| `records` event | 32 + heap for `Say` | 48 + heap / 24 buffer | variable |
| string header | `String` 24, `&str` 16 | `std::string` 32 | none: inline varint |

The `records` row is where Rust beats C++ outright and still loses to Goose by
4.1x. Rust's 32 bytes against MSVC's 48 comes from two things: `String` is three
words where `std::string` is four, and the enum tag hides in the pointer's
niche rather than needing its own word. But it is still a *fixed* enum, so every
one of the 16M elements is sized for `Say` whether it is a `Say` or not, and the
text is a second allocation on top. That is the thing no amount of Rust skill
removes, and it is what `records_fixed.goose` is the control for.

## Reading the toolchain table

The two backends are within 10% on most rows and neither dominates. The gaps
that remain:

* **clang vectorises struct-of-arrays float loops; v145 does not.** `particles
  cpp SoA` is the largest gap in the suite at 1.66x (1,983 vs 1,193 ms). The
  AoS rows -- which is the shape Goose emits -- are within 11-25%, so this costs
  Goose nothing today, but it is the shape any future SIMD work would take.
* **clang exploits a wider ISA when allowed.** With `/arch:AVX2` on `sum`,
  clang improves 13% and v145 1%. Neither compiler goes above SSE2 by default,
  so both languages are leaving that on the table equally. (Measured a round
  ago and not repeated since; the default-flags rows above are current.)
* **v145 is better at `std::vector::push_back`.** `push cpp vector+reserve` is
  0.61 -- v145 185 ms against clang 302. clang also trails on both `interp`
  arena rows.
* **clang is consistently ~10-16% ahead on string and hash work** (`words`,
  `strlist`, `records variant+string`, `sexp unique_ptr`).
* On Goose's own rows the spread moved this round, and in v145's favour, because
  the data-stack-top work recovers exactly what MSVC's lack of TBAA was costing:
  `records variable enum` favours v145 by 17% and `interp` by 12%, `words` and
  `sexp` favour clang by only 4-5% where they favoured it by 9-11%, and `sum`
  and `push` -- which used to favour clang -- are now level or v145's. No row's
  conclusion changes with the backend.

## Bounds-check elimination, measured

The compiler now proves most index and slice checks away (spec 10.5).
`bench/bce_ab.ps1` builds each benchmark with and without `--no-bce` under both
toolchains, at the size baked into each source file rather than the report's
`large` one.

Two things to know before reading the A/B. First, `--no-bce` now switches off
more than the checks: the loop-view hoist (`7a7ac87`) decides whether a loop's
array length is invariant by asking BCE's kill summary, so with the pass off the
view is re-read every iteration too. The A/B measures both together. Second,
`graph_csr` is the one row where turning the pass off costs real time, and that
supersedes the previous round's reading of it -- over four runs at 4, 8 and 12
reps per side, `--no-bce` costs it 5.1%, 8.7%, 10.4% and 17.0% under v145 and
0.4-4.4% under clang, where it was previously recorded as inside +/-3%. The
earlier number was under-sampled, not a change in the compiler: `graph_csr` at
the `large` size is flat to within noise across all eight of this round's
commits.

| benchmark | index checks elided | slice |
|---|---|---|
| sexp | 11/12 | 0/1 |
| graph_csr | 11/21 | 0/0 |
| graph | 5/9 | 0/0 |
| words | 2/10 | 3/3 |
| strlist | 2/4 | 1/1 |
| records | 0/1 | 0/0 |
| sum, push, tree, interp, particles | no index expressions at all | |

What the A/B is worth, per side, best of the reps shown: `graph_csr` +5 to +17%
under v145 and +0 to +4% under clang -- the one row where it clearly pays --
`graph` +0.8% and +1.1%, `sexp` +2.2% and -1.8%, `strlist` +0.6% and +3.7%,
`words` -2.8% and -3.8%, `records_var` -0.6% and +1.6%. So outside `graph_csr`
the pass is still worth roughly nothing, and that is because of *which* checks
it gets, not because bounds checking is free. Neutering `GS_IDX` entirely in the
generated C puts `graph` at 1,842 ms against 1,910 with the pass on and 1,956
with it off, and `graph_csr` at 235 against 265 and 256. Bounds checking costs
`graph` about 8% in total; the pass recovers two or three points of that, and
the remaining five or six sit in checks it cannot prove.

`words` going *negative* on both backends is worth not over-reading. Removing
checks cannot make a program slower, so a consistent negative is the A/B picking
up code layout rather than the pass -- the same confound the caveats describe,
and the reason only `graph_csr`'s gap here is large enough to claim.

**Every check that survives is an index that came out of memory.** `dist[u]`
where `u = q[qh]`, `dist[w]` where `w = cur.to`, `out[fill[s]]` in the CSR
fill. The analysis gets everything derived from loop counters, lengths,
constant masks and reductions, and nothing that was loaded from a data
structure -- which is what `test/bce.goose` documents ("keep-cases index with
values loaded from array contents, which the analysis never tracks").

Four rewrites were tried to see whether any natural form unlocks them. None is
worth adopting, and the reasons are the useful part:

* **Making `graph`'s pool, dist and queue globals**, so the whole-program
  invariant pass could apply: still 5/9. The invariant would have to hold on
  array *contents* ("every `i32` in `q` is a valid index into `dist`"), not on
  a scalar variable.
* **Deriving `words`'s mask from `slots.len`** rather than from the separate
  `nslots`: 3/10. `x & (len - 1)` is not related to `len`; the case that works
  in `test/bce.goose` has a constant mask and a constant length.
* **Using the documented `%` reduction** for the probe index: still 3/10,
  because `idx` is loop-carried. Re-deriving the reduction inside the probe
  loop reaches 9/10, which pins the gap precisely: the range fact does not
  survive the loop back-edge.
* **One `assert(idx >= 0 && idx < slots.len)` at the top of the probe loop**:
  9/10, five checks traded for one assert. But all four variants time within
  noise of each other (110-125 ms), because those checks are perfectly
  predicted branches on a value already in a register and the loop is bound by
  the random slot access.

## What this round's codegen work bought

Eight commits landed between the last benchmark update and this one. Because a
single suite run cannot separate a compiler change from the machine (see the
reproducibility caveat below), each was measured by building the compiler at
every commit in turn and putting the same benchmark sources through all of them
back to back, best of five after two warm-ups, with `-falign-loops=32` under
clang so a shifted hot loop cannot masquerade as a codegen change. Times in ms
at the `large` sizes, base = `171bf6a`:

| | v145 base -> HEAD | clang base -> HEAD |
|---|---|---|
| `push` | 172.7 -> 148.9 (**-13.8%**) | 163.4 -> 148.6 (**-9.1%**) |
| `sum` | 315.1 -> 295.1 (-6.3%) | 330.4 -> 311.8 (-5.6%) |
| `sexp` | 1,583.8 -> 1,511.4 (-4.6%) | 1,464.5 -> 1,439.6 (-1.7%) |
| `strlist` | 294.3 -> 281.3 (-4.4%) | 292.5 -> 288.6 (-1.3%) |
| `graph` | 5,961.7 -> 5,933.0 (-0.5%) | 6,129.3 -> 5,847.7 (-4.6%) |
| `particles` | 913.6 -> 911.0 (0.0%) | 854.9 -> 909.2 (**+6.4%**) |
| `particles_scalar` | 881.6 -> 875.0 (-0.7%) | 852.4 -> 905.0 (**+6.2%**) |

Nearly all of the movement, good and bad, is one commit: caching each data
stack's `top` in a local (`432f8ce`, with `b4ac4c7` narrowing the write-backs to
the stacks a call can actually reach). It is worth 6-8% on the push-heavy rows
under both backends, which is the previous round's recommendation number 2
landing roughly where it was predicted -- except that it was predicted to be
worth "6% under v145 on `push`, nothing under clang", and it is worth about as
much under clang. Loading and storing non-optional relative references without a
null test (`a8c9f69`) is the other visible win, and this one is lopsided: a
further 4.8% on `push` under v145 against 1.3% under clang, and about 1% on
`sexp`, all on the pointer-chase critical path.

The cost is that the same commit makes both float kernels about 6% slower under
clang and nothing under v145 -- the one place where writing the bump pointer into
a local is worse than letting the backend do it, which is exactly where the
backend was already doing it. That is a real regression, it survives the
alignment control, and it is why the suite's clang geometric mean improved less
than its v145 one this round.

Two commits in the middle of the range, `672a5ff` and `b4ac4c7`, emit C that
clang-cl rejects outright (`error: call to undeclared function 'gs_ovf'`);
`bebf97b` fixes it. Anything bisecting compiler performance across that window
has to skip those two under clang.

## Recommended compiler work, in order of measured payoff

Each was measured by transforming the generated C by hand and timing it, so
these are upper bounds on what doing it properly in the compiler would buy. The
percentages are whole-program, so the effect on the construct itself is larger.
They are all single digits: there is no large win left lying around.

The previous list's number 2 -- cache each data stack's `top` in a local -- has
since been done (`432f8ce`, `b4ac4c7`), and the section above records what it
actually bought against what was predicted. Its one unpredicted effect, the 6%
clang loss on the float kernels, is the first thing on this list now.

1. **Stop caching the stack top where the backend already does it.** Worth
   recovering the **6% lost on `particles` and `particles_scalar` under clang**
   at `432f8ce`, without giving back the 6-8% the same transform wins on the
   push-heavy rows under both backends. The distinction the compiler is missing
   is that the local pays for itself only where the loop actually pushes; in a
   kernel that only reads and writes elements, clang's TBAA had already kept the
   top in a register, so the local is added live range and nothing else.
2. **Do accumulator tail-recursion elimination in the Goose optimizer.** Worth
   **7.5% under v145** on `tree` when it was measured last round, by hand
   transform, and not re-measured since; clang already does it, so this is
   purely about levelling the backends. `sum_tree`, `walk_chain` and `eval` are
   all the shape `return f(a) + self(b)`, which becomes a loop with a running
   accumulator. Goose knows its whole call graph and already requires
   `recursive fn`, so it has everything the transform needs.
3. **Extend the bounds-check invariant pass from scalars to array contents.**
   Worth the **5-6% still left in `graph`** and about 10% in `graph_csr`, per
   the measurements above. The pass already records "every write preserves
   `v >= 0 && v <= len`" for scalar variables and globals; the checks it misses
   need the same statement about the *elements* of an array, so that an index
   read back out of `q` is known to be in range for `dist`. A narrower version
   would help too: carry a range fact for a loop-carried index across the back
   edge, which is what the `words` probe loop needs.
4. **Merge repeated dispatch on the same scrutinee.** `sexp`'s `walk_chain`
   switches on the same tag byte twice in a row -- once to visit the node, once
   to fetch its `next` link -- emitting two jump tables where one would do. Not
   separately measured, but it is pure duplication, and the case-function idiom
   produces it whenever two overload sets are applied to the same value.
5. **Consider `/arch:AVX2` (or clang) for release builds.** 13% on `sum` under
   clang. A build-flag decision rather than codegen, but it is the one place
   where the backend choice repays wider vectors today.

Three plausible-sounding ideas were measured and should **not** be done:

* **Dropping `#pragma pack(1)` where the layout is already gap-free** makes no
  difference to either backend (`particles`: 40.8 vs 41.1 ms under v145, 36.8
  vs 36.8 under clang). The packed layouts are not costing anything here.
* **Fusing an *optional* relative-reference load with the null test that follows
  it** makes both backends *worse*: `tree` goes from 38.9 to 42.8 ms under v145
  and 35.2 to 39.4 under clang. The current three-statement form gives the
  optimizers something they evidently prefer. This is separate from dropping the
  null test on *non-optional* relative references, which was pure waste and has
  since been done (`a8c9f69`, worth about 5% on `push`): offset 0 spells null
  only for the optional form, so on a non-optional slot the test was both dead
  and wrong for a target starting at the offset field itself.
* **Rewriting benchmark code so the bounds-check pass can prove more** buys
  nothing, per the section above.

## Where Goose loses

**Pointer-linked adjacency loses to CSR, in all three languages.** On `graph`
the one-pass linked build is 4.5-5.7x slower to traverse than two-pass
count-then-fill: Goose linked 4,964 ms against Goose CSR 870, C++
arena-with-indices 4,578 against C++ CSR 891, Rust arena-with-indices 4,183
against Rust CSR 935. The Goose CSR row exists to show this is a data-structure
effect rather than a language one, and Goose writes CSR level with both. What
Goose buys is that the one-pass version is *possible* and pleasant to write
while handing out real pointers -- Rust has no safe spelling of it at all, only
the index form -- but it does not make a linked list local.

**Float kernels are a draw under v145 and a small loss under clang.**
`particles` per component is 870 ms for Goose under v145 against 916 for C++ and
854 for Rust -- everyone inside everyone else's noise, on identical memory --
but 902 under clang, where C++ reaches 824. This is the only benchmark Goose
never leads, and the clang side got worse this round rather than staying put:
see the regression below. Elementwise notation is free in both languages that
can express it (Goose 908 against 870, Rust 868 against 854), so `impl Add for
F3` costs a Rust programmer nothing but the boilerplate. There is no Goose
advantage here -- as the design predicted for flat scalar work.

**A 6% clang regression on both float kernels, from this round's stack-top
work.** Building the compiler at each commit since the last benchmark update and
timing the same sources through it puts the change at `432f8ce` exactly:
`particles` under clang goes 855 -> 909 ms there and stays there through HEAD,
`particles_scalar` 852 -> 907, while the v145 column does not move at all
(911-914 at every commit). It is not the alignment confound -- the A/B was run
with `-falign-loops=32` on both sides, and repeated at five reps. The same
commit is worth 6% on `sum` under both backends and 8% on `push`, so this is a
transform that pays for itself overall and costs these two loops; the likely
mechanism is that clang's TBAA already kept the stack top in a register here, so
the hand-written local is pure added live range. Tracked as its own piece of
work.

## Caveats on these numbers

* Whole-process wall clock, so input generation is inside every measurement.
  Where generation is a large share of the work (`sexp`, `words`, `graph`) the
  ratios are diluted toward 1.0 and the gap on the interesting phase is larger
  than the table shows.
* Best of three runs after two discarded warm-ups. The warm-ups are not
  optional: on this machine a freshly written executable costs up to 10x its
  steady-state time on the first execution and takes about three runs to
  settle, and it hits clang-linked binaries far harder than MSVC-linked ones.
  Without them the toolchain comparison measures the malware scanner.
* One machine, no core pinning. Differences under ~10% are not differences.
* **The allocator-heavy rows are the least reproducible in the suite, and they
  are the ones the headline ratio rests on.** Running the whole harness twice
  back to back, the Goose rows land within 3% of themselves, and so do the
  arena and CSR rows in every language -- but `sexp cpp unique_ptr` moved 19%,
  `rust enum + Box + String` 15%, `graph cpp arena + indices` 21%, `rust arena +
  indices` 23% and `words cpp unordered_map` 8%. Those are all malloc-bound, and
  malloc's behaviour depends on machine state the harness does not control. Two
  consequences: a change in the "vs idiomatic" geometric mean of less than about
  10% between rounds means nothing at all, and a single suite run is not enough
  to attribute a change to the compiler. Attributing anything to a compiler
  change wants the per-commit A/B described above, where both sides are built
  from the same sources minutes apart.
* Code alignment is a real confound when comparing two *Goose* builds. Two
  compiler versions whose hot loop was byte-identical measured 14% apart on
  `particles` under clang, purely because a change earlier in the function
  moved that loop; building both with `-falign-loops=32` closed the gap
  entirely. Use that flag when A/B-ing compiler changes against each other, and
  not here: these rows compare languages, so a flag given to Goose would have
  to be given to C++ and Rust too, where it would likely cancel out.
* This CPU has a very large L3, so `small` and `medium` can sit entirely in
  cache. The `large` rows are the ones that stress the memory subsystem.
* Rust rows are a single rustc time, not one per backend. rustc is LLVM, so
  the clang column is the one they are most directly comparable with; the
  Goose-vs-Rust ratios are reported against both Goose builds anyway.

## What writing these taught us about the language

Findings from implementing and maintaining the benchmarks, independent of how
fast anything ran.

This round's compiler work was all codegen, so nothing below changed with it;
the language-level frictions are as they were.

**Fixed earlier, all three by the sized-integer change:**

* `varint` fields now accept a computed value. Previously a field took a
  literal but rejected a runtime integer as a narrowing store, and `as varint`
  was rejected outright, leaving no path in at all -- `sexp` had to store
  parsed numbers as `i32`. It stores them as `varint` again.
* Compound assignment to narrower storage works: `count += 1` on an `i32` field
  no longer has to be written `count = (count + 1) as i32`. This was the most
  frequent friction in the whole suite (spec TODO 0a).
* Field initialisers no longer need an `as` from a value the compiler can see
  is in range, for the same reason.

**Still open, re-verified against the current compiler:**

* **Reference laundering defeats relative references.** A reference read back
  out of a container is conservatively rooted at that container (spec 9.5), so
  it cannot then be stored into a relative-reference field belonging to the
  array it actually points into. `graph` still keeps its per-vertex list heads
  in the same pool as the edges to work around it.
* **A `null`-reachable optional is rooted at static data.** An optional
  reference that can still be null where it reaches a relative-reference field
  poisons that store, even though the non-null path is rooted in the pool. A
  local rebound before use is now accepted, but `sexp`'s parser -- where the
  first sibling genuinely is null -- is not, so its links are still plain
  8-byte references where 4-byte relative ones would do. Comparing against a
  sentinel variant instead would need reference address identity, which the
  language does not have (spec TODO 0b).
* **The cycle store rule reshapes tree walks.** Inside a recursive cycle a
  reference derived from a parameter may be passed down but not stored, and
  this now rejects even `var c = start;` at the top of a recursive function.
  `sexp` walks sibling chains by passing the link down to a recursive helper
  rather than looping with `.=`.
* **Grow-only arrays have no `clear`.** A scratch buffer refilled per iteration
  must be grow-shrink (`[>..<]`), which then cannot hand out references or
  slices at all. "Reusable scratch space" and "structure I can point into" stay
  mutually exclusive, chosen at the type level.

**What worked without any friction:** grow-only pools with interior references,
relative references inside a single root, variable-mode ADTs with inline
variable-size payloads, case functions as the dispatch mechanism, returning
references rooted at globals out of recursive functions, slices as struct
fields, `return ... from` as a parser error path, and NRVO on returned locals.

## What writing the Rust benchmarks taught us

Findings from implementing the suite a third time, independent of how fast
anything ran. The headline is that Rust is a much closer competitor than C++ on
speed and a much better-designed language than C++ on most of these axes -- and
that the places it still cannot follow Goose are structural, not effort.

**One benchmark has no Rust spelling at all.** `push` keeps a pointer to every
64th element of an array it is still appending to. In Goose that is
`marks.push(items.push(...))` and the reference stays valid for the array's whole
life. In Rust it does not compile: a `&Item` borrows `items` for as long as it is
held, so the following `items.push` is rejected. No std container is both
pointer-stable and O(1)-append -- `VecDeque` is a ring buffer and reallocates,
`Vec<Box<Item>>` still borrows the outer `Vec`. The remaining options are
indices, `Rc<RefCell<_>>`, a third-party arena crate, or `unsafe`. The row uses
indices because that is what a Rust programmer writes; it matches Goose on
memory and is 11% behind it on time since the data stack top moved into a local.
The interesting cost is in the source, not the numbers.

**Indices everywhere Goose has references.** Same story in `tree`, `interp`,
`graph` and `sexp`: the fast, safe, recommended Rust shape is a `Vec` arena with
`u32` links, exactly as `design.md` predicted. Worth being precise about what
that costs, because it is not safety -- an out-of-range index panics like any
other Rust index:

* the type says `u32`, not what it points at, so nothing connects a link to the
  pool it belongs to and nothing stops one pool's index being used on another;
* `Option<&T>` becomes a magic `0` sentinel, and the sentinel has to occupy a
  real slot (every arena row here wastes element 0);
* an index into a pool that is later reused is stale in a way the compiler
  cannot see, where a reference would have been rejected.

Goose reaches the same 12-byte layout with `TNode&<u32>?`: checked, nullable,
and typed to the thing it points at. So on these five benchmarks Goose is not
just level with Rust, it is level with the shape Rust falls back to when the
borrow checker says no, while keeping the one the borrow checker would have
liked.

**Rust enums are the best fixed-tag ADT of the three.** A byte discriminant, a
jump table from `match`, no vtable pointer, and niche optimisation that hides
the tag inside a payload pointer. That is enough to beat `std::variant` on both
time and memory in `records` and `interp`. On `interp` the time gap against Rust
is about the same as against hand-built C++ (1.43x and 1.28x against the Rust
arena, 1.38x and 1.34x against the C++ tagged union), but the memory gap is
smaller -- 2.2x rather than 2.4x -- because the Rust arena node is the most
compact of the non-Goose rows. What it cannot do is stop being fixed: every
element is sized for the largest variant. Variable-mode enums are the one Goose feature in this
suite with no Rust analogue at any level of effort.

**Rust's idioms are already the fast ones, twice.** `collect()` over a
`TrustedLen` iterator preallocates exactly, so the "did you remember to
`reserve`" gap that `sum` and `push` measure against C++ simply does not exist
against Rust. And borrowing is the default, so `Vec<&str>` and
`HashMap<&str, _>` are the first thing written -- where the idiomatic C++ row
copies every key into a `std::string` and has to be talked out of it. Both are
real language-design wins over C++ and both shrink the Goose margin.

**Where Rust pays anyway:**

* `Box::new(T { .. })` builds the value in a stack temporary and moves it to the
  heap. There is no guaranteed construct-in-place, which is spec 4.3/7.3.
* Teardown is real and it is the same cache-hostile pointer chase that building
  was. `interp`'s `Box` row is the slowest in that benchmark at 463 ms --
  *behind C++ virtual dispatch* -- because an allocation per node plus a
  recursive drop costs more than a vtable does.
* std's `HashMap` hashes with SipHash-1-3, keyed and DoS-resistant by policy.
  That is the right default for a general map and the wrong one for counting
  words in a local buffer: it costs about 39% here. The community answer is a
  hasher crate, which a std-only suite cannot use, hence the hand-rolled row.
* The borrow that makes `sexp`'s arena row fast works only because `text` is
  complete before parsing starts and never grows again. A parser that interned
  or rewrote text while building nodes -- which real ones do -- would be back to
  owned `String`s or to offsets.

**What Rust does that Goose should be measured against again later:** `Vec<Vec>`
is 1.6x faster than the `vector<vector>` it mirrors, so the idiomatic-nested-
container row is much less of a straw man in Rust than in C++.
