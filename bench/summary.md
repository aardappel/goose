**Over sixteen benchmarks Goose runs at about 3.2x the speed of idiomatic C++,
about 1.15x the speed of hand-optimised C++, and ahead of idiomatic Rust
(1.04x under v145, 1.10x under clang), on 1.9x, 1.3x and 1.2x less memory.**
This round changed the compiler, not the benchmarks: thirteen optimizations
and language items from `plan.md` landed (`notes.md` has the per-row A/B,
`adoption.md` the per-item verdicts), and only the Goose rows moved. Against
the six larger benchmarks' *arena* Rust rows, which last round Goose lost
three of, it now wins three under clang (`respond` 1.35x, `blur` 1.09x,
`lru` 1.05x), ties two (`bintrees` 0.95x under v145, `scene` 0.99x under
clang) and still loses `calc` (0.82x) and, under v145, `lru` (0.69x); it
beats every idiomatic C++ and idiomatic Rust shape on all six.

**The losses, taken apart, are still three different things, and two of them
shrank.** `lru` was the relative references: an offset computed from two
addresses on every relink. Two things changed it. The range check on the
store is gone (a `u32` link into a stack under 2 GB cannot overflow), and
the links are now *pool-relative* -- spec 3.9's `in pool` form, offsets from
the pool's base rather than from the field, which also lets the map hold
4-byte references into the pool instead of indices. Under clang that puts
the row level with the Rust arena (2,133 against 2,237 ms) and the C++ one;
under v145, which schedules the base add worse, it stays 1.45x behind, and
that is now a backend gap rather than an encoding one. `bintrees` was the
backend, and the backend caught up: inlining the
base case, keeping the pool's stack top in a register through the fat
reference and eliminating the accumulator tail call take Goose from 360 to
244 ms under v145 against the Rust arena's 232 and the C++ arena's 387;
under clang 0.81x remains. `calc` is 0.82x, from 0.78x: the `return from`
discriminant no longer travels through every call, and what is left is the
global cursor and a varint decode on values that mostly need two bytes.
`scene` is unchanged: a backend gap under v145 that the C++ arena shares,
level with Rust under clang on 1.35x less memory.

**The two clearest new wins are the two Goose ideas the first ten did not
reach.** `respond` builds a DTO -- a string, a list of records each with a
string -- as one inline value and renders it with no allocation anywhere:
1.5-1.8x faster than the C++ struct-of-strings and 2.1x faster than the Rust
one, level with the streaming rows that never build the object at all. And
every one of the six beats its idiomatic C++ row by more than the first ten
did on average, because the idiomatic shapes here are a `std::list` plus an
`unordered_map`, a `vector<unique_ptr>` per node, exceptions unwinding through
half-built `unique_ptr` trees, and `new`/`delete` per node, which is what the
design said would compound.

**`blur` was included to lose and now loses only under v145.** The obvious
`src[y*W + x]` form was 1.8x slower than flat C++ and 6.4x slower than flat
Rust under both backends. Under clang the loss was the array header being
reloaded after every store because a byte store might alias it, the
no-aliasing-information cost `design.md` predicted; the loop-view hoist now
reaches arrays behind references, and the row runs 268 ms against Rust's
292. Under v145 the nine bounds checks per pixel still stop the loop
vectorising (2,026 ms against 1,056 for flat C++): the analysis can now
prove `y*W + x < W*W`, but only where it knows the array's length, which
inside `blur(src: u8[>..]&, ...)` it does not; one `assert` on the length
at the top of the kernel is enough. Written over row slices, every check is
proven away, both backends vectorise, and Goose lands on Rust (303/264 ms
against 287-292) and on C++ with `__restrict`.

**Memory is still the least ambiguous column: 1.9x smaller than idiomatic C++,
1.3x smaller than expert C++, 1.2x smaller than Rust.** Where the layouts are
literally the same the rows land within 1%; the gaps are where Goose can
express something a fixed enum or a heap-owning string cannot -- variable-mode
payloads (`records` 4.0x, `interp` 2.0x, `sexp` 1.9x), inline strings
(`strlist` 2.2x), the 16-byte `lru` node against a `std::list` node plus a map
node (3.2x), and the 117-byte `scene` node against a node plus a child vector
(1.3x). Three of the new benchmarks (`calc`, `respond`, `bintrees`) never
accumulate anything and report 8-36 MB for every row: their working set is one
input, one request or one tree, and the process floor is most of it.

**How Rust gets there is still the result worth reporting.** In seven of the
sixteen -- `push`, `tree`, `interp`, `sexp`, `scene`, `calc`, `bintrees` --
the Rust row that comes closest to Goose is linked by integer indices where
the Goose row is linked by typed references, because safe Rust cannot hold a
reference into a container it is still growing, and `lru`'s idiomatic Rust
row is *already* the arena because std has no intrusive list. Where Rust does
keep real references -- `Box` nodes -- it is 2.6x to 14x slower than its own
arena row on the six benchmarks that have both. So on the pointer-heavy
benchmarks Goose is, at worst, level with the shape Rust falls back to when
the borrow checker says no, while keeping the typed one; `lru` is the first
place where keeping the *compact* one costs more than that, and `notes.md`
records what to do about it.

**Ratios that need a caveat.** `graph`'s idiomatic column compares Goose's CSR
against C++'s `vector<vector>`, and `respond`'s and `blur`'s best Goose row is
the streaming or row-slice form, so those three idiomatic columns are partly
data-structure comparisons; the per-benchmark tables carry the like-for-like
rows. `push`'s expert figure is 1.22x under v145 and 2.01x under clang for
backend reasons, so read the v145 one. `words` and `lru` compare against
hand-rolled tables in both other languages because std's default hasher is
SipHash by policy. And `lru`, like `graph`, is a cache-bound random-access row
whose noise floor is about 15%.

**What is not in these numbers.** Every measurement is whole-process wall clock
including input generation, so benchmarks that spend a large share of their
time generating data (`sexp`, `words`, `graph`, `calc`) have their ratios
pulled toward 1.0; the gap on the phase under test is larger than the table
shows. The process-start floor is 13.8 ms this run, as last time, for every
language alike, and it is most of the `small` column.
