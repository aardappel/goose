# Next compiler work, from the sixteen benchmarks

What to change in the compiler, and in a few places the language, to remove
the deficits the benchmark suite measures and to compound the advantages it
confirms. Every item below carries either a measurement (a hand-transformed
version of the generated C, or the same benchmark written two ways in Goose,
timed at the `large` size on the machine in `results.md`) or an explicit
"not measured". Hand transforms are upper bounds on what the compiler doing
the same thing properly would buy; noise is 5-10% on most rows and ~15% on
the two cache-bound random-access rows (`lru`, `graph`).

## 1. Where the time goes, per deficit

The `large` ratios against the best safe Rust row, and what each one is:

| benchmark | Goose vs Rust | what it is | measured |
|---|---:|---|---|
| `bintrees` | 0.64x | pushing through a fat reference; no base-case inlining; no TRE under v145 | fixed by hand: 356 -> 229 ms (v145), 381 -> 252 (clang), Rust 227 |
| `lru` | 0.61x | self-relative link encoding on a relink-only workload | indices 2,397, plain refs 2,557, relative 3,738 (v145) |
| `calc` | 0.78x | varint numbers 11%; `return from` discriminants (est. 5-10%); parse-loop codegen | `i32` numbers: 548 -> 487 ms |
| `scene` | 0.79x v145, 0.98x clang | backend: the C++ arena is 9% behind its clang build too | padding to 120 bytes: no change |
| `blur` flat | 0.16x | v145: 9 checks/pixel block vectorisation; clang: header reloads through the fat reference | row slices: 133 -> 33 ms (v145), 131 -> 31 (clang) |
| `particles` | 0.95-0.98x | stack-top caching where clang already kept it in a register | 6% clang, from last round's A/B |
| `respond` DTO vs streaming | 30% apart | inlined named results are copied into the field, not built there | the two Goose rows: 559 vs 427 ms |
| `push` by reference | 8% | pushing through a fat reference | `push_ref` probe: 167 vs 155 ms |
| `graph` linked, `graph_csr` | -- | loaded-index bounds checks | 5-6% and ~10% (last round) |

Everything that is *not* on this list -- the flagship parse, the variant
records, the strings, the tree walks -- is already ahead, and the items below
that touch it (`return from`, dispatch merging, named results) compound
rather than repair.

## 2. Compiler work, in order of measured payoff

### 2.1 Cache data-stack tops through fat references

`make(pool: TNode[>..]&, ...)` emits, per push, `pool.stk->top` read and
written four times and `pool.hdr->len++` in memory, because a function
holding a fat reference is excluded from top caching entirely
(`CanCacheTops`): the reference carries a stack that the function might also
name directly, so the two spellings could alias. With the top kept in a local
and flushed around the recursive calls (the same discipline the global-stack
caching already uses), `bintrees` goes from 356 to 313 ms under v145 and 381
to 331 under clang (-12%, -13%). `push_ref` says the same loop moved behind a
reference costs 8%.

The aliasing question is answered by the checker, not the backend. Every
reference parameter has a root class per specialization; two fat references
in different classes are provably different stacks, and a fat reference can
alias a directly named stack only if its class root *is* that global -- which
is visible at codegen time. So: cache `ref.stk->top` per class as a local,
treat same-class references as one stack, and only fall back to the memory
form for the (rare) specialization where a class root is a global the
function also names. `calc` is unaffected (its pushes are a small share;
the global-pool variant timed the same), so this is worth 8-13% on
push-heavy callees and nothing elsewhere.

### 2.2 Inline the base case of a self-recursive function one level

Rust's arena `bintrees` beats Goose and the C++ vector arena by 1.6x in the
build and the check alike, and rustc's assembly shows why: LLVM inlines the
recursion one level, so every leaf call is gone. Hand-transforming `make` so
that `depth == 1` builds both leaves inline (plus 2.1 and 2.3) takes
`bintrees` from 356 to 229 ms under v145 and 381 to 252 under clang -- parity
with Rust's 227. The unroll alone is worth about 27% on top of 2.1.

The optimizer never inlines cycle members ("cycles never inline"). The
targeted form is cheap: for a `recursive fn` whose body begins with a base
case `if <param test> { return <leaf construction> }`, rewrite each self-call
site `f(..., e)` into `if <test on e> { <leaf construction with e> } else {
f(..., e) }`. That halves the calls for every complete tree walk in the
suite (`tree`, `interp`, `sexp`, `scene`, `calc`) and needs no general
recursive inlining; a one-level general inline behind a size cap is the
follow-up if the pattern match proves too narrow.

