**Over sixteen benchmarks Goose runs at about 3.3x the speed of idiomatic C++,
about 1.16x the speed of hand-optimised C++, and ahead of the best safe Rust
(1.05x under v145, 1.11x under clang), on 1.9x, 1.3x and 1.2x less memory.**
Against the *arena* Rust rows -- the shape the Rust community recommends for
anything pointer-heavy, and the one that wins every Rust-side comparison here
-- Goose wins seven of the sixteen under both backends, is level on two, splits
four by backend, and loses three: `particles`, `scene` and `calc`.

**The wins are structural, not tuned.** Every row where Goose is clearly ahead
is ahead for something Rust has no version of. A variable-mode enum costs each
element its own variant instead of the largest, so `records` is 2.0-2.3x faster
on 4.0x less memory than a Rust `enum` whose every element is sized for its
`String` arm; `interp` is the same property in a tree, 1.3x faster on 2.0x less.
Variable-size parts nest inside a fixed-size parent, so `respond` builds a DTO
whose name, item list and per-item sku are one inline value, renders it with no
allocation anywhere, and beats the Rust DTO by 2.3x while landing level with
the streaming rows that never build an object at all. References stay valid
into a container that is still growing, which `push` has no safe Rust spelling
for and `tree`, `graph`, `scene`, `bintrees` and `calc` can only reach as `u32`
indices. And links can be narrower than a pointer: `sexp`'s nodes link with
4-byte offsets and `calc`'s with 2-byte ones, where Rust's arena reaches four
bytes only as an untyped index that needs a sentinel slot and goes stale
silently when the pool is reused.

**The three losses are three different things, and only one is Goose's.**
`scene` (0.77x under v145, 0.95x under clang) is the backend: the C++ arena
row is 10% behind its own clang build on the same float-and-pointer mix, and
padding the 117-byte node to an aligned 120 changes nothing. `particles`
(0.95-0.98x) is a flat float kernel where the design never predicted an
advantage and there is none; every row in it is within 5% of every other.
`calc` (0.82-0.86x) is the only one that is really Goose's: a global parse
cursor, a `return from` discriminant on every frame of the way out (spec 7.9),
and a varint decode per evaluated number, on inputs of about 40 bytes in a run
that never leaves L1. The four backend-split rows are the same story from the
other side: `lru` is 0.68x under v145 and 1.16x under clang because MSVC will
not schedule a base-plus-offset load the way clang does, `graph` 0.99x and
1.18x, `bintrees` 0.98x and 0.81x, `blur` 0.95x and 1.09x.

**`blur` is included to lose, and under clang it stops losing.** The obvious
`src[y*W + x]` form is 1.9x slower than flat C++ and 7.0x slower than flat Rust
under v145, where nine bounds checks per pixel stop the loop vectorising at
all. Under clang the checks are free -- it vectorises around them exactly as
LLVM does for Rust's own checked row -- and the same Goose source runs at 271
ms against Rust's 291. What separates the backends is measured directly in
`notes.md`: proving the ten checks takes the v145 row from 141 to 77 ms at
W=2048, and writing the kernel over row slices takes it to 33, level with Rust
and with C++ under `__restrict`. Since these tables were measured the compiler
carries the image's length from `main` into the kernel by itself, so the flat
row now proves its checks without the `assert` and runs at the 77.

**Memory is the least ambiguous column: 1.9x smaller than idiomatic C++, 1.3x
smaller than expert C++, 1.2x smaller than Rust.** Where the layouts are
literally the same the rows land within 1%; the gaps are where Goose can
express something a fixed enum or a heap-owning string cannot -- variable-mode
payloads (`records` 4.0x against Rust, `sexp` 2.4x, `interp` 2.0x), inline
strings (`strlist` 2.2x against `Vec<String>`), the 16-byte `lru` node against
a `std::list` node plus a map node (3.2x), and the 117-byte `scene` node
against a node plus a child vector (1.3x). `sexp` is the clearest case:
its links are 4-byte offsets throughout, including the null that ends every
sibling chain, and that alone is 1.3x of the row's memory. Three benchmarks
(`calc`, `respond`, `bintrees`) never accumulate anything and report 8-36 MB
for every row: their working set is one input, one request or one tree, and
the process floor is most of it.

**How Rust gets there is still the result worth reporting.** In seven of the
sixteen -- `push`, `tree`, `interp`, `sexp`, `scene`, `calc`, `bintrees` --
the Rust row that comes closest to Goose is linked by integer indices where
the Goose row is linked by typed references, because safe Rust cannot hold a
reference into a container it is still growing, and `lru`'s idiomatic Rust
row is *already* the arena because std has no intrusive list. Where Rust does
keep real references -- `Box` nodes -- it is 2.6x to 14x slower than its own
arena row on the six benchmarks that have both. So on the pointer-heavy
benchmarks Goose is, at worst, level with the shape Rust falls back to when
the borrow checker says no, while keeping the typed one.

**The standard library is in eight of the sixteen and costs nothing.**
Splitting, hashing, formatting, filling and table sizing come from `stdlib/`
rather than being spelled out per benchmark, and every one of those
substitutions was measured against the hand-written form it stands in for:
the library lands between 0.98x and 1.07x of it, and on `strlist` and `words`
it is the faster of the two.
`words` additionally carries a whole second row built on `dictionary`, which
is the fastest Goose row there under v145 and 1.2x smaller than the
hand-rolled table it sits beside -- a table-sizing difference as much as a
container one. Two adoptions were measured and rejected -- `format_int` in
the two `respond` rows and `push_n` in the two `graph` rows -- and `notes.md`
says why.

**Ratios that need a caveat.** `graph`'s idiomatic column compares Goose's CSR
against C++'s `vector<vector>`, and `respond`'s and `blur`'s best Goose row is
the streaming or row-slice form, so those three idiomatic columns are partly
data-structure comparisons; the per-benchmark tables carry the like-for-like
rows. `push`'s expert figure is 1.24x under v145 and 2.00x under clang for
backend reasons, so read the v145 one. `words` and `lru` compare against
hand-rolled tables in both other languages because std's default hasher is
SipHash by policy. And `lru`, like `graph`, is a cache-bound random-access row
whose noise floor is about 15%.

**What is not in these numbers.** Every measurement is whole-process wall clock
including input generation, so benchmarks that spend a large share of their
time generating data (`sexp`, `words`, `graph`, `calc`) have their ratios
pulled toward 1.0; the gap on the phase under test is larger than the table
shows. The process-start floor is 15.2 ms for Goose and 15.9 ms for C++ in this
run, and it is most of the `small` column.
