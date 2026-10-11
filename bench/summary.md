**Over sixteen benchmarks Goose runs at about 3.5x the speed of idiomatic C++,
about 1.25x the speed of hand-optimised C++, and ahead of the best safe Rust
(1.12x under v145, 1.19x under clang), on 2.0x, 1.3x and 1.3x less memory.**
Against the Rust *arena* implementations, which use indexed storage for
linked data and win each comparison with the corresponding Rust pointer-based
implementation, Goose wins nine of the sixteen under both backends, is level
on three, splits three by backend, and loses one: `calc`.

**Data representation accounts for the clearest gains.** These comparisons
benefit from layouts and reference rules that ordinary Rust types do not
provide. A variable-mode enum costs each
element its own variant instead of the largest, so `records` is 2.1-2.3x faster
on 4.1x less memory than a Rust `enum` whose every element is sized for its
`String` arm; `interp` is the same property in a tree, 1.3-1.5x faster on 2.2x less.
Variable-size parts nest inside their containing records, so `respond` builds a DTO
whose name, item list and per-item sku are one inline value, renders it with no
allocation anywhere, and beats the Rust DTO by 2.2-2.3x, landing between the
C++ and Rust streaming rows that never build an object at all. References stay valid
into a container that is still growing, which `push` has no safe Rust spelling
for and `tree`, `graph`, `scene`, `bintrees` and `calc` can only reach as `u32`
indices. And links can be narrower than a pointer: `sexp`'s nodes link with
4-byte offsets and `calc`'s with 2-byte ones, where Rust's arena reaches four
bytes only as an untyped index that needs a sentinel slot and goes stale
silently when the pool is reused.

**One consistent loss remains, and three comparisons depend on the backend.**
`calc` (0.84-0.87x) has costs specific to the Goose implementation: a global parse
cursor, a `return from` discriminant on every frame of the way out (spec 7.9),
and a varint decode per evaluated number, on inputs of about 40 bytes in a run
that never leaves L1. `scene` is level with the Rust arena under clang and
0.83x under v145, where the C++ arena row is 10% behind its own clang build on
the same float-and-pointer mix, and padding the 117-byte node to an aligned
120 changes nothing. `lru` is 0.72x under v145 and 1.10x under clang because
MSVC will not schedule a base-plus-offset load the way clang does, and `blur`'s
flat kernel 0.98x and 1.10x because MSVC will not vectorise it. Two former
losses are gone: `particles`, a flat float kernel where the design never
predicted an advantage, is 2-4% ahead, inside the noise of the row, and
`bintrees`, which trailed the Rust arena by 24% under clang, is 13-16% ahead
under both backends since the compiler keeps a by-reference pool's top and
count in locals across the recursion.

**`blur` tests a likely weakness: bounds checks in an image kernel.** The obvious
`src[y*W + x]` form has all ten of its checks proven, from the lengths `main`
establishes. Under clang it is 1.10x ahead of flat Rust (256 against 282 ms).
Under v145 it runs 3.3x behind flat Rust, though still ahead of flat C++,
because MSVC cannot rule out that the stores through `dst` alias the loads
through `src` and leaves the loop scalar. Written over row slices both
backends vectorise it, and Goose is level with Rust (285/255 against 280-282
ms). The checks themselves matter under v145 only: `notes.md` measures proving
them as worth 1.8x on the flat kernel and 2.9x on the row slices, and nothing
under clang, which vectorises around them as LLVM does for Rust's checked
implementation.

**Peak memory use is 2.0x smaller than idiomatic C++, 1.3x
smaller than expert C++, 1.3x smaller than Rust.** Where the layouts are
literally the same the rows land within 1%; the gaps are where Goose can
express something a fixed enum or a heap-owning string cannot -- variable-mode
payloads (`records` 4.1x against Rust, `sexp` 2.4x, `interp` 2.2x), inline
strings (`strlist` 2.2x against `Vec<String>`), the 16-byte `lru` node against
a `std::list` node plus a map node (3.2x), and the 117-byte `scene` node
against a node plus a child vector (1.3x). `sexp` is the clearest case:
its links are 4-byte offsets throughout, including the null that ends every
sibling chain, and that alone is 1.3x of the row's memory. Three benchmarks
(`calc`, `respond`, `bintrees`) never accumulate anything, and every row but
the two `bintrees` allocator ones reports 4-32 MB: their working set is one
input, one request or one tree, and the process floor is most of it. The
1.11x against Rust on `calc` and `respond` is the half-megabyte difference
between the two languages' process floors.

**Similar performance sometimes requires different interfaces.** In seven of the
sixteen -- `push`, `tree`, `interp`, `sexp`, `scene`, `calc`, `bintrees` --
the Rust row that comes closest to Goose is linked by integer indices where
the Goose row is linked by typed references, because safe Rust cannot hold a
reference into a container it is still growing, and `lru`'s idiomatic Rust
row is *already* the arena because std has no intrusive list. Where Rust does
keep real references -- `Box` nodes -- it is 2.5x to 14x slower than its own
arena row on the six benchmarks that have both. The linked-data comparisons therefore measure both performance and a
difference in representation: Goose uses typed references where the Rust
arena implementations use indices. The timing results vary by workload and
backend, as described above.

**Seven of the sixteen benchmarks use the standard library without a
measurable slowdown.**
Splitting, hashing, formatting, filling and table sizing come from `stdlib/`
rather than being spelled out per benchmark, and every one of those
substitutions was measured against the hand-written form it stands in for:
the library lands between 0.98x and 1.07x of it, and on `strlist` and `words`
it is the faster of the two.
`words` additionally carries a whole second row built on `dictionary`, which
is 1.27x smaller than the hand-written table used for comparison and 14-19%
slower -- a table-sizing difference as much as a container one. Two adoptions
were measured and rejected -- `format_int` in the two `respond` rows and
`push_n` in the two `graph` rows -- and `notes.md` says why. `respond` no
longer uses `hash` for its checksum: std's hash stopped being FNV-1a, which
is what the other languages' rows compute.

**Ratios that need a caveat.** `graph`'s idiomatic column compares Goose's CSR
against C++'s `vector<vector>`, and `respond`'s and `blur`'s best Goose row is
the streaming or row-slice form, so those three idiomatic columns are partly
data-structure comparisons; the per-benchmark tables carry the like-for-like
rows. `push`'s expert figure is 1.26x under v145 and 2.03x under clang for
backend reasons, so read the v145 one. `words` and `lru` compare against
hand-rolled tables in both other languages because std's default hasher is
SipHash by policy, and in `words` those tables hash a byte at a time with
FNV-1a where Goose's std hash reads 8 bytes a step, which is worth 1.19x of
the Goose row under clang and nothing under v145. And `lru`, like `graph`, is
a cache-bound random-access row whose noise floor is about 15%.

**What the timings include.** Every measurement is whole-process wall clock
including input generation, so benchmarks that spend a large share of their
time generating data (`sexp`, `words`, `graph`, `calc`) have their ratios
pulled toward 1.0; the gap on the phase under test is larger than the table
shows. The process-start floor is 8.1 ms for Goose and 8.2 ms for C++ in this
run, and it is most of the `small` column.
