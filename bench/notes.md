## What the numbers say

All figures are the `large` column unless stated. "Idiomatic C++" means the row
a C++ programmer writes first (`unique_ptr`, `vector<string>`, `unordered_map`,
`std::variant`); "expert C++" means the hand-built one (exactly-reserved arena
with indices, `string_view`, flat offsets, open addressing, CSR).

**Against idiomatic C++, the allocation-heavy benchmarks are 3-7x.** `interp`
7.1x (47 ms vs 335), `tree` 4.0x (758 vs 3058), `records` 4.1x (408 vs 1659),
`sexp` 3.7x (1518 vs 5635), `strlist` 2.8x (290 vs 809), `words` 1.6x (2303 vs
3629). This is malloc-per-node, the teardown walk, and the extra bytes per node
all pulling the same way -- the result the design predicted, and it widens with
size in every one of them.

**Against expert C++ it is a draw, sometimes better.** Goose is ahead on
`interp` (47 vs 68, a 1.5x lead over a tagged union in an arena), `records`
(408 vs 566), `strlist` (290 vs 328), `push` (299 vs 319), `sum` (298 vs 324)
and `sexp` (1518 vs 1594); level on `words` (2303 vs 2334); and modestly
behind on `tree` (758 vs 687). So the idiomatic Goose program lands where C++
only gets by hand-building an arena -- without the programmer choosing a memory
strategy at all.

**`push` is the clearest demonstration of the central claim.** Goose beats an
exactly-reserved `vector` while handing out real pointers into the array that
stay valid across all later growth. The only C++ container that can also do
that is `deque`, at 5.4x the time (1611 ms) and 2.5x the memory. Both `vector`
rows only work because the benchmark stores indices instead of pointers.

**Memory: 2-4x better than idiomatic C++, level with expert C++.** `records`
145 MB against 553-1100, `sexp` 492 against 965-1891, `interp` 24 against
59-99, `strlist` 218 against 687. Where the expert row has the same layout
Goose does -- `sum`, `push`, `tree`, `graph`, `particles` -- the two are within
1%, which is the right answer and a good check on the measurement. The one row
Goose loses is `strlist` against flat offsets (218 vs 192 MB): the Goose word
list copies the bytes and is therefore self-contained, while the offsets row is
8 bytes per word pointing into a text buffer it cannot outlive.

**The control behaves.** `sum` tracks a reserved `vector` at every size (298 vs
324 ms, identical memory), so the harness is measuring the algorithm.

## Where Goose loses, and why

**Elementwise struct math is a codegen problem, not a language one, and it is
the biggest single finding here.** The `particles` rows settle it. Written per
component, Goose is 909 ms against C++'s 852 -- a 7% draw. Written the
idiomatic way, `p.vel = p.vel + G`, Goose is 4568 ms, while the *same*
expression shape in C++ costs nothing over the per-component form (906 ms). So
it is not the extra zero-component adds, and it is not the packed layout:
rebuilding the generated C with `#pragma pack(8)` changes nothing, and hoisting
the array header and the global constant out of the loop changes nothing. What
is left is how the operator is emitted -- whole struct temporaries copied out
of and assigned back through the element pointer, which MSVC does not
scalarise. A further ~10% is separately explained by the generated C using
double literals (`0.0`, `0.5`) in expressions that are `f32`, so those
subexpressions round-trip through double. This is spec TODO 10 with a number
attached: the language's nicest vector-math notation is currently its slowest
path by 5x.

**Pointer-linked adjacency loses to CSR, in both languages.** On `graph` the
one-pass linked build is 4-6x slower to traverse than two-pass
count-then-fill. The Goose CSR row is in the suite to show this is a data
structure effect and not a language one: Goose CSR 938 ms against C++ CSR
1197, while Goose's linked row (5897) sits alongside the equivalent C++
arena-with-indices (4479) and both are far behind. What Goose buys here is
that the one-pass version is *possible* and pleasant to write while handing out
real pointers; it does not make a linked list local. If traversal dominates,
CSR is still the answer, and Goose writes it at least as well as C++ does.

**A 5-30% tax where the structures are identical.** Goose's linked `graph` row
is 32% behind its C++ twin, `tree` 10% behind the reserved arena, `particles`
per component 7% behind. Bounds checking and 64-bit `int` arithmetic are the
obvious suspects; nothing here isolates them, and a build with checks disabled
would.