### 2.3 Accumulator tail-recursion elimination

`check`'s `1 + check(l) + check(r)` rewritten as a loop with an accumulator
takes 7% off `bintrees` under v145 and nothing under clang, which already
does it; the same transform was worth 7.5% on `tree` under v145 last round.
Pattern: a return of the form `e op self(x)` (or `e + self(x) + self(y)`,
looping on the last call) with `op` associative. Goose already knows the
whole call graph and requires `recursive fn`, so the transform has everything
it needs; it is purely about levelling v145 with clang.

### 2.4 Hoist array views reached through fat references out of loops

`blur`'s kernel reads `src.hdr->base` and `src.hdr->len` on every access and
clang cannot hoist them, because a byte store through `dst` may alias the
header: 131 ms at W=2048 against 31 for the same loop over a whole-array
slice taken once. The existing loop-view hoist (`7a7ac87`) only fires for
locals and globals. Extending it to reference parameters is safe for the same
reason it is safe for globals: only a growth or shrink operation, or a call
that can reach the array, changes `base`/`len`, and the BCE kill summary
already says whether the loop body contains one. An element store through
any reference never does. Worth 4x on `blur` under clang, and it applies to
every `fn kernel(a: T[>..]&, ...)` in the language.

### 2.5 Bounds-check analysis: three extensions

a. **Slice lengths as differences.** `let a = src[lo..lo + W]` should record
   `len(a) = W` -- `hi - lo` with both terms tracked is already inside the
   difference domain. Today `blur_rows` needs an `assert(a.len == W)` per row
   to get its 10/10; this removes the assert and makes the row-slice idiom the
   obvious one to write.
b. **Products of counters.** `src[y * W + x]` with `y < H - 1`, `x < W - 1`
   and `len == W * H` (the counted push loop `for i in W * W` already states
   the length as a product) keeps all nine checks, and under v145 that alone
   is 4x (133 vs 33 ms). A special rule for the 2D idiom -- an index `i * c
   + j` with `0 <= j < c`, `0 <= i < m` and `len >= m * c` recorded as a
   product term -- is contained and covers image kernels, matrices and
   row-major tables generally. Under clang the checks are free (it vectorises
   around them, as it does for Rust), so this is the v145 half of the `blur`
   story and 2.4 is the clang half.
c. **Array-contents invariants** (`graph` 5-6%, `graph_csr` ~10%, from last
   round): "every element of `q` is a valid index into `dist`". The only
   route to the loaded-index checks, which are the majority of what survives
   in `graph`, `words`, `lru` (0/19) and `scene`'s child loop. Not urgent:
   `lru` with 37 such checks in its index variant is still 1.5x faster than
   the relative-reference row, so these checks cost little where they are.

### 2.6 Build inlined named results at the destination

`respond`'s DTO row is 30% behind its own streaming row (559 vs 427 ms), and
the generated C shows where: `name_of`, `sku_of` and `make_items` are inlined
(each is used once), and the inliner turns "construct the named local, return
it" into "construct on a fresh stack, `memcpy` into the field". The spec's
guarantee (7.3, named results built at the destination) holds for the
non-inlined value form and is lost the moment the callee is inlined -- which
is the common case for the small builders a DTO is made of.

The fix is to bind the inlined callee's NRVO local to the receiving slot: its
elements go straight onto the destination stack, and the receiver's length
prefix is written afterwards. For fixed-width prefixes (`u32`, `u16`), reserve
the prefix before the elements and patch it: zero copies. For `varint`
prefixes the size is not known until the count is: reserve one byte and move
the elements up only when the count reaches 128 -- the rare case. The same
reserve-then-patch replaces the `memmove` in `EmitReprefix` (a `T[]` result
landing in a `T[varint]` slot) and in `EmitNrvoPrefix` (a resizable local
returned as a variable array) for the non-inlined forms. Upper bound: the 30%
between the two `respond` rows, of which some is rendering from the object
rather than building it; not separately measured.

### 2.7 Stop caching the stack top where the loop does not push

Still the 6% lost on both float kernels under clang at `432f8ce`: the local
pays for itself only where the loop actually pushes. Emit the cached-top form
per loop, when the loop body contains a growth op on that stack, rather than
per function. Unchanged from last round's list; this run measures `particles`
2% behind Rust under v145 and 6% under clang.

