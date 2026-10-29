# Results of the compiler changes proposed in plan.md

> **Deprecated language features:** `reusable` and `reusable[]`. Use
> [`slot_pool<T>`](../docs/stdlib.md#slot_pool) for slots and
> [`slice_pool<T>`](../docs/stdlib.md#slice_pool) for contiguous runs.
> Builtin behavior and historical examples below remain relevant to existing
> code; removal is planned separately, after the remaining migration gaps.

*This document records a completed round of compiler changes, their
measurements, and the reasons for adopting or rejecting each one. See
`results.md` for the benchmark report.*

There were sixteen branches, one per item of `bench/plan.md`. Each was
implemented, with tests, by its own agent on its own `opt/<name>` branch from
master `caf3b65`.

Each branch was then measured on a quiet machine using the same procedure. The
baseline compiler and the branch's compiler generated every Goose row at the
`large` size. Both toolchains built the output, with `-falign-loops=32` on both
sides for clang. Runs alternated between baseline and candidate, taking the best
of five and checking that outputs were identical. Ratios are baseline time
divided by candidate time: above 1.00 is faster.

Rows unaffected by a branch emit byte-identical C. Across all sixteen runs,
their ratios ranged from 0.94 to 1.08 and mostly stayed within 2% of 1.00. A
change under about 5% on a single row is therefore noise. Only
`graph`/`graph_csr`, the cache-bound random-access rows, are noisier than that.
Where an optimization needs a source pattern absent from the suite, the branch
includes a variant source, timed the same way and marked *variant*.

## Summary

| branch | item | rows that changed, large, v145 / clang | diff: compiler src, all files | verdict | future code |
|---|---|---|---:|---|---|
| `opt/fatref-tops` | 2.1 tops through fat refs | bintrees **1.12 / 1.16**, calc 1.00 / 1.03, respond 1.01 / 1.00; *variant* push-by-reference 1.08 / 1.08 | +121 -9; all +256 -9 | adopt | high |
| `opt/base-case-inline` | 2.2 inline the base case | bintrees **1.35 / 1.13**, tree 1.02 / 1.01, sexp 1.00 / 0.98 | +238 -3; all +369 -3 | adopt | high |
| `opt/tre` | 2.3 accumulator TRE | bintrees **1.09** / 1.00, sexp 1.02 / 1.00, calc 1.02 / 1.01, tree 1.01 / 0.99 | +490 -4; all +659 -4 | adopt, one decision | medium |
| `opt/fatref-views` | 2.4 hoist views through fat refs | blur **0.94 / 6.96** | +129 -1; all +169 -1 | adopt | high |
| `opt/bce-slice-product` | 2.5a+b slice lengths, products | scene 1.02 / 1.00; *variant* blur with a length assert **2.07 / 7.02** | +179 -23; all +349 -23 | adopt | high |
| `opt/bce-array-contents` | 2.5c array contents | graph 1.07 / 0.97, graph_csr 0.97 / 1.02, lru 1.01 / 1.01 | +493 -16; all +715 -21 | hold | low as speed |
| `opt/inline-nrvo-dest` | 2.6 inlined named results in place | respond **1.06 / 1.06**, records 1.01 / 1.03 | +215 -58; all +343 -58 | adopt | high |
| `opt/per-loop-tops` | 2.7 tops only in growing loops | particles 1.00 / **1.06**, particles_scalar 1.00 / **1.06**, all push rows within 2% | +236 -47; all +279 -47 | adopt | high |
| `opt/cycle-return-roots` | 2.8 cycle return roots | no code change; *variant* calc without its Paren node **1.04 / 1.09** | +373 -3; all +721 -15 | adopt | high |
| `opt/relref-rangecheck` | 2.9 skip the range check | no-op at the harness's 8 GB; lru +5-12% at 2 GB (agent) | +35 -6; all +66 -8 | adopt with a 2 GB harness | medium |
| `opt/varint-fastpath` | 2.10 varint fast path | records 1.00 / 1.05, sexp 0.98 / 1.03, calc 1.00 / 1.00 | +65 -19; all +93 -19 | adopt (prefixes only) | medium |
| `opt/dispatch-merge` | 2.11 merge dispatch | sexp 1.01 / 1.01 | +328 -124; all +420 -124 | drop for now | low |
| `opt/retfrom-register` | 2.12 `return from` discriminant | calc **1.04 / 1.04**, sexp 1.03 / 1.02 | +41 -34; all +267 -37 | adopt | high |
| `opt/pool-relative` | 3.1 pool-relative offsets (flag) | lru 1.04 / **1.41**, graph 1.03 / 1.18, bintrees 0.96 / 0.93, walkers 1.00; calc does not compile | +405 -41; all +517 -41 | hold, language decision | medium |
| `opt/self-reference` | 3.2 `self` in literals | no code change; *variant* lru with non-optional links **1.10 / 1.01** | +126 -26; all +373 -29 | adopt | medium |
| `opt/clear-grow-only` | 3.5 `clear()` on grow-only locals | no code change; *variant* calc over slices 0.92 / 0.90 | +85 -2; all +433 -5 | adopt as expressiveness | medium |

*Adopt* means the change is sound, its complexity is justified, and it
benefits the suite or similar code. *Hold* means keep the branch, do not merge
yet. The last column is a prediction of how often future Goose code hits the
construct, judged from the shapes the spec's examples and the sixteen
benchmarks are built from.

The measurements support the expected combined gains. Done by hand, 2.1 +
2.2 + 2.3 took `bintrees` from 356 to 229 ms under v145, against Rust's 227.
On the branches, 2.2 alone gives 1.35, 2.1 gives 1.12 and 2.3 gives 1.09,
and each affects a different part of the same recursion. `blur` needs 2.4
for clang (7x) and 2.5b for v145 (2x, once the length of the reference
parameter is known); 2.4 alone makes that row 6% slower under v145, and
2.5b's elision then removes that loss.

## Merged changes

The changes were merged onto master from `caf3b65` in this order:

1. `/bigobj` for the compiler build; 2.7 per-loop-tops; 2.1 fatref-tops; 2.4
   fatref-views.

2. 2.5a+b bce-slice-product; 2.12 retfrom-register; 2.6 inline-nrvo-dest; 3.2
   self-reference.

3. 2.9 relref-rangecheck, with the harness reservation lowered to 2 GB; 2.10
   varint-fastpath; 3.5 clear-grow-only.

4. 2.2 base-case-inline; 2.8 cycle-return-roots; 2.3 tre; then the fixes for the
   pre-existing bugs listed below.

Decisions taken along the way:

* 2.3's footnote was resolved by loosening the guarantee rather than the
  pass: a debug build aborts on a signed overflow per operation *as it
  executes*, and the optimizer may regroup or drop operations (spec §6.2,
  §9.3).
* The three large, self-contained analyses moved to their own headers so that
  the passes that use them stay readable: `src/optimize_tre.h`
  (`TailRecursion`), `src/optimize_basecase.h` (`BaseCaseInliner`) and
  `src/typecheck_cycles.h` (`CycleRoots`); `optimize.h` and `typecheck.h`
  keep one declaration and one call each.
* 2.5c bce-array-contents and 2.11 dispatch-merge were dropped and their
  branches deleted. The while-exit guard listed below as a fix on 2.5c turned
  out to protect that branch's own index postconditions rather than fix a bug
  in the base, so nothing was ported from it.
* 3.1 pool-relative was held on its branch pending the language-level
  question of what a compressed reference denotes; the answer is the
  `in pool` form of spec §3.9 (`docs/design/relative_references.md`), implemented
  afterwards without the flag branch's hidden arguments or fixpoint.
* The variant sources the branches used for their claims stay in
  `bench/goose/` beside the rows (`push_ref`, `blur_assert`, `lru_nonopt`,
  `calc_slices`, `calc_noparen`, `calc_noerr`); the manifest does not list
  them.

The table below shows everything together: baseline `caf3b65` against the
landed compiler, at the large size with the 2 GB reservation, under both
toolchains, with the same method as above. It was measured before the fixes
branch went in. Of the suite's generated C, the fixes change only `lru`'s
two alloc literals, which are now built in their slot instead of copied from
a temporary, and the harness rows for `lru` and `calc` were re-measured
afterwards:

| row | v145 | clang | row | v145 | clang |
|---|---:|---:|---|---:|---:|
| sum | 0.99 | 0.99 | particles | 1.00 | 1.06 |
| push | 1.00 | 1.01 | particles_scalar | 1.00 | 1.07 |
| strlist | 1.00 | 0.98 | sexp | 1.00 | 1.05 |
| records_var | 1.04 | 1.09 | lru | **1.13** | **1.14** |
| records_fixed | 1.01 | 1.00 | scene | 1.01 | 0.99 |
| tree | 1.05 | 0.98 | calc | 1.04 | 1.06 |
| interp | 1.00 | 0.99 | bintrees | **1.53** | **1.37** |
| graph | 1.03 | 1.01 | respond | 1.08 | 1.09 |
| graph_csr | 0.99 | 1.06 | respond_stream | 1.00 | 1.00 |
| words | 1.01 | 1.03 | blur | **0.94** | **6.85** |
| | | | blur_rows | 1.00 | 1.00 |

`bintrees` under v145 went from 360 to 235 ms against Rust's 227, the
combined gain the plan predicted. The only loss is `blur` under v145, also
as predicted: the nine checks per pixel stay because the reference
parameter's length is not known inside `blur`, and the hoisted views take
two more live locals. A one-line `assert(src.len == W * W)` in the benchmark
makes the row 2.07x / 7.0x (the `blur_assert` variant); whether to add it to
the benchmark source was left open.

---

## 2.1 `opt/fatref-tops` -- cache stack tops through fat references

**What.** A function holding a resizable by reference (`pool: T[>..]&`)
keeps `ref.stk->top` in a local, flushed before and reloaded after calls,
where before any such function fell back to the memory form for every stack.
Eligibility comes from the checker's root classes:

* there is one cache per fat-reference *parameter*;
* same-class pairs are rejected (same class does not prove same stack);
* any fat reference in a local, binding, field or by-value resizable
  parameter disables the mode;
* a global whose type matches a parameter's pointee keeps the memory form.

All of it is in `codegen.h`.

**Measured.** bintrees 1.12 / 1.16; calc 1.00 / 1.03 (its pushes are a small
share of the parse); respond level. The `push` loop moved behind two fat
references (`push_ref.goose`, on the branch) went from 170.5 to 157.7 ms
under v145 and from 166.8 to 154.0 under clang, 1.08 on both. Every other
row is byte-identical.

**Complexity and side effects.** The change adds one predicate, one flag and
one string set, and reuses the flush discipline the global caching already
uses. Two conservative choices limit the speedup while preserving soundness:
every call flushes everything in this mode, and globals are not cached
alongside. The branch also fixed a pre-existing unsoundness in
`CanCacheTops`, with a regression test. `CanCacheTops` did not see a fat
reference read out of a field or element, so a body that pushed to a global
both directly and through such a reference cached one form of the access and
wrote through the other (the repro printed `2,3,0` for `1,2,3`). That fix
is useful independently of the optimization.

**Verdict: adopt.** **Future code: high.** "Recursive builder into a pool
the caller owns" is the idiom the previous round's checker change made
possible, and every per-request or per-input arena is written that way.

## 2.2 `opt/base-case-inline` -- inline a self-recursive function's base case

**What.** In a `recursive fn` whose body starts with `if c { return e }` (or
the `guard` form), each direct self-call is rewritten to
`if c' { e' } else { f(...) }`, with the arguments bound to temporaries,
once per site. This applies only under a whitelist: all parameters are
fixed-size, the test is pure, the result does not call into the cycle, and
the size is under the inliner's thresholds. It is implemented in
`optimize.h`, reusing the inliner's copier.

**Measured.** bintrees 1.35 / 1.13, tree 1.02 / 1.01, sexp level. It fires on
`bintrees.make`, `tree.build` and `sexp.walk_chain`. It does not fire on
`interp` and `scene` (a statement precedes their base case), `calc` (mutual
recursion) or `bintrees.check` (binds a local first).

**Complexity and side effects.** Code growth is bounded by the existing
thresholds (`make` grows from 32 to 62 nodes). The test is evaluated twice
on the recursing path, so it must be pure, which the whitelist enforces;
chain recursion is roughly neutral. The agent avoided a real hazard: a
non-fixed parameter would give a cycle member a non-fixed local, which the
checker rejects when written by hand but codegen silently accepts. The
agent also found the `INT32_MIN` bug listed below.

**Verdict: adopt.** **Future code: high** for tree walks and builders, which
is most recursion in this language. Extending it to a base case that is not
the first statement (`scene`, `interp`) is the obvious follow-up.

## 2.3 `opt/tre` -- accumulator tail-recursion elimination

**What.** Self-recursive integer-returning functions whose returns fold with
an associative operator become a loop with an accumulator; parameters become
rebindable locals. The transform does not apply to floats, to callees that
can `return ... from` the function, to tails inside loops, or to relative,
fat or pool parameters. The branch adds `/bigobj` to the build.

**Measured.** bintrees 1.09 under v145 and 1.00 under clang, which already
does this; sexp 1.02 / 1.00; calc 1.02 / 1.01 (its generator is a plain tail
call); tree level at depth 24, where the row is memory-bound.

**Complexity and side effects.** This is the largest optimizer branch (485
new lines in `optimize.h`, against 238 for 2.2), and it is also the branch
with a semantic footnote. Regrouping a signed `+`/`*` chain changes the
intermediate values, so a debug build's overflow abort can fire where it
did not before, or not fire where it did; release builds are bit-identical.
That makes a single, documented exception to the optimizer's "aborts
included" rule. The strict alternative (unsigned and bitwise operators
only) would disable all four benchmark instances. Expression-bodied folds
(`if c { 0 } else { 1 + f(x) }`) are not matched.

**Verdict: adopt after deciding the footnote**: either accept it as
documented, or gate the signed case behind `-O2`. The transform exists to
bring v145 level with clang. **Future code: medium.** `return e op self(x)`
is common in walks and sums, and half the users are on the backend that
already does this transform.

## 2.4 `opt/fatref-views` -- hoist array views reached through references

**What.** A loop that indexes an array reached through a fat reference loads
its base and length once before the loop when BCE's kill summary says the
body cannot change that array (no growth or shrink of it, no call that can
reach it); element stores through any reference do not block it. The change
is half analysis (`bce.h`) and half codegen.

**Measured.** blur 6.96 under clang (1,858 to 267 ms) and 0.94 under v145,
where the nine checks per pixel remain and two more live locals cost 6%.
Every other row is byte-identical.

**Complexity and side effects.** The change is small and self-contained. The
analysis asks the existing kill summary one more question, and the length
lvalue used for growth stays on the real header, so a missed growth could
never silently miscompile. Both base and length must be hoisted to get the
gain; neither alone produced a measurable gain. The v145 loss on `blur` goes
away once 2.5b can prove the checks in that function (which needs the
reference parameter's length, below).

**Verdict: adopt.** **Future code: high.** `fn kernel(a: T[>..]&, b:
T[>..]&)` is how spec 7.4 says to write a mutating range algorithm, and every
such loop pays this today under clang.

## 2.5a+b `opt/bce-slice-product` -- slice lengths and product indices

**What.** The bounds-check domain records `len(a[lo..hi]) = hi - lo` when
the bounds are nameable, and does interval arithmetic for products and
two-term sums of bounded counters, as one-shot bases and only where the
result provably fits the expression's width. The change is in `bce.h`, plus
nine annotated tests.

**Measured.** Elision: `scene` 9/13 to 12/13 (the 3x3 `p.m[i * 3 + k]`),
`blur_rows` without its asserts 0/10 to 10/10. `scene` 1.02 / 1.00. `blur`
itself stays at 0/10 because its checks are in `fn blur(src: u8[>..]&, ...)`,
which is not inlined, and the length fact is established in `main`. With one
`assert(src.len == W * W)` per call (`blur_assert.goose`, on the branch) it
reaches 10/10 and goes from 1,922 to 930 ms under v145 (2.07) and from 1,860
to 265 under clang (7.02).

**Complexity and side effects.** There is no new pass; terms for products and
sums now flow into everything that consumes terms (`for i in n * m`,
`resize(n * m)`, `let` equalities). Compile time goes up by 2%. The width
check is what keeps the analysis sound against release-mode wrapping.

**Verdict: adopt.** **Future code: high** for the row-major idiom (images,
matrices, tables) and for any slice taken with an arithmetic bound. The
missing piece for `blur` as written, the length of a reference parameter, is
an interprocedural fact and a separate item.

## 2.5c `opt/bce-array-contents` -- index postconditions and contents invariants

**What.** After `a[i]` has executed, `0 <= i < len(a)` holds; and for local
integer arrays where every write provably satisfies `0 <= v <= const` or
`v <= len(P) + c` with `P` never shrinking, loads from the array carry that
fact. It is a third recording walk in `bce.h`.

**Measured.** Elision: `graph` 5/9 to 8/9, `graph_csr` 11/21 to 17/21, `lru`
0/18 to 2/18. Time: no measurable change (graph 1.07 / 0.97,
graph_csr 0.97 / 1.02, lru 1.01 / 1.01), with opposite signs per toolchain
on the noisiest rows in the suite.
These are perfectly predicted branches on register-resident values, which is
what last round's `words` rewrites found; plan.md's 5-10% came from the
whole-pass `--no-bce` A/B, which also disables the loop-view hoist.

**Complexity and side effects.** This is the largest analysis change, and
its risk is whether it enumerates every kind of write (push, append, store,
pointee write, resize, whole-array assignment, freelist `free`). It also
fixed a latent while-exit hazard in the existing analysis. Some of the
remaining checks cannot be removed: the `out[fill[s]]` invariant is false at
the last vertex.

**Verdict: hold.** The branch is correct and thoroughly tested, but there is
no measurable speedup and the analysis is large. Merge it when a benchmark
shows loaded indices costing time, or as the base for per-array index types
(TODO 0e). **Future code:** medium as a foundation, low as a speedup.

## 2.6 `opt/inline-nrvo-dest` -- inlined named results built at the destination

**What.** When the optimizer inlines a builder whose result is a named
resizable local into a construction context, the local is bound to the
destination stack and the inlined `return` writes only the length prefix.
A fixed-width prefix is reserved and patched (no move); a varint prefix
reserves one byte, and the elements are moved only when the count reaches
128. The same reserve-then-patch replaces the memmove in the non-inlined
forms. The change touches `ast.h`, `optimize.h` and `codegen.h`.

**Measured.** respond 1.06 / 1.06 (557.8 to 524.5 ms, 534.2 to 503.3),
records 1.01 / 1.03. No result copies remain in respond's `handle`, and its
data stacks drop from four to two. The DTO row's gap to the streaming row
goes from 26% to 19%.

**Complexity and side effects.** Medium: one map replaces `nrvodst`, and
three helpers replace `EmitNrvoPrefix`. There is one forced behaviour
change: a multi-name receive (`let a, b = f(); return a;`) is no longer an
NRVO candidate, because it was already miscompiled; it now copies, which is
what spec 7.3 says. The agent found four more latent bugs around named
results (below).

**Verdict: adopt.** It makes spec 7.3's guarantee hold for the inlined case,
which is the common one. **Future code: high**, for DTOs, string builders,
and anything returning a built-up `u8[]` or `T[]` into a field or a push.

## 2.7 `opt/per-loop-tops` -- cache stack tops only in loops that grow

**What.** The cached-top local is confined to the loops (or the whole body,
when growth is outside every loop) that actually grow a stack; a float loop
that only rewrites elements keeps the memory form. Deferred placeholders are
resolved after the body is emitted, and escapes out of a pushing loop
(`break`, an inlined early `return`) are flushed by label position. The
change is in `codegen.h`.

**Measured.** particles 1.06 and particles_scalar 1.06 under clang (916 to
862 ms, 913 to 858), level under v145; every push-heavy row within 2% (sum
1.01 / 1.00, push 0.99 / 1.00, strlist 0.99 / 0.98, sexp 1.00 / 1.00,
bintrees 1.00 / 1.01, lru 1.00 / 1.00). This recovers the previous round's
6% clang regression on float kernels without a measurable regression
elsewhere.

**Complexity and side effects.** This is the most intricate codegen change
of the set: it does textual post-processing over label positions, and the
Debug object is at MSVC's section limit. Every function's temp numbering
shifts, so generated-C diffs against master are noisy. Stacks grown only by
callees are also no longer cached, which removes flush/reload pairs from
`bintrees` and `lru`.

**Verdict: adopt.** **Future code: high**, for any array filled once and
then processed in place, which includes every simulation and image loop.

## 2.8 `opt/cycle-return-roots` -- return roots of a recursive cycle by fixpoint

**What.** Before a `recursive fn` is checked, a syntactic scan over the
cycle's returns (`X.push(...)`, `&X[...]`, typed `let` locals, calls into
the cycle) is iterated to a fixpoint. The result seeds each function's
return root, which is then verified against the real returns. A back-edge
result whose root cannot be determined gets a sentinel root that cannot be
stored or returned. The change is in the checker only; spec 7.8 is updated
and TODO 0g is done.

**Measured.** No performance change by design: the C is byte-identical for
all 22 sources. `calc` without its Paren node (`calc_noparen.goose`, on the
branch) typechecks with relative links throughout, drops 311,505 nodes,
keeps the checksum, and runs 545 to 526 ms under v145 (1.04) and 537 to
492 under clang (1.09). `sexp`'s back-edge result is now pool-rooted instead
of static.

**Complexity and side effects.** The change is large for a checker change,
but it closes a soundness hole the baseline has (a reference into a
caller's local pool stored into a global is accepted today) and removes a
language artefact that a first-time parser writer would run into. There is
one behaviour change: a back-edge result rooted at a non-pool reference
parameter is now non-storable inside the cycle, where it was silently
static.

**Verdict: adopt.** **Future code: high** for anything mutually recursive
returning references, which is every parser.

## 2.9 `opt/relref-rangecheck` -- skip the relative-reference range check

**What.** The store-time range check is emitted under `#if GS_STACK_RESERVE
> (1ull << (bits-1))` and only when no fixed-size root in the program is
wider than the width, so a `u32` link on a stack under 2 GB is stored
without a check. Spec 3.9 and 9.3 note the condition, and the branch adds a
deterministic abort test at the default 256 MB.

**Measured.** At the harness's 8 GB reservation the branch is provably a
no-op (identical preprocessed code; the suite A/B is all noise). At 2 GB the
agent measured `lru` +5-12% in its best-controlled runs, and everything else
within noise; the largest Goose row needs 536 MB, so 2 GB has 4x headroom.

**Complexity and side effects.** The change is small. The agent found a
pre-existing bug: a struct literal with relative-reference fields assigned
into an *element* (`a[2] = C2 { link: &a[1] }`) is built in a C temp and
copied, so its offsets are wrong, and only the range check ever detects it.
That is why that path keeps its check unguarded.

**Verdict: adopt, together with lowering the harness reservation to 2 GB
and fixing the literal bug.** **Future code: medium.** It affects every
relative store, but only relink-heavy code shows a difference.

## 2.10 `opt/varint-fastpath` -- varint decode fast path

**What.** A one-byte fast path with an out-of-line slow continuation, for
array *length prefixes* only.

**Measured.** The item as written does not work, and the agent showed why:
36% of `calc`'s values are two-byte after zigzag encoding and 92% of
`sexp`'s `Num.v` values are three-byte, so an out-of-line slow path for
field values is a loss (both variants were measured and reverted). Length
prefixes are one byte below 128 elements, and a walk of 128+ elements
amortises the call; for them the results are records 1.00 / 1.05,
sexp 0.98 / 1.03, calc unchanged.

**Verdict: adopt the length-prefix half.** The guidance in `notes.md` stands:
`calc`'s 11% needs `u8`, not decoder work, since a value of 64 already costs
two bytes. **Future code: medium.** Every variable array of variable
elements walks its prefixes.

## 2.11 `opt/dispatch-merge` -- merge repeated tag dispatch

**What.** Two case-function calls on the same never-written scrutinee in one
expression or statement sequence share a switch. The branch includes a
mechanical split of `EmitDispatch` into setup and arm.

**Measured.** Only `sexp` has the shape; sexp 1.01 / 1.01. On a
walk-dominated variant the walk itself gains 2-6% under clang and 0-2% under
v145, about 1% of the row.

**Verdict: drop for now.** The change is correct and conservative, but
nothing in the suite or the spec's idioms shows it above noise, and it is
not small. Keep the branch; the `EmitDispatch` split is worth taking if that
code is touched again. **Future code: low.**

## 2.12 `opt/retfrom-register` -- the `return from` discriminant

**What.** The discriminant moves from an `int32_t *` out-parameter on every
propagating function to one thread-local that is zero except between a
`return ... from` and its catch, so ordinary returns write nothing and a
call site costs a load and a never-taken branch. A measurement flag
`--unsafe-no-rf-check` is documented as unsound. The emitter is simpler
overall; spec C.3 is updated.

**Measured.** calc 1.04 / 1.04 (548.9 to 527.7 ms, 535.1 to 514.1), sexp
1.03 / 1.02. According to the agent's breakdown, the checks alone were 2%
(v145) / 4.7% (clang) of the error-free `calc`, and the whole mechanism
5-7%; after the change the checks have no measurable cost (the flag adds no
further speedup). NULL-signalling pointer results, a two-register struct
return (Win64 returns 16 bytes through memory) and a plain global were
measured and rejected.

**Verdict: adopt.** **Future code: high.** Every parser, loader and
validator uses `return from`, and every function on a propagation path paid
for the out-parameter.

## 3.1 `opt/pool-relative` -- pool-relative offsets, behind `--relref-base`

**What.** Relative references are encoded as offsets from the root pool's
base, and the base is found from the checker's roots: a global, a local, a
fat reference's header, or a hidden `uint8_t *` argument where a plain
reference parameter's class is not pinned to one pool. There are two
checker additions, so that nothing is ever resolved against the wrong base:
a least fixpoint over (class root, call-site root) pairs, and an "exact
root" bit separating a real root from a container-laundered one. With the
flag off, the C is byte-identical.

**Measured.** With the flag: lru 1.04 / 1.41 (2,991 to 2,123 ms under
clang), graph 1.03 / 1.18, bintrees 0.96 / 0.93 (the hidden argument on a hot
recursion), tree, interp and scene level, and `calc` does not compile. Its
Paren node holds a plain reference read back out of a container, which is
the only construct with no base, and is the artefact that 2.8 removes.

**Complexity and side effects.** The representation is 40 lines; the rest
implements root tracking that the compiler previously lacked. Narrow widths
change meaning (a `u16` would bound the pool at 32 KB rather than the link
distance) and sub-region copies (TODO 16) would need rebasing.

**Verdict: resolved at the language level.** The comparison is written up
in `docs/design/relative_references.md`, and the outcome is the `in pool`
form of spec §3.9: a field names a global pool, with no hidden arguments
and no per-call-site fixpoint. `lru` with 8-byte reference slots is level
with the index form under v145 and 1.31x faster under clang. The experiment
answered plan.md 3.1: pool-relative recovers about half of the `lru`
deficit under clang and little under v145; it has no measurable cost on the
build-once walkers, and costs a hot recursion 4-7%. It is not a default; a
per-declaration choice (`T&<u32 pool>`) would keep both encodings, with 2.8
merged first so the blocker disappears. **Future code: medium**, for
relinking structures only.

## 3.2 `opt/self-reference` -- `self` in literals

**What.** The branch adds a `self` keyword, valid as the whole initializer
of a non-optional relative-reference field whose pointee is the literal's
own type. It stores minus the field's offset (a constant for fixed layouts,
width-checked at compile time). The change touches the lexer, parser,
checker, codegen, and spec 2/3.9/4.2/D. Existing sources produce
byte-identical C.

**Measured.** `lru_nonopt.goose` (non-optional links, sentinels via `self`,
no narrowing asserts) against `lru` on the same compiler: 3,734 to 3,387 ms
under v145 (1.10), 3,152 to 3,113 under clang (1.01).

**Complexity and side effects.** The change is one keyword in one position.
Non-optional relative references need `let p: Node& = n.prev;` annotations,
because an unannotated `let` decays a plain reference. `self` is preferable
to defining offset 0 as the containing value: 0 is already null for
optionals, and on a non-optional the representation reads 0 as the field's
own address. The branch also found another latent bug (`alloc_index` with
a relative-reference literal, below).

**Verdict: adopt.** **Future code: medium**, for sentinel-linked and
circular structures, which are common in the mutation-heavy code that the
`reusable` pool exists for.

## 3.5 `opt/clear-grow-only` -- `clear()` on grow-only locals

**What.** `clear()` on a grow-only *local* is accepted where no reference or
slice rooted at it can be live. This is checked by a scan over the open
scopes' variables, plus a guard against clears inside value-producing
constructs. Codegen uses the grow-shrink clear, and the bounds-check
analysis already killed its facts correctly. Spec 5.1, 3.3 and TODO 4 are
updated; the branch is mostly tests and spec.

**Measured.** `calc_slices.goose` (grow-only local text cleared per input,
parser over slices) is *slower* than `calc`: 544 to 589 ms under v145 (0.92),
532 to 588 under clang (0.90). The causes are the generator's pushes
through a fat reference (2.1's territory) and a 16-byte slice passed through
the recursion instead of a named global.

**Verdict: adopt as an expressiveness item, not a speed one.** It lets a
scratch buffer support both reuse and slices. On master the same rule now
also covers `pop` and `resize`, and spec 5.1 defines grow-only as "never
shrinks while a reference or slice into it can be live". A shrink must also
stand on its own (a statement, an initializer, or the right-hand side of an
assignment to a variable), so that no reference taken earlier in the same
expression can outlive it. **Future code: medium.** Per-iteration scratch
buffers are everywhere; whether they need slicing is case by case.

---

## Merge order and interactions

The merge order is listed under "Merged changes" above. The conflicts were:

* 2.1 with 2.7, in the top-cache state and its per-function reset (2.7's
  machinery, with 2.1's mode on top);
* 2.6 and 2.10 with 2.7, at the points that grow a stack, which now go
  through 2.7's growth-recording accessor, and 3.5 likewise at `clear`;
* 2.9 with 2.12 in the CodeGen constructor, and with 3.2 in the literal
  store;
* 2.2 and 2.3 with each other, only in the optimizer's summary line and the
  header list;
* 2.3's spec bullet with 2.9's.

Every branch appended to the same test file and expected outputs, which
merged mechanically except for one test type name that two branches both
chose. `/bigobj` went in first, since three branches hit MSVC's per-object
section limit in this single-translation-unit compiler. If 3.1 is ever
revisited, 2.8 goes in before it: 3.1's "exact root" bit is what a default
pool-relative encoding would build on, and 2.8 removes 3.1's only blocker.

## Pre-existing bugs the agents found

None was introduced by a branch; each was reproduced on the baseline. The
first three are fixed on master (the third on 2.1's branch, the other two on
`opt/fixes`, merged last); the rest are open.

* `CodeGen::IntStr` emitted `INT32_MIN` as the unsuffixed `-2147483648`,
  which MSVC's C front end types as unsigned (C90 literal rules), so every
  checked `as i32` aborted in a `GS_DEBUG=1` build and an `i64` literal at
  that value printed positive in release (found by 2.2 and 3.1). It is now
  `(-2147483647 - 1)`, the form `INT64_MIN` already used.
* Struct literals with relative-reference fields were sometimes constructed in a
  C temporary and copied, leaving offsets measured from the temporary (2.9,
  3.2). This happened when assigning a literal into an element or a struct-typed
  field, storing it whole through a reference, or building it through
  `alloc_index`/`alloc_ref`. Array literals whose elements are relative
  references also emitted invalid C. All of these now build at the destination.
* `CanCacheTops` missed fat references read out of fields and elements,
  which was unsound; this was fixed on 2.1's branch.
* A variable-class (not resizable) named result is not built at its
  destination at all: `fn mk() -> R { let r = R{...}; return r; }` returns
  garbage at -O0 (2.6).
* `GenArrayLit` writes the callee's length storage into a `u8[varint]`
  element slot for `[f(), g()]` (2.6).
* A global initializer whose builder needs an element-run twin fails to
  compile at -O0 (2.6).
* `buf.append(f(&buf))` was accepted although the callee grows the stack the
  result is being built on (2.6); the checker now rejects a growth of an
  array while a value is built in place in it (spec 1.3(4), 4.2).
* The `While` exit-flow hazard reported on 2.5c's branch is a guard for that
  branch's own index postconditions; the base has no such fact to shift, and
  its own `break` guard is already in place.

## Measurement notes

* `scratchpad/ab_goose.ps1` is the A/B driver; `ab_all.log` the full run
  (672 rows). The identical-code rows across all sixteen runs read 0.94-1.08,
  and only on `graph`/`graph_csr` outside 0.97-1.03.
* Every agent found the machine too noisy for sequential timing while
  fifteen others were building; the reported agent numbers were taken with
  interleaved or repeated runs and agree with the quiet-machine run within
  that noise everywhere except the two cache-bound rows.
