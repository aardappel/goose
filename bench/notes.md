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
  0.61 -- v145 182 ms against clang 298. clang also trails on both `interp`
  arena rows.
* **clang is consistently ~10% ahead on string and hash work** (`words`,
  `strlist`, `records variant+string`, `sexp unique_ptr`).
* On Goose's own rows the spread is small: `records variable enum` favours v145
  by 16%, `words` and `sexp` favour clang by 9-11%, everything else is inside
  noise. No row's conclusion changes with the backend.

## Bounds-check elimination, measured

The compiler now proves most index and slice checks away (spec 10.5).
`bench/bce_ab.ps1` builds each benchmark with and without `--no-bce` under both
toolchains; what it finds is that the pass removes a lot of checks and changes
no measurable time.

| benchmark | index checks elided | slice |
|---|---|---|
| sexp | 11/12 | 0/1 |
| graph_csr | 11/21 | -- |
| graph | 5/9 | -- |
| words | 2/10 | 3/3 |
| strlist | 2/4 | 1/1 |
| records | 0/1 | -- |
| sum, push, tree, interp, particles | no index expressions at all | |

A/B over eight runs per side: `graph` +2.4% under v145 and +3.0% under clang,
everything else inside +/-3% in both directions. So the pass is worth roughly
nothing today -- but that is because of *which* checks it gets, not because
bounds checking is free. Neutering `GS_IDX` entirely in the generated C puts
`graph` at 1,842 ms against 1,910 with the pass on and 1,956 with it off, and
`graph_csr` at 235 against 265 and 256. Bounds checking costs `graph` about 8%
in total; the pass currently recovers two or three points of that, and the
remaining five or six sit in checks it cannot prove.

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

## Recommended compiler work, in order of measured payoff

Each was measured by transforming the generated C by hand and timing it, so
these are upper bounds on what doing it properly in the compiler would buy. The
percentages are whole-program, so the effect on the construct itself is larger.
They are all single digits: there is no large win left lying around.

1. **Do accumulator tail-recursion elimination in the Goose optimizer.** Worth
   **7.5% under v145** on `tree`; clang already does it, so this is purely
   about levelling the backends. `sum_tree`, `walk_chain` and `eval` are all
   the shape `return f(a) + self(b)`, which becomes a loop with a running
   accumulator. Goose knows its whole call graph and already requires
   `recursive fn`, so it has everything the transform needs.
2. **Cache each data stack's `top` in a local across a loop or function.**
   Worth **6% under v145** on `push`, nothing under clang. Every push currently
   loads a global, stores through it, and reloads it to bump: three accesses to
   `gs_stks[i].top` per element. clang's type-based alias analysis proves the
   element store cannot touch the stack control block and keeps `top` in a
   register; MSVC does no TBAA by default and reloads every time. Emitting the
   local -- and writing it back before any call that could touch the same stack
   -- makes the two backends behave alike.
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
* **Fusing an optional relative-reference load with the null test that follows
  it** makes both backends *worse*: `tree` goes from 38.9 to 42.8 ms under v145
  and 35.2 to 39.4 under clang. The current three-statement form gives the
  optimizers something they evidently prefer.
* **Rewriting benchmark code so the bounds-check pass can prove more** buys
  nothing, per the section above.

## Where Goose loses

**Pointer-linked adjacency loses to CSR, in both languages.** On `graph` the
one-pass linked build is 5-6x slower to traverse than two-pass
count-then-fill: Goose linked 4,955 ms against Goose CSR 838, with the
equivalent C++ arena-with-indices at 4,710 and C++ CSR at 849. The Goose CSR
row exists to show this is a data-structure effect rather than a language one,
and Goose writes CSR level with C++. What Goose buys is that the one-pass
version is *possible* and pleasant to write while handing out real pointers; it
does not make a linked list local.

**Float kernels are a draw, not a win.** `particles` per component is 2% ahead
of C++ under v145 and 4% behind under clang (866 vs 834 ms). The elementwise regression is fixed, so the remaining gap
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