### 2.8 Seed a cycle function's return root before its back edges

A recursive call's result takes the root the callee has recorded so far; in
a mutually recursive parser the entry function has none before the back
edge, so `calc` carries a plain-reference Paren node and the result is
rooted at static data -- which is also unsound, since it could be stored
into a global (spec TODO 0g). Compute cycle return roots as a fixpoint:
collect each cycle function's returns (`pool.push(...)` gives the pool's
class; `return x` where `x` came from a cycle call gives that callee's root),
iterate until stable, then check bodies. Small time effect; it removes an
artefact node from the flagship-shaped benchmark and closes a soundness hole.

### 2.9 Relative references: the relinking cost

`lru` at the `large` size, v145 / clang, best of three:

| links | node | v145 | clang |
|---|---:|---:|---:|
| `Node&<u32>?` self-relative (the row) | 16 | 3,787 | 3,143 |
| same, null tests on loads removed by hand | 16 | 3,498 | 3,083 |
| same, range checks on stores removed by hand | 16 | 3,353 | 2,775 |
| both removed | 16 | 3,242 | 2,724 |
| `Node&<u64>?` self-relative (no range check emitted) | 20 | 3,573 | 2,881 |
| `Node?` plain references | 24 | 2,557 | 2,352 |
| `i32` indices, `pool[i]` bounds-checked | 16 | 2,397 | 2,266 |

The checks and null tests are 14% of the row; removing all of them by hand
leaves the relative form 35% behind indices. The gap is 13-21% at `small`
(in cache) and 41-55% at `large`, so what remains is a memory-level effect of
the encoding on a pointer chase -- every hop is a 32-bit load, a
sign-extension, an add to the *field's* address and a null test before the
next load can issue -- and the first thing to put a hardware counter on. Two
compiler items and one language question follow:

* **Skip the range check where the width covers the stack.** A `u32` offset
  is exact for any pool under 2 GB; the runtime reservation size is a
  compile-time constant (`GS_STACK_RESERVE`), so the check can be emitted
  only when it exceeds the width. Worth up to 11-12% here, nothing on the
  build-once rows (`bintrees` with `u64` links times the same as `u32`).
