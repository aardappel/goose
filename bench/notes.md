## Reading the toolchain table

The two backends are within 10% on most rows and neither dominates. The gaps
that remain:

* **clang vectorises struct-of-arrays float loops; v145 does not.** `particles
  cpp SoA` is the largest gap in the suite at 1.62x (1,912 vs 1,183 ms). The
  AoS rows -- which is the shape Goose emits -- are within 6-18%, so this costs
  Goose nothing today, but it is the shape any future SIMD work would take.
* **clang exploits a wider ISA when allowed.** With `/arch:AVX2` on `sum`,
  clang improves 13% and v145 1%. Neither compiler goes above SSE2 by default,
  so both languages are leaving that on the table equally.
* **v145 is better at `std::vector::push_back`.** `push cpp vector+reserve` is
  0.60 -- v145 185 ms against clang 307. clang also trails on `graph cpp CSR`
  (0.82) and on both `interp` arena rows.
* **clang is consistently ~10% ahead on string and hash work** (`words`,
  `strlist`, `records variant+string`, `sexp unique_ptr`).
* On Goose's own rows the spread is small: `records variable enum` favours v145
  by 14%, `words` and `sexp` favour clang by 7-11%, everything else is inside
  noise. No row's conclusion changes with the backend.

## Recommended compiler work, in order of measured payoff

Each was measured by transforming the generated C by hand and timing it, so
these are upper bounds on what doing it properly in the compiler would buy. The
percentages are whole-program, so the effect on the construct itself is larger.

1. **Elide bounds checks the front end can already prove.** Removing every
   check from `graph` is worth **15% under v145** and 3% under clang. The
   common case is a `for i in 0..n` loop indexing an array whose length is `n`
   and does not change in the loop: the C backend cannot see that, the Goose
   optimizer can. This is the largest lever measured, and the same pass would
   remove the duplicated `GS_IDX(u, pool.len)` that currently appears two and
   three times inside a single statement group.
2. **Cache each data stack's `top` in a local across a loop or function.**
   Worth **6% under v145** on `push`, nothing under clang. Every push currently
   loads a global, stores through it, and reloads it to bump: three accesses to
   `gs_stks[i].top` per element. clang's type-based alias analysis proves the
   element store cannot touch the stack control block and keeps `top` in a
   register; MSVC does no TBAA by default and reloads every time. Emitting the
   local -- and writing it back before any call that could touch the same stack
   -- makes the two backends behave alike.
3. **Do accumulator tail-recursion elimination in the Goose optimizer.** Worth
   **7.5% under v145** on `tree`; clang already does it, so this is purely
   about levelling the backends. `sum_tree`, `walk_chain` and `eval` are all
   the shape `return f(a) + self(b)`, which becomes a loop with a running
   accumulator. Goose knows its whole call graph and already requires
   `recursive fn`, so it has everything the transform needs.
4. **Merge repeated dispatch on the same scrutinee.** `sexp`'s `walk_chain`
   switches on the same tag byte twice in a row -- once to visit the node, once
   to fetch its `next` link -- emitting two jump tables where one would do. Not
   separately measured, but it is pure duplication, and the case-function idiom
   produces it whenever two overload sets are applied to the same value.
5. **Consider `/arch:AVX2` (or clang) for release builds.** 13% on `sum` under
   clang. A build-flag decision rather than codegen, but it is the one place
   where the backend choice repays wider vectors today.

Two plausible-sounding ideas were measured and should **not** be done:

* **Dropping `#pragma pack(1)` where the layout is already gap-free** makes no
  difference to either backend (`particles`: 40.8 vs 41.1 ms under v145, 36.8
  vs 36.8 under clang). The packed layouts are not costing anything here.
* **Fusing an optional relative-reference load with the null test that follows
  it** makes both backends *worse*: `tree` goes from 38.9 to 42.8 ms under v145
  and 35.2 to 39.4 under clang. The current three-statement form gives the
  optimizers something they evidently prefer.

## Where Goose loses

**Pointer-linked adjacency loses to CSR, in both languages.** On `graph` the
one-pass linked build is 5-6x slower to traverse than two-pass
count-then-fill: Goose linked 4,899 ms against Goose CSR 842, with the
equivalent C++ arena-with-indices at 4,654 and C++ CSR at 854. The Goose CSR
row exists to show this is a data-structure effect rather than a language one,
and Goose writes CSR level with C++. What Goose buys is that the one-pass
version is *possible* and pleasant to write while handing out real pointers; it
does not make a linked list local.

**Float kernels are a draw, not a win.** `particles` per component is 4% behind
C++ (883 vs 845 ms). The elementwise regression is fixed, so the remaining gap
is small, but there is no Goose advantage here either -- as the design
predicted for flat scalar work.

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
* One machine, no core pinning. Differences under ~10% are not differences, and
  `graph` at `large` has been seen to move 20% between runs.
* This CPU has a very large L3, so `small` and `medium` can sit entirely in
  cache. The `large` rows are the ones that stress the memory subsystem.
* Rust is not represented yet.

## What writing these taught us about the language

Findings from implementing and maintaining the benchmarks, independent of how
fast anything ran.

**Fixed since the last round, all three by the sized-integer change:**

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
