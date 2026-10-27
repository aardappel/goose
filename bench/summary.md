**The October 16, 2026 run puts Goose at 3.51x / 3.54x the speed of
idiomatic C++, 1.24x / 1.26x the speed of expert C++, and 1.13x / 1.19x the
speed of the best safe Rust rows**, under MSVC / clang respectively. These
are geometric means over the sixteen benchmarks at their large sizes.
Peak-memory ratios are 1.95x, 1.32x and 1.25x in Goose's favor. The full
suite was rebuilt and rerun at all three sizes, including the separate JIT
measurements.

**The source improvement in this refresh is check-free graph traversal.**
Both Goose graph rows store vertex IDs as `u32 in dist`; CSR also stores
prefix offsets as `u32 upto out`. Validation happens during construction,
and `checkfree fn bfs` verifies that no bounds check remains in the
traversal. The entries are still four bytes, and construction and validation
are still timed.
The fresh large CSR times are 699/617 ms, against C++ 784/761 and Rust 811:
1.12x/1.23x ahead of matching C++, and 1.16x/1.32x ahead of Rust. Linked
adjacency remains much slower than CSR in every language. Alternating
same-compiler trials against the original Goose sources measured speedups of
1.21x/1.19x for linked adjacency and 1.08x/1.17x for CSR at the large size
(`notes.md`). These measure the whole source rewrite; the full-suite table
alone does not isolate a source change from other changes since the previous
report.

**Compact representation remains the clearest advantage.** `records` is
2.02-2.26x faster than Rust on 3.98x less memory: a variable-mode enum stores
each record at the size of its own payload, including inline text. `interp`
is 1.29-1.41x faster on 2.04x less memory for the same distinction in a
tree. `sexp` uses 2.43x less memory than the Rust arena and 5.04x less than
the C++ owning nodes. For `strlist`, distinguish Goose's owned inline
strings from the borrowing `string_view`, flat-offset and `Vec<&str>` rows;
those have different lifetime requirements.

**Losses and near-ties remain.** `calc` takes 481/491 ms against the Rust
arena's 436 ms. `lru` takes 2,760/2,068 ms against Rust's 2,249, so it loses
under MSVC and wins under clang. `scene` is 460/371 ms against Rust 379,
again a loss under MSVC and approximately level under clang. This refresh
measures these differences; it does not re-establish the older diagnostic
claims about which individual compiler mechanisms caused them.

**A check-free kernel can still be limited by backend optimization.** The
flat blur is now explicitly `checkfree` and takes 952/266 ms. The row-slice
form, also `checkfree` including slice construction, takes 298/269 ms.
Rust's two forms take 299-300 ms. Removing bounds checks therefore does not
by itself close the MSVC flat-kernel gap. The scene composition and recursive
update also carry `checkfree` contracts. The fastest particle rows are nearly
level across Goose, Rust and clang C++; small differences are not evidence
of a language advantage on this flat numeric workload.

**Allocation and interfaces matter to the comparison.** `bintrees` takes
206/204 ms, against C++'s arena at 393/407 and Rust's arena at 235. The
per-node allocation rows exceed three seconds. Goose uses typed references
into growing storage where the safe Rust arena rows use integer indices.
In `respond`, the inline Goose DTO takes 507/491 ms against Rust's DTO at
1,338, while Goose streaming takes 454/400 against Rust streaming at 540.
Compare matching interfaces when deciding which result applies to an
application.

**The aggregate combines results from different implementations.** For each
backend and comparison tier, it uses the fastest Goose implementation. Memory
figures are selected separately, using the implementation with the lowest memory
use. For example, `graph` speed comes from CSR and its memory figure from linked
adjacency. `words` speed comes from the hand-written table and its memory figure
from `dictionary`. The `respond` headline uses streaming; `blur` uses row slices
under MSVC and flat indexing under clang. No single implementation has all of
these results. Also, `words` compares different hash functions and table
policies: the Goose dictionary uses less memory than its hand-written table but
runs more slowly.

**These are whole-process measurements on one machine.** They include input
generation, construction and teardown, use the best of three runs after two
warm-ups, and do not pin cores. Treat small differences cautiously,
especially in cache-sensitive graph and LRU workloads. The empty-process
floor is now 13.1 ms for Goose and 13.8 ms for C++, versus about 8 ms in the
October 10 report, and MSVC has changed from 19.51.36257 to 19.51.36260.
Cross-date timing changes cannot be attributed solely to Goose compiler
improvements. The older diagnostic studies are retained, explicitly marked
historical, in `notes.md`; current comparisons come from the fresh tables.