## Caveats on these numbers specifically

* **`graph` at `large` is noisy**: between two full runs the Goose CSR row moved
  from 1283 ms to 938 and the C++ CSR row from 1091 to 1197, so the ordering of
  those two flipped. Treat that pair as a tie, not as a Goose win. The other
  benchmarks reproduced within a few percent across runs.
* MSVC only. clang is the more aggressive vectoriser and should move
  `particles` and `sum` in particular; the harness builds both languages with
  it as soon as it is on PATH. Rust is not represented at all yet.
* Whole-process wall clock, so input generation is inside every measurement.
  Where generation is a large share of the work (`sexp`, `words`, `graph`) the
  ratios are diluted toward 1.0 and the gap on the interesting phase is larger
  than the table shows.
* One machine, best of three runs, no core pinning. Differences under ~10% are
  not differences.
* This CPU has a very large L3, so `small` and `medium` can sit entirely in
  cache. The `large` rows are the ones that stress the memory subsystem.

## What writing these taught us about the language

These are findings from implementing the benchmarks, independent of how fast
anything ran. Several are places where the implementation is stricter than
the spec, or where a rule composes badly with another rule.

**`varint` fields cannot hold a computed value.** A `varint` struct or payload
field accepts a literal, but a runtime `int` is rejected as a narrowing store,
and `as varint` is rejected outright ("varints are written at construction
only"). That leaves no path from a computed value into a varint field, which
contradicts spec 3.6 ("a struct varint field initialized from `arr.len` goes
through `int` -- decode, re-encode"). Hit in `sexp.goose`, where parsed
numbers wanted to be varints and had to become `i32`. Varint *array lengths*
(`u8[varint]`) work fine and are used throughout, so only the value path is
affected.

**Reference laundering defeats relative references.** A reference read back
out of a container is conservatively rooted at that container (spec 9.5), so
it cannot then be stored into a relative-reference field belonging to the
array it actually points into. In `graph.goose` the natural shape -- a `head`
array of per-vertex list heads, and edges linking to each other -- is
rejected for exactly this reason. The workaround is to put the list heads in
the same pool as the edges, which is arguably a better structure anyway, but
it is not a change the compiler can suggest.

**A `null` start makes a link non-relative.** An optional reference variable
initialized to `null` is rooted at static data, and the variable commits to
its first binding's root, so it can never afterwards be stored into a
relative-reference field -- even once it has been rebound to a pool-rooted
reference. In `sexp.goose` this forced the sibling and child links to be plain
8-byte references instead of 4-byte relative ones. A sentinel-variant scheme
would avoid the null, but comparing against a sentinel needs reference address
identity, which the language does not have (spec TODO 0b).

**The cycle store rule reshapes tree walks.** Inside a recursive cycle a
reference derived from a parameter may be passed down but not stored (7.8),
so the ordinary "walk the sibling chain in a loop" form -- `c .= next(c);` --
is rejected. `sexp.goose` walks the chain by passing the link down to a
recursive helper instead. It works and reads fine, but it is a real idiom cost
and it turns an iteration into recursion.

**Storage-type ergonomics are the most frequent friction by far.** Every `i32`
field initializer needs an `as i32` even from a value the compiler can see is
in range, and `count += 1` on an `i32` field has to be written
`count = (count + 1) as i32`. This is spec TODO 0a, and across ten benchmarks
it was the single most common thing to trip over.

**Grow-only arrays have no `clear`.** A scratch buffer that is refilled per
iteration has to be grow-shrink (`[>..<]`), which then cannot hand out
references or slices at all. That is the right rule, but it means "reusable
scratch space" and "structure I can point into" are mutually exclusive
choices, made at the type level. The BFS queue in `graph.goose` is the
grow-shrink one; everything else in that program is grow-only.

**What worked without any friction:** grow-only pools with interior
references, relative references inside a single root, variable-mode ADTs with
inline variable-size payloads, case functions as the dispatch mechanism,
returning references rooted at globals out of recursive functions, slices as
struct fields (the map keys in `words.goose`), `return ... from` as a parser
error path, and NRVO on returned locals. Nine of the ten benchmarks are
written the way the spec's worked examples suggest.