* **Non-optional links for sentinel lists.** The null test on every load is
  there because the list links are optional, and they are optional only
  because the two sentinels cannot be constructed pointing at anything: a
  non-optional `Node&<u32>` has no value before the first node exists. A
  construction form that names the value under construction (`Node { prev:
  self, next: self }`, or a documented "offset 0 on a non-optional relative
  reference means the value itself") would let the whole list be
  non-optional and drop the test; `a8c9f69` already loads non-optional
  relative references without one. Worth 2-8% here.
* **Pool-relative offsets** (see 3.1) would make the load `base + off` with
  the base in a register, which is exactly the index shape that runs 1.5x
  faster; that is a language change with a root-tracking cost, and the
  honest current guidance is the one in `notes.md`: relative references are
  for structures built once and walked, and a `reusable` structure that
  relinks is better off with plain references or indices.

### 2.10 Varint fields on hot values

`calc` with `Num { v: i32 }` instead of `varint` runs in 487 ms against 548
(-11%). It is not the decoder: a single-byte fast path added by hand to
`gs_uleb_read` made the row *slower* (599 ms; the extra branch costs more
than the loop it skips). It is the cost of a decode against a load, paid once
per evaluated number. Two consequences: the language guidance should say
that `varint` is for values whose range is genuinely open and a sized integer
(`u8` here -- the values are 1..99) for values whose range is known; and a
codegen refinement worth trying is decoding a varint *field* read as `b < 128
? b : slow(p)` with the slow path out of line, which is what the hand
experiment did not manage inside the inlined loop.

### 2.11 Unchanged from last round

* **Merge repeated dispatch on the same scrutinee** (`sexp`'s `walk_chain`
  switches on the tag twice). Pure duplication; not measured.
* **`/arch:AVX2`, or clang, for release builds.** 13% on `sum` under clang
  last round; this round adds `scene` (24% better under clang), `respond`
  streaming (22%), `blur_rows` (15%) and `lru` (18%) to the list of rows
  where the backend choice matters more than anything above.

### 2.12 `return from`: not measured, estimated

The discriminant is an `int32` out-parameter checked after every call on a
propagation path: `calc` has 45 such sites and makes roughly 60 calls per
input, 1.6M inputs, so on the order of 100M predictable branches and loads --
5-10% of the row by count, on inputs that never leave L1. The hand experiment
to measure it (neutralising the checks in the error-free variant) crashed,
because the same temp pattern is used by ordinary booleans; a clean
measurement needs a compiler flag that omits the checks. Cheaper alternatives
if it turns out to matter: return the discriminant in a register alongside
the fixed result (a two-register struct return) instead of through memory,
and skip the check after calls to functions that cannot propagate to any
live target.

## 3. Language and spec questions the measurements raise

### 3.1 Self-relative or pool-relative offsets

Spec 3.9 makes relative references self-relative so that a linked region is
position-independent as a unit and any sub-region can be moved (the
whole-region copies of TODO 16, not yet implemented). `lru` prices that
choice at 1.5x on a relinking workload, and the index variant shows what the
alternative is worth: an offset measured from the *pool's* base loads as
`base + off` with the base register-resident and stores as `ptr - base`,
with no dependency on the field's own address and no range check at all for
a `u32` on a pool under 4 GB. The whole pool stays mappable. What it costs is
that every load of a relative reference needs its root's base, and the
checker's roots are conservative in exactly one place: a reference read back
out of a container is rooted at the container (9.5), so `slots[i].node.next`
would be resolved against the wrong base. Pool-relative offsets are therefore
gated on precise roots for stored references -- struct fields are "implicitly
generic over their roots" (9.2), which is most of the machinery -- or on a
runtime base carried with the reference. Worth an experiment behind a flag
before a spec decision; the payoff is the entire `lru` deficit.

### 3.2 A constructible self-reference

For non-optional relative links to be usable in cyclic structures (2.9), the
value under construction needs a name, or offset 0 on a non-optional slot
needs a defined meaning. The second is already what the representation does
(`a8c9f69`), so documenting it -- "a non-optional relative reference field
constructed from `self` points at the containing value" -- is the smaller
change; the first is the more readable one.

### 3.3 Return roots across a cycle

2.8 changes what the checker accepts (a cycle function may `return` the
result of a back-edge call); the spec's 7.8 should say that cycle return
roots are the fixpoint over the cycle's returns, and TODO 0g goes away.

### 3.4 Row slices, not a 2D type

With 2.5a and 2.5b in place, the row-slice kernel needs no assert and the
flat kernel proves its checks, so the language does not need a
multi-dimensional array type to be fast on images and matrices. Worth
stating in the performance notes: slice the rows, index within them.

### 3.5 Scratch buffers that hand out slices

`calc` reads its input through a global grow-shrink buffer by index because a
grow-only array has no `clear` and a grow-shrink one cannot be sliced.
A flow-sensitive relaxation -- `clear()` on a grow-only *local* is legal at a
point where no reference or slice rooted at it is live -- would give the
"reusable scratch that can be sliced" the spec's TODO 4 asks for, at no
runtime cost. Not a speed item; it removes the one place the new benchmarks
had to choose between reuse and references.

### 3.6 Arithmetic width and `varint` in the performance notes

Two guidance items rather than changes: a sum of `u8` taps wraps (6.2) and
needs `as u16` on each -- `blur`'s first version was a non-blur for that
reason -- and `varint` costs a decode per read (2.10). Both belong in a
"choosing storage types" note next to Appendix A.

## 4. Suggested order

1. **Codegen, low risk, this round's mechanics:** 2.1 (tops through fat
   references), 2.4 (views through fat references), 2.7 (tops only in loops
   that push), 2.5a (slice lengths), 2.9's range-check skip. Expected:
   `bintrees` -12%, `push`-by-reference +8%, `blur` 4x under clang,
   `particles` +6% under clang, `blur_rows` without asserts, `lru` +11%.
2. **Optimizer:** 2.2 (base-case inlining), 2.3 (TRE), 2.6 (inlined named
   results at the destination), 2.8 (fixpoint roots). Expected: `bintrees` at
   parity with Rust, `tree` +7% under v145, `respond` DTO up to 30%, `calc`
   without its Paren node.
3. **Analysis and language:** 2.5b (product indices: `blur` 4x under v145),
   2.5c (array contents), the pool-relative experiment of 3.1 (the `lru`
   deficit), 3.2 (self-referential sentinels), 3.5 (clearable scratch).
4. **Measure, do not guess:** hardware counters on `lru`'s relative row, a
   flag to omit `return from` checks for 2.12, and re-run the suite after
   each of the three rounds -- the reproducibility caveats in `notes.md`
   apply to every number here.
