**Over sixteen benchmarks Goose runs at about 3.1x the speed of idiomatic C++,
about 1.1x the speed of hand-optimised C++, and level with idiomatic Rust
(1.01x under v145, 1.04x under clang), on 1.9x, 1.3x and 1.2x less memory.**
The first ten benchmarks say what they said last round -- every row within the
documented noise of the previous run, Goose 1.2x ahead of Rust on their
geometric mean -- and the six new, larger ones pull the Rust figure down to
level, which is the result of this round: Goose beats the idiomatic C++ shape
on all six by 2.3x to 8.4x and the idiomatic Rust shape (`Box`, `HashMap`,
`String`) on five of them, but against the *arena* Rust rows it wins one
(`respond`, 1.25x), ties two (`blur`, `scene` under clang) and loses three
(`calc` 0.78x, `bintrees` 0.64x, `lru` 0.61x).

**Each loss was taken apart, and they are three different things.** `lru` is
the relative references: the same Goose program with `i32` index links runs
in 2,397 ms and with plain 8-byte references in 2,557, against 3,738 with the
4-byte self-relative links, so the encoding -- an offset computed and
range-checked on every relink -- costs 1.5x on a workload that only relinks,
and that is the whole gap to Rust's 2,170. `bintrees` is the backend: Goose
and the C++ vector arena land together (353-397 ms) and the Rust arena is 1.6x
ahead of both in building and in checking alike; a global pool, 8-byte links
and the by-reference push all measure the same, so the difference is what LLVM
makes of rustc's recursion rather than anything in Goose's data path. `calc`
is 7% behind the C++ arena under v145 and 21-24% behind Rust's, and the
per-input pool is not the cost either (a never-reset global pool times the
same); what remains is the `return from` discriminant on every call, the
global cursor and the varint decode, on inputs that never leave L1. `scene`
under v145 is a backend gap the C++ arena shares (both 24% behind their clang
builds); under clang Goose and Rust are level on 1.35x less memory.

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

**`blur` was included to lose and does, twice over, and both halves are
compiler work rather than language cost.** The obvious `src[y*W + x]` form is
1.8x slower than flat C++ and 6.4x slower than flat Rust. Under v145 the nine
bounds checks per pixel stop the loop vectorising (the analysis reasons in
differences and cannot see `y*W + x < W*W`); under clang the checks are free
-- it vectorises around them, exactly as it does for Rust's checked row -- and
the same 4x is lost to the array header being reloaded after every store
because a byte store might alias it, the no-aliasing-information cost
`design.md` predicted. Written over row slices with one `assert` per row,
every check is proven away, both backends vectorise, and Goose lands on Rust
(304/263 ms against 287-292) and on C++ with `__restrict`.

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
rows. `push`'s expert figure is 1.24x under v145 and 2.03x under clang for
backend reasons, so read the v145 one. `words` and `lru` compare against
hand-rolled tables in both other languages because std's default hasher is
SipHash by policy. And `lru`, like `graph`, is a cache-bound random-access row
whose noise floor is about 15%.

**What is not in these numbers.** Every measurement is whole-process wall clock
including input generation, so benchmarks that spend a large share of their
time generating data (`sexp`, `words`, `graph`, `calc`) have their ratios
pulled toward 1.0; the gap on the phase under test is larger than the table
shows. The machine was slower to start a process this run (13 ms against 4.6
last time, for every language alike), which is most of the `small` column.
