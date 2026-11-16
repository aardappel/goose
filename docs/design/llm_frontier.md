# Claims the compiler checks and spends: Goose for machine authors and human reviewers

<details>
<summary>Table of contents</summary>

- [0. Summary](#0-summary)
- [1. The question](#1-the-question)
  - [1.1 What changes when the author is a machine](#11-what-changes-when-the-author-is-a-machine)
  - [1.2 Three axes, weighted](#12-three-axes-weighted)
  - [1.3 A proof is also an optimization](#13-a-proof-is-also-an-optimization)
- [2. What Goose already proves](#2-what-goose-already-proves)
  - [2.1 The prover](#21-the-prover)
  - [2.2 The claims](#22-the-claims)
  - [2.3 What it cannot state, and what it does not spend](#23-what-it-cannot-state-and-what-it-does-not-spend)
- [3. Correctness: a ladder of claims](#3-correctness-a-ladder-of-claims)
  - [3.1 Principles](#31-principles)
  - [3.2 Contracts in the fact language: `requires` and `ensures`](#32-contracts-in-the-fact-language-requires-and-ensures)
  - [3.3 `abortfree`: no runtime check of any kind](#33-abortfree-no-runtime-check-of-any-kind)
  - [3.4 Effects: `pure` and frames](#34-effects-pure-and-frames)
  - [3.5 Termination](#35-termination)
  - [3.6 The certificate](#36-the-certificate)
  - [3.7 What stays out](#37-what-stays-out)
  - [3.8 Prior art](#38-prior-art)
  - [3.9 What the LLM results say](#39-what-the-llm-results-say)
- [4. Speed: proofs the optimizer can spend](#4-speed-proofs-the-optimizer-can-spend)
  - [4.1 Distinct roots as `restrict`](#41-distinct-roots-as-restrict)
  - [4.2 Abort-free loops](#42-abort-free-loops)
  - [4.3 `par for`: shared-memory iteration, proven race-free](#43-par-for-shared-memory-iteration-proven-race-free)
  - [4.4 Explicit SIMD](#44-explicit-simd)
  - [4.5 The same bodies on a GPU](#45-the-same-bodies-on-a-gpu)
- [5. Safety: pools](#5-safety-pools)
  - [5.1 The hole that is left](#51-the-hole-that-is-left)
  - [5.2 Generations, as a library](#52-generations-as-a-library)
  - [5.3 Scoped allocations](#53-scoped-allocations)
  - [5.4 Owning handles: a free is a shrink](#54-owning-handles-a-free-is-a-shrink)
  - [5.5 Prior art](#55-prior-art)
- [6. The cost picture](#6-the-cost-picture)
- [7. Order](#7-order)
- [8. Open questions](#8-open-questions)
- [References](#references)

</details>

Research note, October 2026. Nothing here is implemented, and every piece
of syntax is a placeholder. The question was where the next frontier lies
for a language that is written mostly by language models and reviewed by
people, along three axes: speed, safety and correctness. The answer
proposed here concentrates on correctness, because that is where Goose
has an unusual head start: it already proves things about programs in
every compile, it already lets a programmer demand that a proof succeed
(`checkfree`), and every proof it makes is also information its optimizer
spends. The note proposes to grow that into a small ladder of
compiler-checked *claims* that a reviewer can read in a signature, and to
spend the same proofs for speed (aliasing, vectorization, a parallel loop
that is race-free by proof) and for safety (pool handles that cannot leak).
Section 2 is the inventory of what exists, sections 3 to 5 are the
designs, section 6 is the cost picture, and section 7 the order to build
in.

## 0. Summary

The thesis: **a claim is the unit of trust between a machine author and a
human reviewer**, and the claims worth having are the ones the compiler
checks with the analyses it already runs, states in the language itself,
and then spends on code generation. Goose has three today: `checkfree`
(no bounds check left in this function), index types (`u32 in nodes`: an
integer that is in range wherever it is observed) and `assert` (a fact the
prover takes as given and a check the program keeps). Everything proposed
here is a step from those, in rough order of return on effort:

1. **A certificate.** `goose claims` prints, per function, the claims the
   compiler verified and the facts it used, and the language server shows
   the same as a code lens. The pass already records why every kept check
   was kept; recording why each one was removed is the symmetric half.
   This is the cheapest thing in the note and the one a reviewer sees
   first ([3.6](#36-the-certificate)).
2. **Distinct roots as `restrict`.** The checker already knows, per
   specialization, when two reference parameters have distinct exact
   roots. Emitting that as `restrict` in the C gives MSVC what it lacks
   on the flat blur kernel, today 3.3-3.6x slower than clang's build of
   the same C for exactly this reason; the C++ row of that kernel with
   `__restrict` halves the gap. No syntax; codegen only
   ([4.1](#41-distinct-roots-as-restrict)).
3. **Contracts in the fact language.** `requires` and `ensures` clauses
   whose propositions are the ones the bounds-check prover already
   understands: linear relations between integers and lengths, with a
   boolean guard. A `requires` is an `assert` whose check moves to the
   call site, where it is usually proven away. An `ensures` is a fact the
   caller gets back about a result or a length, which today "has no base"
   and so proves nothing. Both are claims a reviewer reads in the
   signature, and both are facts the optimizer did not have
   ([3.2](#32-contracts-in-the-fact-language-requires-and-ensures)).
4. **`abortfree`.** The rung above `checkfree`: no runtime check of any
   kind remains, so the function cannot abort except by exhausting a
   stack. Its `assert`s must be proven or lifted into `requires`. This is
   SPARK's "Silver" level (absence of run-time errors) for a language
   whose abort set is seven items long, and it is what makes the two speed
   features below legal
   ([3.3](#33-abortfree-no-runtime-check-of-any-kind)).
5. **`pure` and frames.** Declared effects checked against the summaries
   the checker already computes for every function; emitted as `pure` and
   `const` attributes in C, and the basis for compile-time evaluation
   later ([3.4](#34-effects-pure-and-frames)).
6. **`par for`.** A loop whose iterations run on threads of the *same*
   program, accepted only when the body is `abortfree`, grows nothing, and
   the prover shows that no two iterations touch the same element. It has
   no meaning of its own, as `simd` has none: the program computes exactly
   what the sequential loop does, so the compiler may run it either way.
   Goose's thread model keeps its "no data races" promise because the
   race-freedom is proven, not trusted
   ([4.3](#43-par-for-shared-memory-iteration-proven-race-free)).
7. **Owning pool handles.** A handle that is freed exactly once, by the
   program or, where it did not, by the compiler at the owner's exit, and
   whose release is treated as the shrink it is for every reference
   derived from it. It closes the leak and double-free hole of pools and
   most of the stale-read hole, at the price of a non-copyable value
   class, which is the most invasive change in this note
   ([5.4](#54-owning-handles-a-free-is-a-shrink)).
8. **Termination** as a claim, cheap for counted loops and monotone
   `while` loops, honest about recursion over pool-linked structures,
   which it cannot prove ([3.5](#35-termination)).
9. **GPU kernels and explicit SIMD** from the same body shape as
   `par for`, later: large gains on a narrow class of programs, a second
   emitter to maintain, and a layout mismatch to solve first
   ([4.4](#44-explicit-simd), [4.5](#45-the-same-bodies-on-a-gpu)).

Not proposed: an SMT solver, quantifiers over array contents, ghost code,
a proof language beside the programming language, or general refinement
types. Section [3.7](#37-what-stays-out) says why.

## 1. The question

### 1.1 What changes when the author is a machine

Goose was designed around a trade: more rules at compile time (which
array kind, where a value lives, no reference outliving its root, no
growable local across a recursive call) for speed and safety with no
annotations. Rust made a similar trade with its borrow checker. Both were
questioned on effort: does the programmer's time spent satisfying the
checker pay for itself? When the programmer is a language model, the
answer changes. A model iterates against a compiler at machine speed, and
a rejection with a precise message is cheap for it. What it costs instead
is attention from the person who reviews the result, and that person
cannot read every line at the speed the model writes them.

So the economics invert. Annotations that were "too much to ask" of a
human author are cheap to write, and the scarce resource becomes
*reviewability*: how much a reader can trust per minute of reading. Three
things follow for a language:

* **The unit of review is the claim, not the line.** A reviewer reads a
  signature and its claims ("cannot abort", "writes only `dst`", "returns
  an index of `xs`"), and the compiler's confirmation that each holds.
  The claims have to be in the source, next to the code, in the language's
  own terms, and checked on every compile, or they are comments.
* **Specifications become the new source of error.** A claim the model
  wrote can be wrong (too weak to matter, or stating the wrong property).
  So claims should be small, local to one function, and readable at a
  glance; a twenty-line postcondition is as hard to review as the code it
  describes. The claims that pay are the ones whose *meaning* is obvious
  and whose *truth* is hard to establish by eye.
* **The feedback loop must be deterministic and specific.** A model that
  iterates against a prover needs the same verdict every run and a
  message that names the missing fact. Goose's bounds-check pass has this
  character already: it is sound, incomplete, deterministic, and
  `checkfree` errors say which bound it could not show and which `assert`
  would settle it. A solver that times out, or proves something on Tuesday
  and not on Wednesday, is the wrong tool for this loop
  ([3.9](#39-what-the-llm-results-say) has the evidence).

Goose is well placed for this because its compiler is already a prover
of an unusual kind. The lifetime rules make every reference's root
static, every call target is known, functions are specialized per root,
effects are summarized over the call graph, and the bounds-check pass
tracks linear facts flow-sensitively through the whole program. The
features in this note mostly *expose* that machinery as claims, and add to
it only where the exposure finds a gap.

### 1.2 Three axes, weighted

* **Speed.** Goose already runs ahead of C++ and safe Rust on its
  benchmarks ([bench/summary.md](../../bench/summary.md)) and leaves
  little on the table for scalar CPU code. What remains is in three
  places: the C backend does not pass on what the checker knows about
  aliasing (a 3.3x loss on one kernel under MSVC), loops with runtime
  checks in them are side-exit loops the C compiler will not vectorize or
  reorder freely, and parallelism across cores is only available through
  share-nothing workers that copy their inputs. Each of these is a place
  where a *proof* the compiler can already make, or almost make, is worth
  a multiple. Explicit SIMD and GPU execution are real but narrower wins
  with a much larger surface; they come last.
* **Safety.** Goose is memory safe and race free already. The hole that is
  left is logical: a pool slot freed twice, never freed, or read through a
  stale handle gives the wrong value of the right type. It matters most
  for exactly the programs pools are for (editors, servers, games), and
  it is the one place where a reviewer of generated code has to reason
  about pairing by hand. There is a cheap library answer (generations) and
  a deeper language one (owning handles); this note sizes both.
* **Correctness.** This is where the head start is, and where the largest
  share of this note goes. The ladder `assert` → `checkfree` already shows
  the shape: a fact the programmer states once, a proof the compiler
  makes, a guarantee the programmer can demand. The questions are which
  other properties can join that ladder without an SMT solver, and how
  the claims become visible.

### 1.3 A proof is also an optimization

A claim the compiler verifies is also a fact the compiler holds, and in
Goose that has always been the point of proving: the bounds-check pass
exists to remove checks, `assert` exists to give it facts, index types
exist to move a check to where data enters so that a loop runs without
one. The note keeps this coupling deliberately. Every claim below comes
with what the optimizer gets from it:

| Claim | The reviewer learns | The compiler spends it on |
|---|---|---|
| `requires c` | the function's precondition, checked at every call | entry facts the body could not get from an opaque caller, or inside a recursive cycle; the check runs at call sites, where it is usually proven away |
| `ensures c` | what the result and the lengths satisfy on return | result facts the caller never had: `xs[find(xs, k)]` without a check; length facts that survive a call the effect summary would otherwise kill |
| `abortfree` | the function cannot fail | no side exits in loops: vectorization and speculation; C attributes; the precondition of `par for` and of GPU kernels |
| `pure`, `writes(…)` | what the function touches | `__attribute__((pure))`/`const` in C (common-subexpression elimination, hoisting), fewer kills in the caller, compile-time evaluation later |
| distinct roots | two arguments cannot alias | `restrict` in the generated C |
| termination | every loop and recursion is bounded | with `pure` and `abortfree`, the function is `const` in C's sense: calls can be deleted, merged and moved freely |
| `par for` | iterations are independent | the loop runs on all cores, and the program's meaning does not change |
| owning handles | every allocation is freed exactly once | a freed slot's references are dead, so a pool can shrink or compact where it cannot today |

## 2. What Goose already proves

This section is the inventory the rest builds on. Spec and implementation
references are in the [References](#references).

### 2.1 The prover

The bounds-check pass (spec §10.5, [implementation.md §5](../implementation.md#5-bounds-check-elimination))
is a flow-sensitive analysis over a **difference-constraint domain**: facts
of the form `l <= r + c` between *bases*, where a base is zero, an integer
variable, the length of a *place* (an array or slice location), or a
one-shot base for a value the operation itself bounds. A query asks for
the smallest provable `c`, answered by a shortest path over the fact
graph. Facts come from `if`, `while`, `guard`, `assert`, `match` arms, loop
headers, completed checks ("the program continues past `a[i]` only with
`0 <= i < len`"), completed `pop`s, exact length steps (`push`, `clear`,
`resize`), and the type of a value (a `u8` is below 256; a `u32 in a` is
below `len(a)`). Kills open a new *generation* of a base rather than
deleting facts, so what was known about an old length stays true of that
length, and a `push` bridges generations (`old <= new`) while a `pop` does
not. Loops get inferred invariants from a recording walk: a counter that
starts non-negative and only steps by constants keeps `i >= 0` and
`i <= len + c`; a `for` loop's step counters cannot gain on its index.

Across calls the pass is whole-program. Every call site records what it
proves about its arguments, and a specialization enters with the meet of
its sites once every site has been analyzed. Callees are summarized, not
assumed hostile: `ComputeEffects` computes for every specialization the
storage it may resize or overwrite (by parameter class, global or captured
local) and the integers it may write, to a fixpoint over the call graph,
and a call kills exactly what the summary names. Three things make this
unusually productive: a grow-only array shrinks only at operations the
pass sees, so a bound established before a `push` survives it; every
reference's root is static per specialization, so a call cannot reach
storage it has no path to; and an array has one type, so a growth through
a reference to one array leaves every array of another type alone.

The pass is required to be sound and allowed to be incomplete. Nothing
about a program's meaning depends on it, except whether a `checkfree`
function compiles, for which the spec fixes a core every implementation
must prove (spec §7.14). It is deterministic and runs in every build at
every optimization level, which is what lets `checkfree` be a language
rule rather than an optimization.

### 2.2 The claims

* **`assert(c)`** is a fact and a check. The check stays in every build;
  the fact feeds the prover from that point on. The idiom "state the
  lengths once at the top of a kernel" is how most hot loops become
  check-free today (implementation.md §9.1).
* **`checkfree fn`** demands that no bounds check, slice check, index-type
  conversion or pool `free(i)` check remains anywhere in the function's
  text after elimination, in every specialization, every `simd` version
  and every inlined copy. What it calls is judged where it runs inside it,
  so the guarantee is transitive. Its error names the check that remained and
  why, in the terms a programmer can act on: "nothing shows `u <
  dist.len`; `u` is a `u32 in nodes`, so `assert(dist.len >= nodes.len)`
  where neither changes after it would settle it". It changes nothing about
  what the program computes. The other aborts remain, and `assert` in
  particular is how a `checkfree` function states its preconditions once,
  outside its loops ([index_types.md §6](index_types.md#6-checkfree-functions)).
* **Index types** `uN in a` and `uN upto a` are refinement types of the
  narrowest useful kind: an integer below (or at most) the length of one
  named grow-only global array, which the type then pins against
  shrinking anywhere in the program. The invariant is established by one
  checked conversion and preserved by the type rules alone, with nothing
  for the prover to do except read the type (spec §3.11).
* **Lifetimes, writability, flatness, `const`** are claims of the type
  system that the checker proves per specialization with no annotation:
  no reference outlives its root, nothing writes through a read-only view,
  nothing but flat values crosses a queue. They are not the subject here
  except as the machinery the new claims reuse.

### 2.3 What it cannot state, and what it does not spend

The gaps that the designs in sections 3 to 5 close, in the order they
come up:

* **A call's result has no base.** A value returned by a call, like one
  read out of an array or a field, is an opaque one-shot value bounded
  only by its type. `xs[find(xs, k)]` keeps its check however `find` is
  written. Entry facts flow *into* a callee from its sites; nothing flows
  back out except the effect summary.
* **A callee in a recursive cycle gets no entry facts**, and neither does
  a thread entry or an `export fn`. Their preconditions have to be
  `assert`ed inside, which costs a check per call.
* **No disjunctions, no implications.** The domain cannot say
  `r == -1 || 0 <= r < n`. The usual Goose shape for "found or not" is a
  second `bool` result or a `T?`, so the fact that matters is conditional
  on a boolean, which the domain also cannot state. A small extension,
  facts guarded by a boolean variable, covers the idioms.
* **No facts about array contents.** `∀k: 0 <= adj[k] < n` is not a fact
  the domain holds; index types are the answer for what is stored, and an
  analysis of content invariants was built, measured and dropped for
  producing no speedup (implementation.md §5.12). This note does not
  reopen that.
* **The checker knows two references are distinct and tells no one.** Per
  specialization, two exact roots at different variables are different
  storage. The C backend does not emit `restrict` from that, and a C
  compiler that cannot see it leaves a loop scalar.
* **A check is a side exit.** A loop containing one has a path out that
  the C compiler must preserve in order, which blocks vectorization under
  MSVC outright and limits what clang may speculate. `checkfree` removes
  the bounds checks; division, capacity and `assert` remain.
* **Parallelism copies.** Workers are separate programs, so a kernel over
  a large array runs on one core unless the array is cut up and sent
  through queues by value.
* **Pool pairing is unchecked.** `alloc` and `free` are ordinary calls;
  the checker pairs nothing, and a stale reference to a freed slot is a
  valid reference to a valid value.

## 3. Correctness: a ladder of claims

### 3.1 Principles

1. **One prover.** Every claim is discharged by the difference-constraint
   pass, extended where this note says so, or by the checker's existing
   analyses (effects, roots, flow). No second engine. What the prover
   cannot show is a compile error in a claiming function and a runtime
   check elsewhere, exactly as `checkfree` already divides the world.
2. **The spec fixes a core.** As for `checkfree`, the spec lists what
   every implementation must prove for each claim, so a program written
   to the core compiles under every version of the pass. Beyond the core
   is implementation-defined, and a claim that stops compiling because a
   caller elsewhere changed is reported at that caller.
3. **Claims are read in the signature.** Preconditions, postconditions,
   effects and the abort-freedom of a function are written between its
   signature and its body, because that is what a reviewer reads first.
   `assert` stays a statement, since it is a fact about a point in the
   body.
4. **A claim changes nothing about what the program computes.** Like
   `checkfree`, `simd` and `inline`, the claims are checked and then
   erased; a program with its claims removed means the same thing. The one
   exception is a `requires` clause on a function whose callers the
   compiler cannot see (a thread entry, an `export fn`), which runs as a
   check at entry, as an `assert` there would.
5. **Gradual.** Nothing is required anywhere. A program with no claims
   compiles as today, and a function gains claims one at a time, in the
   order they pay.

### 3.2 Contracts in the fact language: `requires` and `ensures`

**What they are.** A `requires` clause is a proposition in the prover's
language that every call must establish; an `ensures` clause is one that
every return must. The language of propositions is deliberately the
domain's: comparisons (`<`, `<=`, `==`, `!=`, `>=`, `>`) between terms
the pass can read (integer parameters and results, `.len` of parameters
and of globals, constants, sums and differences with constants, the
products and quotients the pass already handles), joined by `&&`, plus
two forms the domain needs a small extension for: a boolean guard
(`found: idx < xs.len`, read as "where `found` holds, `idx < xs.len`") and
`distinct(a, b)` for two array or slice parameters
([4.1](#41-distinct-roots-as-restrict)). An index-type parameter already
carries its proposition in its type; `requires` is for relations between
things.

```goose
var nodes: Node[>..] = [];
type NodeId = u32 in nodes;

// Today: the assert is a fact for the body and a check inside every call.
checkfree fn sum_at(ids: NodeId[:], weights: i32[>..]&) -> i64 {
    assert(weights.len >= nodes.len);
    var t = 0;
    for id in ids { t += nodes[id] + weights[id]; }
    return t;
}

// Proposed: the same fact, stated in the signature. Each call site proves
// it or checks it; the body gets it for free.
checkfree fn sum_at(ids: NodeId[:], weights: i32[>..]&) -> i64
    requires weights.len >= nodes.len
{
    var t = 0;
    for id in ids { t += nodes[id] + weights[id]; }
    return t;
}
```

**`requires` at the call.** The clause is instantiated with the call's
arguments and judged in the caller's state, exactly as a conversion into
an index type is judged as the index check it is: where the caller's facts
prove it, nothing is emitted; where they do not, a check runs before the
call, aborting with the clause's text and the call's location. Inside a
`checkfree` or `abortfree` caller, an unproven `requires` is an error that
names the call and the clause ("the call at line 40 does not establish
`weights.len >= nodes.len`"), the diagnostic shape
[index_types.md §6.5](index_types.md#65-diagnostics) wanted and could not
give. Inside the callee, the clause is an entry fact from the first
statement on, however the function was reached: from an opaque site, from
inside a recursive cycle (which today contributes no facts to its callee),
through a function value. For a thread entry and an `export fn`, whose
callers the compiler never sees, the clause runs as a check at entry
instead. A `requires` is thus an `assert` whose check has moved to where
it can usually be proven, and whose text has moved to where a reviewer
reads it.

**`ensures` at the return.** The clause may name the result as `result`,
or several results by names the signature gives them (`-> idx: i64,
found: bool`, a placeholder; [section 8](#8-open-questions)), the
parameters, and lengths. Every
`return` is a point where the clause is judged in the callee's state; an
unproven one is a check there, or an error in an `abortfree` function.
The caller gets the clause as facts after the call: about the result's
one-shot base, and about the lengths it names, *which survive the call's
effect summary*. That second part matters as much as the first: today a
call to `fill(dst, n)` kills every fact about `dst.len` because the
summary says `fill` resizes `dst`; with `ensures dst.len == n`, the
caller's `for i in n { dst[i] }` is check-free again.

```goose
// The result is an index of xs, so xs[clamp(i, xs)] needs no check anywhere.
fn clamp(i: i64, xs: i64[:]) -> i64
    requires xs.len > 0
    ensures 0 <= result && result < xs.len
{
    if i < 0 { 0 } else if i >= xs.len { xs.len - 1 } else { i }
}

// A header parser: what is left of the input starts at result.
fn skip_header(b: u8[:]) -> i64
    ensures result <= b.len
{ ... }

fn body(b: u8[:]) -> u8[:] { b[skip_header(b)..] }    // no slice check

// A conditional fact: where found holds, idx indexes keys.
fn find_slot(keys: u64[:], k: u64) -> idx: i64, found: bool
    ensures found: 0 <= idx && idx < keys.len
{ ... }

fn bump(keys: u64[:], counts: i64[:], k: u64)
    requires counts.len == keys.len
{
    let i, found = find_slot(keys, k);
    if found { counts[i] += 1; }                       // no check: found releases the fact
}
```

**The boolean guard.** `found: P` is stored as a fact predicated on the
boolean's one-shot base; when control enters a region where that boolean
is known true (`if found`, `guard found else`, `assert(found)`), the
predicated facts join the state. This is the one addition the domain
needs for the idioms, and it is small: a predicated fact is a fact with
one extra base it waits on. Optionals need nothing: a `T?` result that
is narrowed is a `T&`, and the clause can speak about `result.len` after
narrowing if `ensures` is allowed to be conditional on `result` itself.

**What the clauses cannot say** is anything about contents, anything
quantified, anything about values the domain does not track (a `u64`
local, a float). This is a feature: the clauses stay the size of a line,
and a reviewer who knows the prover's vocabulary knows what each one
means. A function whose correctness needs more is a function whose
correctness is established by tests, as today, and
[3.7](#37-what-stays-out) says why the line is drawn here.

**Interaction with the rest of the language.**

* *Inlining.* An inlined callee's `requires` is judged at the site like a
  non-inlined one, and its `ensures` is granted to the caller at the
  inlined body's exit, so inlining does not change what is proven.
* *Function values.* A call through a function value is a call to each
  function it may be; each `requires` is judged, and the caller gets the
  meet of the `ensures`.
* *Recursion.* A cycle's functions get their `requires` as entry facts in
  every round, which is what the cycle lacks today; their `ensures` are
  granted to back edges as they are to any call.
* *Overloading.* Clauses take no part in resolution.
* *`return … from`.* A long-distance return bypasses the frames between;
  it lands at the target's destination and must satisfy the target's
  `ensures` at the point it is written, judged in the writer's state. An
  `ensures` that the writer cannot judge (it names the target's locals)
  is a check at the landing.
* *`defer`.* A `defer` body runs after the returned value is built; an
  `ensures` about a length the `defer` may change is judged after it.
* *Index types.* A `requires b.len >= a.len` is the standard companion of
  an `a`-indexed parameter used on `b`, and the `checkfree` core gains a
  clause: "an index of `a`'s index type into `b`, where `b.len >= a.len`
  is a `requires` of the function".

**Cost.** Parser and checker for the clauses, instantiation at call sites
and returns, emission of the fallback checks, the predicated-fact
extension, diagnostics, and spec text for the core and for every
interaction above. Index types were +685 lines in 19 files
([index_types.md §8.4](index_types.md#84-size)); this is of that order,
likely larger by half for the two-directional flow and the `return … from`
cases. The new runtime cost is nil where a clause is proven and one
comparison where it is not, which is what the `assert` cost today.

**Gain.** For the reviewer, the precondition is in the signature and the
compiler confirms every caller meets it. For the optimizer, result facts
and post-call length facts are new, and recursive cycles get entry facts
for the first time. For the LLM, the error at an unproven call names the
clause and the site, which is the kind of message that closes a loop in
one iteration.

### 3.3 `abortfree`: no runtime check of any kind

**What it is.** `abortfree fn` (the name is a placeholder; `proven` and a
rename of `checkfree` are discussed in [section 8](#8-open-questions)) is
`checkfree` extended to every abort of spec §9.3 that a proof can remove:

| Abort | Proof obligation | Status of the proof |
|---|---|---|
| index or slice out of bounds, index-type conversion | as `checkfree` | exists |
| `pop`/`pop_n`/`resize` below empty | `len >= 1`, `0 <= n <= len` at the call | exists (`Call::nobc`) |
| limited-array capacity overflow | `len < cap` at each `push`, `len + k <= cap` at `append` | the domain has `Place::cap`; judging the push is new, small |
| division or `%` by zero, `i64.min / -1` | divisor `!= 0`; or `>= 1` as `Binary::nonneg` already judges | partly exists; `!= 0` alone needs the strict-inequality handling conditions already get |
| `assert(c)` | `c` proven at that point | new: an `assert` in an `abortfree` function must be provable or it is an error; the fix is to lift it into `requires` |
| `abort(msg)` | unreachable | new: an error unless the prover shows the path dead, which it does for a branch whose condition it refutes |
| relative-reference offset overflow at store | the pool's length fits the width | hard in general; v1 excludes `T&<u8>`/`<u16>` links into arrays the prover cannot bound, and allows `<u32>` links into arrays with a static cap or a `u32 upto` bound |
| data-stack and native-stack exhaustion | none | excluded by definition, as native-stack exhaustion is excluded from everything today |
| debug-only overflow and conversion checks | none in release | excluded: the claim is about release semantics. A separate `nowrap` rung could later ask the prover to show no signed wrap, using its `NoWrap` reasoning, for arithmetic over `i64` sums and differences; multiplications would mostly fail and need a `requires` |

Everything the function calls must be `abortfree`, a builtin, or an
`extern fn` declared so (an external function is trusted, as its types
are). `print` and `str` are builtins that cannot fail short of stack
exhaustion, and `format` into a limited array is a capacity check like
any push. `qput` cannot abort; `qget` blocks and is excluded from
`abortfree` as a matter of taste rather than proof (a function that can
block forever is not one a reviewer wants under this word). The
guarantee: *while an `abortfree` function executes, the only way the
program can end is `exit`, a stack guard, or a contained worker's parent
being killed.*

```goose
let W = 2048;

abortfree fn blur(src: u8[>..]&, dst: u8[>..]&)
    requires src.len == W * W && dst.len == W * W
{
    for y in 1..W - 1 {
        for x in 1..W - 1 {
            let i = y * W + x;
            let s = src[i - W - 1] + src[i - W] as u16 * 2 + src[i - W + 1]
                  + src[i - 1] as u16 * 2 + src[i] as u16 * 4 + src[i + 1] as u16 * 2
                  + src[i + W - 1] + src[i + W] as u16 * 2 + src[i + W + 1];
            dst[i] = (s >> 4) as! u8;                  // >> 4: no division, no check
        }
    }
}
```

The only change from today's `checkfree` version in `bench/goose/blur.goose`
is that the length facts, which that version gets from its call sites,
are now the function's own contract, so a new caller that passes a wrong
size is reported at the call rather than making `blur` stop compiling.

**The abort set as an inferred effect.** The judgement behind the claim
is useful without the claim. The effect summary already says what a
function may resize or write; it can carry a *may-abort set* beside that
(bounds, division, capacity, `assert`, `abort`), computed per
specialization from the checks that survived and the callees' sets,
over the same fixpoint. `abortfree` is then the claim that the set is
empty, the certificate ([3.6](#36-the-certificate)) prints the set for
every function whether or not it claims anything, and a caller sees
what a callee may still abort on. Zig's error sets are the model for
inferring and subtyping such a set, minus Zig's blind spot that its
panics are not in it ([3.8](#38-prior-art)). A graded form,
`checkfree` for bounds and `abortfree` for everything, falls out of the
same data.

**Why this rung matters more than it looks.** A reviewer of generated
code wants exactly this sentence: "nothing in here can fail". It is
SPARK's Silver level, absence of run-time errors, which Altran makes its
default level at every safety integrity level and which Thales reached
in half a day to a day on its case studies; and SPARK's own history says
that at this level nearly every annotation can be inferred
([3.8](#38-prior-art)). Goose reduces the same level to a language whose
abort set is short and whose checks are already mostly proven. And it is the
precondition of the speed features: a loop body that cannot abort has no
side exit, so the C compiler may vectorize and speculate it
([4.2](#42-abort-free-loops)); a `par for` body that cannot abort has no
observable order of failure ([4.3](#43-par-for-shared-memory-iteration-proven-race-free));
a GPU kernel has no abort at all, so only an `abortfree` body can be one
([4.5](#45-the-same-bodies-on-a-gpu)).

**Cost.** The new judgements are small (capacity, divisor, `assert`,
dead `abort`), since each reuses facts the pass holds. The diagnostics
extend `KeptWhy` with the new kinds. The spec gains a core list for each
abort kind, like §7.14's. The library cost is real: `abortfree` is
transitive, so the standard library's small helpers (`min`, `max`,
`swap`, the `vec` operations, `sort`'s partition) need the claim before a
program can use them under it, as they needed `checkfree`. That is a
one-time pass over the library that also audits it.

**False rejections.** Code that is correct but not provable in the domain
is rejected and must be restructured or given a `requires`. The known
shapes (implementation.md §9.1's "kept" list) are indices loaded from
storage without an index type, `u64` counters, loop exits that are
disjunctions, and doubling counters. The `checkfree` corpus in the
repository already lives inside these limits; `abortfree` adds divisors,
which are usually constants or lengths already known positive, and
`assert`s, which move to `requires`.

### 3.4 Effects: `pure` and frames

**What exists.** `ComputeEffects` already knows, for every specialization,
what it may resize or overwrite and which integers it may write
(implementation.md §5.8), and the writability analysis knows what is
written through each reference. A function that reads and writes only
elements of its arguments has an effect summary that says so. What is
missing is a way to *declare* an effect and have the inferred one checked
against it, so that the declaration is a claim a reviewer can read and a
contract callers can rely on before the callee changes.

**`pure fn`.** The function is a function of its arguments: it writes
nothing outside its own locals and its result (no global, no pointee of
a reference parameter, no element of a slice parameter), reads no
mutable global (`let` and `const` globals are values and may be read),
calls only `pure` functions, builtins without effects, and `extern pure
fn`s (the declarer's assertion about the C function, as `math`'s
bindings would carry), and does not `print`, use a queue, spawn, or
`return … from`. This is D's definition less its "weak purity" (D lets a
`pure` function mutate through its parameters, [d-pure]); in Goose that
case is the frame below, since the kernels that write through a
parameter are exactly the ones a reviewer wants to see the frame of. The
known friction, in D and Nim alike, is debug output: both ended up with
a special `debug` print that purity ignores ([d-pure], [nim-effects]).
Goose should not; a `print` in a `pure` function is a rejection, and the
model removes it.

**Frames.** `reads(symbols)` and `writes(dst, counts)` list the globals
and parameters the function may read, and may write through or resize;
everything else is untouched, and the checker confirms the inferred
summary is a subset. A function with a frame and no `pure` is the common
kernel shape (`blur` writes `dst`, reads `src` and nothing else). SPARK's
`Global` aspect is the model, and its practice is the right one: for
code it can see, GNATprove "generates precise data and flow dependencies"
itself, the user prints them with `--flow-show-gg`, and a written aspect
is a pin against change rather than a proof ([spark-gg]). Goose is in the
same position Prusti is in for Rust, where ownership *is* the frame and
no frame annotation is ever needed ([prusti]); Dafny, by contrast, needs
`reads` and `modifies` on everything and gave methods `reads` clauses
only in 2023 ([dafny-reads]). So a frame in Goose is a checked
restatement of the summary, generated by `goose claims` and checked in
when the author wants it pinned ([3.6](#36-the-certificate)).

**What the compiler spends.** A `pure` function is emitted with
`__attribute__((const))` under gcc and clang, and a function whose frame
reads globals but writes nothing with `((pure))` (MSVC has no
equivalent; `__declspec(noalias)` is close for the frame case), so a call
in a loop whose arguments do not change is hoisted, and two calls with the
same arguments are merged. GCC's documentation of `const` excludes
"functions with infinite loops" ([gcc-attrs]), so the attribute strictly
needs termination too ([3.5](#35-termination)); until then `pure`, which
does not, is the safe emission for both. The checker's own kills already use the
inferred summary, so there is nothing new there; the declared frame
mostly makes the summary visible and stable. Later, `pure` is the
precondition for evaluating a call at compile time: Goose has an
in-process backend, so a `pure` call on constant arguments in a `const`
initializer or a `const for` count could be run by the compiler and
replaced by its result. That is a feature of its own and only noted here.

**Cost.** Small: the summaries exist, the check is a subset test, the
syntax is two clauses, the emission is an attribute. The subtlety is the
treatment of externs (a trust boundary that has to be declared, not
inferred) and of function values (a `pure` HOF is pure when its argument
is, so purity must be judged per instantiation, which is how the summary
already works).

### 3.5 Termination

**What is cheap.** A `for` over a range or a count terminates; a `for`
over an array terminates when the body does not grow the iterated array,
which `ForLoop::fixedlen` already decides (growth during iteration is
legal, implementation.md §3.15, and a loop that pushes onto what it walks
can run forever). A `while` loop terminates when the recording walk has found a
monotone counter that the condition bounds (`while i < n { …; i++; }`,
`while d > 0 { d--; …; }`, the heap sift-up whose index only moves down),
which it already finds for the bounds facts. `loop` and a `while` whose
condition the walk cannot relate to a monotone variable need a variant:
`decreases e` (placeholder), an integer term the prover must show
decreases on every path through the body and stays non-negative, which is
the same recording-walk machinery applied to a programmer-named term.
Recursion over an integer argument needs the same: `recursive fn f(n: i64)
decreases n`, with every recursive call's argument shown smaller and the
base case shown to be `n >= 0`-guarded.

**What is not.** Recursion over a structure linked through a pool
(`Node&<u32 in nodes>`) is not well-founded in anything the prover can
see: the links may form a cycle, and acyclicity is a content invariant.
Such a function can claim termination only with a fuel parameter
(`decreases depth`), which is a real change to the code and sometimes to
the algorithm. Walks that mark visited nodes terminate because the set of
unmarked nodes shrinks, which is again a content fact. This is an honest
limit of the one-prover principle; SPARK and Dafny have the same limit
for heap-linked structures and ask for a ghost measure.

**How much is automatic.** The C studies give the baseline for a
language whose loops are written with `for (;;)` and `while`: about a
quarter of loops are syntactically bounded (44% in WCET code), about a
third are counted `for` loops, and an abstract interpreter that
recognizes monotone counters and `A < B` conditions bounds 75% of 4,090
cBench loops and 93% of the Mälardalen WCET suite ([loop-patterns],
[loopus]). Goose starts far above that baseline because its `for` is
counted or ranges over an array by construction, which is SPARK's
situation too: "for-loops always terminate in Ada", so variants are
asked only of `while` and plain loops ([spark-variants]). Dafny guesses
`B - A` for a `while A < B` ([dafny-termination]), Lean infers measures
of the same shape and prints the one it found ([lean-termination]), and
Liquid Haskell's default metric alone proves 65.7% of recursive
functions terminating, the rest costing 1.7 annotation lines per hundred
([liquid-termination]); the certificate should print Goose's guess the
same way, so a reviewer reads the variant rather than trusting that one
exists. Every mature
system also has a loud escape hatch (`decreases *`, `partial`), and the
residue that needs one is the same everywhere: search loops, parsers,
fixpoints, `loop { … break }` state machines.

**What it buys.** For the reviewer, "`terminating`" on a parser or a
solver is a real claim. For the optimizer, the gain is only through the
combination: a function that is `pure`, `abortfree` and terminating is a
value of its arguments and nothing else, which is what C's `const`
attribute asserts and what lets a call be deleted when unused or moved
anywhere. Alone, termination buys little, since C compilers already
assume side-effect-free loops terminate.

**Cost.** The counted-loop and monotone-`while` cases are a classification
of what the recording walk already finds, so they are cheap. Variants are
a modest addition. The rule for recursion rides on §7.8's existing
cycle machinery. The spec text is the larger part. This rung comes after
the others because its gain per line is lower.

**Two more claims, considered and deferred.** A *data-stack bound*
(`datastack(N)`, placeholder) is unusually tractable in Goose: call
targets are static and recursion is restricted, which is exactly where
the commercial stack analyzers lose precision (GNATstack is
"conservative" about recursion, `cargo-call-stack` breaks on indirect
calls, [gnatstack], [cargo-call-stack]), and a function's frame on each
data stack is a sum of sizes the prover can often bound. But sizes are
usually data-dependent, so the claim would be rejected for most code and
should wait for an entry point that needs a budget; the native C stack
cannot be claimed in the language at all. *Constant time* is not a claim
a language that emits C can make: Jasmin and FaCT get it from a
secret/public type system plus a backend they control, and Rust's
`subtle` and Zig's `timingSafeEql` are documented as best effort against
the optimizer ([jasmin-ct], [subtle]). A *no floating point* claim
(Ada's `No_Floating_Point`, [ada-restrictions]) is trivial to check and
occasionally wanted for determinism across C compilers; it is a one-bit
flag in the summary if anyone asks.

### 3.6 The certificate

The pass records why every kept check was kept (`KeptWhy`). Recording why
every *removed* check was removed is symmetric: by which fact, from which
statement or call site, through which generation. With that, a report can
say, per function:

```
blur (bench/goose/blur.goose:19): abortfree, requires src.len == W * W && dst.len == W * W
  18 index checks removed: 18 by the requires clause and the bounds of y and x
  may abort: nothing
  effects: writes dst elements; reads src elements; no globals
  distinct(src, dst): proven at both call sites in main (exact roots a, b)
bfs (test/optimizer/checkfree_bfs.goose:46): checkfree
  7 checks removed: 4 by index type NodeId, 2 by the assert at line 47, 1 by the while condition
  0 checks kept
  may abort: assert at line 47; dist.len >= nodes.len would be a requires
  effects: writes dist elements, resizes q; reads nodes, edges
```

The shape above is a sketch; the point is that it is produced from data
the pass already has, it is per function, and it names source lines. The
same data is a code lens in the language server ("abortfree ✓ · 18 checks
proven · pure"), which is how a reviewer in an editor meets a claim, and
how a model learns which claims it can make. `--bce-lines` is the ancestor
of this and stays as the per-line counting view.

For the review workflow the certificate is what gets pasted into a pull
request: the model writes the function with its claims, runs `goose
claims`, and the reviewer reads the claims and the compiler's confirmation
rather than re-deriving the bounds by hand. The certificate is cheap
precisely because the claims are checked by the compiler on every build;
it is a report, not a proof artifact that can go stale.

**Cost.** Small: a symmetric record in the pass, a printer, a code-lens
provider over data the server already has from checking. It is the first
thing to build because every later claim shows up in it.

### 3.7 What stays out

* **An SMT solver.** It would let clauses say more (contents, quantifiers,
  nonlinear arithmetic) at the price of the properties
  [1.1](#11-what-changes-when-the-author-is-a-machine) asks for:
  determinism, bounded time, and an error that names a fact. The
  verification languages that use one report instability and timeouts as
  their main pain ([3.8](#38-prior-art)); Goose's prover is weak, fast and
  predictable, and `checkfree`'s spec-fixed core shows how to make a weak
  prover a contract rather than a surprise.
* **Facts about contents.** Index types are the Goose answer: a content
  fact is turned into a type that is checked where data is stored. The
  content-invariant analysis that was tried gave no speedup, and a reviewer
  cannot check a quantified invariant by eye any better than the code.
* **Ghost code and proof blocks.** Nothing in the clauses needs a
  computation that is not in the program. The moment a claim needs a
  lemma, it is the wrong claim for this language.
* **A separate specification language.** Clauses are Goose expressions
  of a restricted shape, parsed by the parser and typed by the
  typechecker. There is no second syntax to learn or to translate.
* **General refinement types.** `u32 in a` is a refinement type with one
  predicate shape, chosen because its invariant is preserved by the type
  rules alone. A general `i64 where P` would need the prover at every
  store, including through generic code, and would push toward the solver.
  If a second predicate shape earns its place (a non-negative integer, a
  non-zero divisor), it should be added as another closed form, not as a
  general mechanism.
* **Tests and bounded checking are a complement, not a rung.** What the
  prover cannot discharge is what tests are for, and Goose's in-process
  backend makes a `goose test` that runs property-style checks cheap.
  Because Goose emits C, a kept check can also be handed to an unmodified
  bounded model checker (CBMC, ESBMC) over the generated C, which either
  produces a concrete input that reaches the abort, a counterexample the
  reviewer can run, or shows the check unreachable within a loop bound
  ([3.8](#38-prior-art)). Both are tooling outside this note's scope, and
  neither changes what the compiler claims: a bounded result is not a
  proof, and the certificate must say so.
* **No justification pragma.** SPARK lets a user mark an unproved check
  as a false positive with a reason; Dafny and Verus have `assume`. The
  evidence in [3.9](#39-what-the-llm-results-say) says a model will use
  any such hatch. In Goose the honest escape is to not make the claim, or
  to lift the fact into a `requires` that the callers then discharge or
  check.

### 3.8 Prior art

The prior art splits into the systems that prove absence of runtime
errors, the contract and refinement systems, the effect systems, and the
termination checkers. Each paragraph ends with what transfers.

**Absence of runtime errors as a checked claim.** The closest relative of
`checkfree` is Google's Wuffs, a language for decoding untrusted file
formats whose guarantee is that it "is safe with respect to buffer
overflows, integer arithmetic overflows and null pointer dereferences"
and that "all such checks are done at compile time, not at run time"
([wuffs]). Its machinery is Goose's in miniature: *facts* are boolean
expressions true at a program point, created by assignment, by `if` and
`while` conditions and by `assert`, intersected at join points; an
`assert` "introduce[s] new compile-time facts", costs nothing at runtime
and fails compilation if unprovable; the checker is "a proof checker, not
an SMT solver or automated theorem prover", deliberately "fast (and dumb)
instead of smart (and slow)"; refinement types `base.u8[..= 99]` carry
constant bounds only; a non-obvious step names its axiom
(`assert n_bits < 12 via "a < b: a < c; c <= b"(c: width)`); arithmetic
overflow is rejected unless proven impossible, with `~mod+` and `~sat+`
as the explicit escape operators ([wuffs-facts], [wuffs-assert],
[wuffs-bounds]). Its decoders run at 1.2-2.75x libpng and 2-6x giflib
([wuffs-png]). The difference from Goose is where the work falls: Wuffs has the
programmer write a loop's `pre`, `post` and `inv` facts by hand, since
"This involves more explicit work by the programmer, but compile times
matter", where Goose infers the loop invariants and keeps a runtime
check for what it cannot prove. The two
designs agree on the thing that matters for machine authors: `assert` is
never an assumption. Rust's decade of trying to add "cannot panic" as a
checked property is the counter-example: the `no_panic` crate works by
link failure and depends on the optimization level ([no-panic]), three
language proposals went nowhere ("pretty much everything in Rust can
panic", [rust-no-panic-2015]; "You've described a solution, not a
problem.", [rust-no-panic-2025]), and what shipped in 2025 is
`panic = "immediate-abort"`, which shrinks the cost of a panic rather
than proving its absence ([rust-immediate-abort]). The lesson is that a
no-abort claim has to be a judgement of the compiler's own check
elimination, not a property recovered from the optimizer's output. Ada's
`pragma Restrictions` (`No_Exception_Propagation`, `No_Recursion`,
Ravenscar and Jorvik) is the mature *profile* approach, partition-wide
switches that remove checks rather than prove them unnecessary; SPARK's
proof is what discharges them ([ada-restrictions]). Frama-C's RTE plugin
turns every potential runtime error in C into an ACSL assertion for WP
to prove ([frama-c-rte]). *Transfers:* `abortfree` is Wuffs's guarantee
over Goose's smaller abort set, with Goose's inferred invariants in place
of hand-written loop facts; the Rust history is the argument for the
spec-fixed core.

**SPARK's ladder, and what Silver costs.** SPARK names five levels:
"Stone level - valid SPARK", "Bronze level - initialization and correct
data flow", "Silver level - absence of run-time errors (AoRTE)", "Gold
level - proof of key properties", "Platinum level - full functional
correctness" ([spark-avocs], [spark-guidance]). In Goose's terms, Bronze
is what the effect summaries and lifetime rules already certify for every
program, Silver is `abortfree`, and Gold is what `requires` and `ensures`
add. The industrial record on Silver is the most relevant evidence in
this note. Altran's policy table makes Silver the default level at every
safety integrity level ([spark-hcss]); Thales reached it in "half a day"
on a code-generator runtime and in under a person-day on a 7 kloc
command-and-control unit ([spark-hcss], [spark-avocs]); the user's guide
puts automation at "Typically, 95% to 98% of run-time checks can be
proved automatically" ([spark-usage]); and NVIDIA's security team, which
moved two C applications to SPARK in about three months, reported "I did
not see any performance difference at all" ([spark-nvidia]). The
sharpest number is Moy's comparison of the same program, SPARKSkein,
under SPARK 2005 and SPARK 2014: the Silver-level annotations fell from
41 effect contracts, 31 pre- and postconditions, 43 loop-invariant
conditions and 23 anti-explosion annotations to 1, 0, 1 and 0, because
"effects and dependencies are generated", "internal subprograms are
inlined" and "loop frame conditions are generated" ([spark-hcss]). That
is the claim of this note from another direction: at the
absence-of-runtime-errors level, nearly every annotation is inferable,
and Goose's pass already infers the two things SPARK had to add
(effects, loop frames). Where SPARK's costs remain, they are the solver's:
"The initial pass may require a substantial effort to resolve all false
alarms", analysis takes "up to a few hours, on large programs", and the
limits show "on non-linear integer arithmetic (such as division and
modulo)" ([spark-guidance]); iFACTS, 250 kloc with contracts at 8% of
lines, re-proves its 152,927 verification conditions nightly in about
three hours ([spark-keynote]). An AdaCore experiment in 2026 had a model
write Platinum-level cryptography: 4,085 checks, "23 loop invariants, 83
assertions, and 87 lemma calls", and 43 of the 52 hours spent "waiting
on tools" ([spark-ai]), which is the cycle-time point of
[3.9](#39-what-the-llm-results-say) in another form.

**Contracts with SMT behind them.** Dafny proves absence of runtime errors
and termination by default and needs a user-written invariant for
anything a loop does, since it "will not know any but the most basic
properties of a loop" ([dafny-guide]); its own blog names the pain,
proofs that break after "a Dafny upgrade, an unrelated definition, or a
renamed variable", and the cures (`opaque`, smaller lemmas, resource
limits) ([dafny-brittle]). The ratios are the cost: IronFleet's "ratio of
proof annotation to executable code is 3.6 to 1" at the implementation
layer ([ironfleet]); Verus's SOSP 2024 systems carried 31K lines of proof
for 6.1K of code and chose "as few triggers as possible" because too
many instantiations time the solver out ([verus-sosp]); seL4 maintains
over a million lines of proof ([sel4-whitepaper]). Mariposa measured the
instability directly over 17,043 queries from six projects: the newest
solver "is unstable on 2.6% of the queries", up to 5% in one project,
and "SMT solver upgrades often make projects less stable" ([mariposa]).
The same Verus paper notes that systems with a restricted logic reach
"remarkably low proof-to-code ratios, e.g., Hyperkernel reports 0.03:1"
([verus-sosp]), which is the trade this note makes. Creusot and Prusti
prove panic-freedom of Rust with `requires`/`ensures`/`invariant`/
`variant` annotations and prophecies or permissions for `&mut`
([creusot], [prusti]); Aeneas drops annotations and extracts a pure model
to prove in Lean ([aeneas]); Frama-C's WP and Why3 front many provers and
record per-prover sessions to replay for stability ([frama-c-wp],
[why3]). *Transfers:* the clause vocabulary (`requires`, `ensures`,
variants) and the default obligations (no runtime error, termination);
not the solver, the triggers, the lemmas, the replay sessions or the
heap framing, each of which solves a problem Goose's model does not have.

**Refinement types with inference: the closest relatives.** Flux refines
Rust types with indices and predicates, `i32[n]`, `RVec<T>[n]`,
`i32{v: x < v}`, strong references whose post-state an `ensures` gives,
and solves quantifier-free Horn constraints from "a small set of
quantifier-free templates" ([flux]). Against Prusti on the same 944 lines,
liquid typing cut "annotation overhead from up to 24% of code size
(average 9%) to nothing at all", halved the specification lines (145
against 315) and cut "verification time by an order of magnitude" (7 s
against 247 s in its evaluation, with an FFT at 0.7 s against nearly
three minutes). The invariants Prusti needed
were "simple inequalities or tedious bookkeeping" such as
`px.len() == n + 1`, which are exactly the facts a difference-constraint
pass carries. Liquid Haskell's real-world study found specifications of
"around two lines to express" with hints at "3.5% of LOC" ([liquid-rw]),
and its termination checker proved "96% of all recursive functions
terminating" with "1.7 lines of termination-annotations per 100 lines of
code", the default metric alone covering 65.7% ([liquid-termination]).
Whiley verified `str[i]` under `for i in 0..|str|` with no annotation but
still needed `i >= 0` written for a counting `while`, and its authors
aim to "automatically synthesize simple loop invariants" ([whiley]).
*Transfers:* this is the family `u32 in a` belongs to, and the direction
of growth is Flux's: lengths and indices as the refinement vocabulary,
`ensures` on lengths for the calls that change them, invariants
inferred, and polymorphism (Goose's per-root specialization) in place of
quantifiers.

**Lean as a programming language.** Lean 4 is "a real functional language
that compiles to native code" through C, with reference counting and
destructive updates ([lean-about], [lean-beans]); its termination
machinery tries structural recursion, then well-founded recursion with
guessed measures of the form `e₂ - e₁` from a guard `e₁ < e₂`, and
prints "Could not find a decreasing measure." with a per-call table when
it fails; `decreasing_by` goals are closed by `omega`, a decision
procedure for linear integer arithmetic of which Goose's difference
domain is a strict subset ([lean-termination], [lean-omega]). Production
Lean (Cedar, SampCert at AWS) is verified library code called through an
FFI ([sampcert]). *Transfers:* the guessed-measure rule for
[3.5](#35-termination), and the idea of printing the inferred measure.
Dependent types, tactics and kernel checking are out of scope for a
language whose author is a model and whose reviewer reads signatures.

**Bounded model checking and abstract interpretation.** Kani needs "no
user annotation" to check overflow, division by zero and out-of-bounds
indexing in Rust, within a loop bound: the Rust standard library campaign
auto-generated 16,748 harnesses of which 11,970 verified, found eleven
bugs that tests and fuzzing missed (one in s2n-quic that "16M fuzz
iterations" had not found, in about twenty seconds), and states that
"Harnesses without loop contracts provide bounded guarantees only"
([kani], [kani-std]). CBMC's documentation is blunt that unwinding is
"unsound" unless the bound is complete ([cbmc]). Astrée reached zero
false alarms on 132 kloc of flight software after specialization, from
1,200 on the first run ([astree], [astree-erts]), and Facebook's Infer
found that alarms reported in batch had a fix rate "near zero" while the
same alarms at diff time "rocketed to over 70%" ([infer]). *Transfers:*
two cheap tools and one policy. A kept check that `abortfree` reports
can be handed, in the generated C, to an unmodified CBMC or ESBMC to
produce a concrete input that aborts, or to show the check unreachable
within a bound, which turns "a check remains" into something a reviewer
can run ([3.7](#37-what-stays-out)). Astrée's history says precision
comes from specializing the domain to the language, which is what the
difference-constraint pass is. And Infer's lesson is the one for the
certificate: report the unproved fact at the edit, never as a batch.

**Effect systems and purity.** The systems that are pleasant to use infer
the property and let an annotation be a checked restatement: Nim infers
side effects, raised exceptions and tags for every routine and lets
`func` or `{.noSideEffect.}` pin them, with its one unsoundness in
callbacks, "Every indirect call via some proc type T is assumed to raise
system.Exception", fixed later by `strictEffects` ([nim-effects]); Koka
infers row-polymorphic effects where `total` "signifies the absence of
any effect" and divergence is itself an effect ([koka]); Flix "precisely
tracks the purity of every expression" and lets a region-scoped mutation
stay inside a pure function ([flix]). The systems that require the
annotation everywhere produce complaints: D infers attributes only for
templates and nested functions because other bodies may be absent, and
its users talk of "attribute soup" ([d-pure], [d-inference]); Fortran's
`PURE` is annotation-only and exists so that calls are safe in
`DO CONCURRENT` ([fortran-pure]); Rust's `const fn` grew its allowed
subset release by release ([rust-const-fn]). GCC's `pure` and `const`
attributes are unchecked promises ([gcc-attrs]), and OCaml 5 chose
effect handlers with no effect typing at all ([ocaml-effects]). The frame
problem is the same story: Dafny needs `reads` and `modifies` on
everything, SPARK generates `Global` and `Depends` and lets the user pin
them ([dafny-reads], [spark-gg]), and Prusti needs no frames because
Rust's ownership is one ([prusti]). *Transfers:* infer, print, let the
annotation pin; Goose's whole-program compiler has no absent bodies and
no indirect calls, so none of the reasons for annotation-only purity
apply.

**Termination.** Idris 2's `total` requires an argument that "has
decreased" along every recursive chain, Agda checks structural recursion
with a configurable depth, Lean tries structural recursion and "attempts
to infer well-founded recursion" with guessed lexicographic measures,
Dafny guesses `decreases` clauses, Koka makes divergence an effect, SPARK
asks for `Loop_Variant` on `while` loops only, and ACL2 needs a measure
under a well-founded relation ([idris-total], [agda-termination],
[lean-termination], [dafny-termination], [koka], [spark-variants],
[acl2-defun]). Every one of them has an escape hatch (`partial`,
`decreases *`, `TERMINATING`, program mode). The numbers on how much is
automatic are in [3.5](#35-termination). *Transfers:* guess, print the
guess, require a variant for the residue, and refuse recursion over pool
links honestly.

**Resource bounds.** Stack bounds are solved commercially at the binary
level (AbsInt's StackAnalyzer) or from compiler-emitted frame sizes plus
a conservative call graph (GNATstack, `cargo-call-stack`), and every such
tool loses precision on indirect calls and recursion ([stackanalyzer],
[gnatstack], [cargo-call-stack]); Zig's proposal to eliminate stack
overflow has been open since 2018 ([zig-stack]). seL4, Ravenscar and
MISRA converge on "allocate at start-up, then prove nothing grows"
([sel4-faq], [misra-21-3]). *Transfers:* Goose already has the shape
those tools want (static targets, restricted recursion, no heap); a
data-stack bound is possible and deferred ([3.5](#35-termination)).

### 3.9 What the LLM results say

The literature on language models writing verified code is three years
old and already large. Its findings, with numbers, and what each one says
about the designs above.

**Auto-active provers work for models; tactic provers mostly do not.**
Every aligned comparison puts an SMT-backed, annotation-driven verifier
far ahead of an interactive one. AlgoVeri (2026) gives the same 77
algorithms to one model in three systems: 40% verified in Dafny, 25% in
Verus, 8% in Lean, and says Dafny lets models "focus on logical
correctness" while the others "trap models in persistent syntactic and
semantic barriers" ([algoveri]). Vericoding (2025; 12,504 specifications)
reports 82%, 44% and 27% on Dafny, Verus and Lean with off-the-shelf
models, and finds that adding natural-language descriptions "does not
significantly improve performance" ([vericoding]); CLEVER's end-to-end
Lean figure is 0.6% ([clever]).
Within one ecosystem the trend is steep: DafnyBench went from 68% (June
2024) to 89% single-model and 96% model-union in fifteen months
([dafnybench], [vericoding]); on 849 tasks drawn from real Verus systems,
a hands-off agent reached 81% in late 2025 where the 2024 tool had 20%
([verusage]). Success falls with program size and hint count everywhere
it is measured (DafnyBench; VeruSAGE's correlation of −0.3 to −0.5 with
task size). *For Goose:* a prover that discharges small, local, arithmetic
obligations automatically, with no proof mode at all, is the shape that
works; the function is the right unit; the clauses should stay one line.

**The failure that does not improve is the specification.** Spec
synthesis is now the bottleneck: on Verina, models write correct code
73% of the time and correct specifications 52% ([verina]); VeriBench's
best agent covers at most 11% of the reference theorems ([veribench]);
22.7% of verifier-*accepted* JML specifications had no postcondition at
all ([spec-harness]). Postcondition-only contracts were unsound 76-82%
of the time for want of a precondition, and asking for both lifted
soundness from 13% to 81% ([nl2contract]). *For Goose:* claims must be
readable by the reviewer at a glance, since the reviewer is the only
oracle for whether the claim is the right one; `requires` and `ensures`
come as a pair; and a claim's vocabulary should be so small that a wrong
claim looks wrong.

**Models cheat the checker wherever it has an escape hatch.** Within a
few iterations of reinforcement, models learned `assume(false)`,
`ensures true`, `sorry` and external axioms ([alphaverus], [vericoding]);
a cheat checker together with library search let three models prove 9%,
24% and 26% more of VeruSAGE's tasks ([verusage]); the SPARK study found that when checks were weak "the agent
tried to bypass them" ([prover-judge]). *For Goose:* this is the strongest
argument for the design already in place. `assert` is never an
assumption: it is a fact *and* a check, so a model cannot introduce a
fact without paying for it at runtime, and inside `abortfree` it must be
proven. `checkfree` is a claim about the compiler's own elimination, not a
user-written specification, so there is nothing to weaken. Keep it that
way: no `assume`, no trusted clause, no `unsafe`. A lint for a trivially
true `assert` and for an `ensures` that says nothing closes the last gap.

**The error message is the feedback signal.** "Invariant not satisfied"
and a raw solver log are not actionable; source-level counterexamples
took one Verus tool from 65% to 82% on hard tasks and cut cost fourfold
([exverus]); LORIS reaches 97% by pointing at the invalid step in the
model's own reasoning ([loris]); repair conditioned on the error message
is what made Baldur work ([baldur]). *For Goose:* the `checkfree` error
already names the bound that failed and the `assert` that would settle
it. The certificate ([3.6](#36-the-certificate)) should add the facts the
pass *had* at that point, and a satisfying assignment where one exists
(`i = len` fails `i < len`), in a machine-readable form.

**Determinism and versioning matter more than power.** A Verus release
that "introduced a breaking requirement for explicit decreases clauses"
cut an earlier proof tool's verified tasks by 23.9% between two releases
of the same year ([kverus]); four of 150 benchmark tasks stopped verifying
between Verus versions ([exverus]); Frama-C rejected ten invariants that
were correct ([loopy]). *For Goose:* the difference-constraint pass has no timeouts
and no search order, and the spec fixes what every implementation must
prove. Keep both, and treat a change to the fact language as a breaking
change.

**Fast cycles beat clever prompting.** AutoVerus solved half its tasks
in under 30 seconds or three calls ([autoverus]); agents ran the verifier
six to eleven times per task ([verusage]); enforcing well-typedness
*during* generation halved compile errors ([type-constrained]). *For
Goose:* compile latency and a machine-readable diagnostic format are
features; a check-only-this-function mode would pay for itself.

**Strictness costs first-try success through unfamiliarity, not through
the checker.** Across 24 models and twelve languages, Python scores 0.48
and Rust 0.33-0.39 at pass@1, but the gap closes for the strongest
reasoning model (71% against 70%) ([multi-lcb]); language models fix
about three quarters of real `rustc` errors by iteration
([rustassistant]); Verus trails Dafny largely on the maturity of its
corpus ([vericoding]). *For Goose:* a young language competes on its
corpus and its documentation; bundling a set of idiomatic verified
examples and a one-page semantics of the fact language is part of the
feature.

**Simple types and arithmetic verify; heap and aliasing reasoning do
not.** F*'s simply-typed tasks verified far more often than its
dependently-typed ones ([fstar-synth]); AlgoVeri reports Verus
performance that "collapses under the systems-level memory constraints"
([algoveri]). *For Goose:* the no-heap, no-sharing model removes the
reasoning that dominates Rust and Verus failures. Keeping bounds, ranges
and lengths as the only obligations is a strength, not a limitation.

**Verified is not correct, and reviewers know it.** The Anthropic and
Theorem work that proved absence of crashes for 123 libraries states that
"formally verified" does not mean "bug-free", that a checker cannot tell
whether the specification matches reality, and lists its trusted base
explicitly ([anthropic-fv]). The SPARK study that discharged 49,280
GNATprove obligations with agents at 20-40x lower supervision cost still
had defects escape the prover and be caught by known-answer tests
([prover-judge]). Surveys put reviewer trust in generated code low (96%
of developers do not fully trust it, 38% say it takes more effort to
review, [sonar]) and describe reviewing agent changes as "a
trust-calibration problem rather than a diffing problem", wanting risk
signals per line and file ([trust-calibrated]). Kleppmann's argument
that "the challenge will move to correctly defining the specification"
([kleppmann]) is the same point from the other side. *For Goose:* the
certificate is the artifact a reviewer calibrates on: per function, what
is claimed, what was proven, by which facts, and what runtime checks
remain. Tests stay beside proofs. And the trusted base should be stated:
the checker, the C compiler, the externs the program declared `pure` or
`abortfree`.

## 4. Speed: proofs the optimizer can spend

### 4.1 Distinct roots as `restrict`

**The evidence.** `bench/goose/blur.goose` is a nine-tap stencil with
every check proven. Under clang it runs in 256-266 ms and is 1.10x faster
than Rust; under MSVC the same C takes 930-952 ms, 3.3-3.6x slower,
because MSVC "reports a possible loop-carried dependence between the loads
through `src` and the stores through `dst`" and leaves the loop scalar
([bench/notes.md](../../bench/notes.md)). The C++ row of the same kernel
written with `__restrict` narrows that clang-to-MSVC gap to 1.83x, so the
qualifier is worth about half of it; the rest is MSVC's vectorizer. The
row-slice form of the Goose kernel vectorizes under both compilers,
because the slices are locals with fresh bases.

**What the checker knows.** `blur(a, b)` in `main` is one specialization
in which `src` is rooted exactly at `a` and `dst` exactly at `b`, two
different variables, hence two different arrays (an exact root is a claim
about storage identity, implementation.md §3.4). The bounds-check pass
uses this already (`HandedApart`, `DistinctTypes`). Emitting `restrict`
on the two parameters of that specialization is sound in C's sense: no
object accessed through one is accessed through the other during the
call. Where a parameter's root is inexact, or two parameters may share a
root, nothing is emitted. Parameters of different array *types* are
distinct by Goose's own argument and can be marked without root
reasoning.

**As a claim.** `requires distinct(src, dst)` makes the same fact part of
the contract, which matters for callers the specialization does not
settle: two slices of one array passed as `src` and `dst` have the same
root, and the call then checks that the ranges do not overlap (two
comparisons) or, in an `abortfree` caller, is rejected. With the clause,
the `restrict` is unconditional for the function, and a reviewer sees in
the signature that the kernel is not an in-place one.

**Cost and gain.** Codegen only for the inferred case: a few dozen lines
to consult roots at parameter emission, plus the `GS_RESTRICT` macro per
compiler. The clause form is part of [3.2](#32-contracts-in-the-fact-language-requires-and-ensures).
The gain is the whole MSVC gap on this kernel class, and likely some of
the `particles` and `scene` losses on the same backend; measure with the
existing `bench/bce_ab.py` A/B harness. Nothing changes under clang,
whose alias analysis already separated the two.

### 4.2 Abort-free loops

A runtime check in a loop is a conditional branch to a `noreturn` call.
For the C compiler it is a side exit whose order relative to the other
iterations' effects must be preserved, so stores cannot be sunk past it,
loads cannot be hoisted over it freely, and a vectorizer must either
prove it cannot fire (clang sometimes does) or give up (MSVC does: nine
kept checks made `blur` 1.75x slower). `checkfree` is the Goose answer for
the checks that dominate. `abortfree` extends it to the remaining aborts
in the body, and the gain is the same kind in two places: loops with a
division or a `pop` in them, and loops in which an `assert` was placed
inside rather than hoisted. It also enables Goose's own codegen to
speculate: the `&&`/`||` probing (`Binary::specidx`) evaluates an operand
unconditionally only when every check in it is proven; with `abortfree`
that is every operand of the function.

Honesty about the size of the gain: on clang the checks that remain are
well-predicted branches and the measured cost of a kept check is usually
small (index_types.md §10: 0-9% on a gather, within noise on a BFS). The
large wins are on MSVC and in loops that a check stops from vectorizing
at all. `abortfree` is worth having for the claim; the speed is a
byproduct the compiler collects where it can.

### 4.3 `par for`: shared-memory iteration, proven race-free

**The gap.** Goose's threads share nothing (spec §11.2): a worker is a
separate program with its own stacks and globals, fed by typed queues of
flat values. This is why the language needs no locks and has no data
races, and it is the right shape for servers and pipelines. It is the
wrong shape for a kernel over one large array, where the work is a loop
whose iterations are independent and the data is already in place: cutting
the array into flat chunks, queueing them to workers and queueing the
results back copies everything twice and leaves the stable references to
the array behind. `samples/23_mandelbrot_threads.goose` is the shape today.

**The proposal.** `par for i in n { body }` (placeholder syntax) is a
`for` loop whose iterations the compiler may run on several threads of the
*same* program, over the same memory, when it can prove that doing so is
unobservable. Like `simd`, the construct has no meaning of its own: the
program computes exactly what the sequential loop computes, so an
implementation may run it sequentially (the TinyCC backend would), and a
program's output never depends on how many threads ran. The compiler
accepts the loop only when:

1. **The body is `abortfree`.** An abort would otherwise be an observable
   event whose iteration, message and timing depend on the schedule. With
   no abort possible, iterations have no observable effect except their
   writes.
2. **The body grows nothing and declares no non-fixed local.** Threads of
   one program cannot share its data stacks; a body whose locals are all
   fixed-size needs only native stack. This is the recursion rule and the
   coroutine rule in a third place: "no non-fixed value at the point where
   the stack discipline would break" (spec §7.8,
   [coroutines.md §2.2](coroutines.md#22-the-recursion-rule-is-the-coroutine-rule)).
3. **Every write is to an element, and no two iterations write or
   read-and-write the same element.** The body's effect summary must name
   element writes only: no global written, no `push`, no whole-value
   assignment to anything outside the body's locals, no `qput`, no
   `print`, no `extern` call unless `pure`. Then the prover shows
   disjointness: for the loop variable at two symbolic iterations `i1 <
   i2`, every pair of a write place in one and a write or read place in
   the other is disjoint. The domain can state this for the shapes
   kernels use: `out[i]` trivially; `out[i * 4 + b]` for `b in 0..4` by
   the `Lin` bases; `dst[y * W + x]` for `x in 1..W-1` by `Lin` plus
   offsets, since `y1 + 1 <= y2` gives `y1 * W + W - 1 < y2 * W + 1`. This
   is the "two-iteration check" that
   [analysis_inventory.md §5](analysis_inventory.md#6-loops-two-iteration-check-instead-of-name-scans-small-effort-independent)
   already proposes for another purpose, with disjointness as the
   question. Reads of arrays the loop never writes need nothing. Arrays
   reached through two parameters need `distinct` ([4.1](#41-distinct-roots-as-restrict)),
   or the writes would have to be proven disjoint across parameters too.
4. **Reductions** (`total += f(i)`) are a second accepted shape: a local
   integer or float accumulated by one associative operation. Integers
   are exact in any order; floats are not, and a float reduction in a
   `par for` would make the result depend on the schedule, so v1 either
   excludes float reductions or defines a fixed tree order with a fixed
   chunk count, which is deterministic but differs from the sequential
   sum. The honest v1 is integers only, with floats reduced by the program
   into a per-iteration output element.

```goose
let W = 2048;

abortfree fn blur(src: u8[>..]&, dst: u8[>..]&)
    requires src.len == W * W && dst.len == W * W && distinct(src, dst)
{
    par for y in 1..W - 1 {                 // iteration y writes dst[y*W+1 .. y*W+W-1]
        for x in 1..W - 1 {
            let i = y * W + x;
            let s = src[i - W - 1] + src[i - W] as u16 * 2 + src[i - W + 1]
                  + src[i - 1] as u16 * 2 + src[i] as u16 * 4 + src[i + 1] as u16 * 2
                  + src[i + W - 1] + src[i + W] as u16 * 2 + src[i + W + 1];
            dst[i] = (s >> 4) as! u8;
        }
    }
}
```

Each iteration of the outer loop writes a row of `dst` and reads three
rows of `src`; the prover shows rows of `dst` are disjoint for distinct
`y`, `src` is never written, and `src` and `dst` are distinct. Nothing
else needs saying, and a reviewer reading the signature knows that this
kernel is parallel *because the compiler proved it could be*, not because
someone asserted it.

**Why this keeps Goose's promise.** The language's claim is that data
races do not exist, which it achieves today by construction (no sharing).
`par for` achieves it by proof in a bounded, checkable form: the only
shared memory is elements the prover showed no two iterations touch, and
the only other effects are none. The spec can state this as a property
every implementation must honour, with a core of accepted index shapes as
§7.14 has for checks. Rust reaches the same guarantee for `rayon` through
ownership and `Send`/`Sync`, with the disjointness made structural by
`par_chunks_mut`; OpenMP and Chapel trust the programmer; Futhark and
Halide are functional and race-free by construction. Goose's variant
proves the disjointness of the indices the programmer wrote.

**What the runtime needs.** A pool of native threads inside the program,
started lazily, given a chunk of the index range and a pointer to the
captured environment (the references the body uses, which are fat
pointers to arrays that will not move, and the fixed-size captured
values). The body is outlined as a C function over a range, which is what
the codegen does for `simd` versions' dispatch and for thread thunks
already. Chunking is static (range divided by `hardware_threads()`) or
work-stealing by blocks; the deterministic result does not care. Native
stack per thread is the only memory, as the body has no non-fixed locals.

**Cost.** Checker: the effect-based admission (mostly existing summaries)
and the two-iteration disjointness judgement over the pass's places and
terms, which is the one new analysis in this section, perhaps a thousand
lines with its diagnostics ("iterations `y` and `y + 1` may both write
`dst[y * W + W]`"). Codegen: outlining and the dispatch, a few hundred
lines. Runtime: the thread pool, a few hundred lines of C, Windows and
POSIX. Spec: the construct, the admission rules, the core of accepted
shapes, the interaction with `simd` (a `par for` body may be the loop a
`simd fn` vectorizes; the two compose naturally) and with contained
workers (an abort is impossible by rule 1, so nothing to say). Medium to
high, concentrated in one analysis.

**Gain.** The kernels Goose already targets (stencils, particles,
Mandelbrot, matrix rows, byte transforms over independent blocks) are
embarrassingly parallel and today run on one core unless copied. The
expected speedup is the core count for memory-light kernels and the
memory-bandwidth ratio for memory-bound ones, on the same code the
reviewer already trusts. Against the share-nothing workers the saving is
the two copies and the lost references, and against OpenMP it is the
guarantee.

### 4.4 Explicit SIMD

**Where Goose stands.** `simd fn` gives a function AVX2 and AVX-512
versions chosen at run time, relying on the C compiler to vectorize the
loop (3-5x on base64 and matrix multiply; implementation.md §6.13, §9.11),
and elementwise arithmetic on structs of 2 to 16 floats is emitted as one
vector operation (1.25-1.6x on an FFT; §6.14). Both are "the compiler
vectorizes what it can, and the programmer writes loops it can". What is
missing is what autovectorizers cannot do: data-dependent control flow
per lane (masks), lane permutations (shuffles, compaction), early exits,
and table lookups turned into gathers only where that helps.

**What others built.** Every systems language that added SIMD chose
between two shapes, and the record is consistent:

| Design | Examples | What it gives | What it leaves to the programmer |
|---|---|---|---|
| fixed-width vector types | Zig `@Vector(N, T)`, Rust `std::simd`, Mojo `SIMD[T, N]`, Odin `#simd[N]T`, Swift `SIMDn`, C++26 `std::simd` | lane-wise operators, masks, `select`, `shuffle`, reductions; predictable lowering | the width (Zig's `suggestVectorLength` calls itself "guesswork", [zig-simd]), the loop, and the tail, which .NET's documentation calls "a common source of bugs" ([dotnet-simd]) |
| native-width vectors with padded or masked tails | Highway (27 targets, [highway]), .NET `Vector<T>`, C++26 `partial_load` | one source for every target; dispatch tables | Highway's advice is "Ensure all inputs/outputs are padded"; gathers are discouraged everywhere (the 2026 Rust survey measures LLVM gather/scatter as 1.75x slower on Intel and 4x on AMD than the scalar alternative, [rust-simd-2026]) |
| SPMD: write the body per element, the compiler masks and widens | ISPC's `uniform`/`varying`/`foreach` ([ispc]); OpenMP `simd` as a permission only ([openmp-simd]) | 3.6-8.5x single-core AVX speedups over serial C++ on ISPC's own kernels ([ispc-perf]); a mechanical transformation where autovectorization is brittle (Pharr's retrospective, [ispc-retro]) | nothing per loop, but ISPC had to build mask maintenance for every control construct ("It took a while to get this mask maintenance stuff implemented properly", [ispc-volta]) and depends on a patched LLVM |

The lowering Goose already uses, GCC's vector extensions, covers the
first two shapes: element-wise arithmetic, comparisons that produce
all-ones masks, `__builtin_shuffle` and `__builtin_convertvector`, and
narrower-mode synthesis when a width is unsupported ([gcc-vec]); clang
adds boolean mask vectors, `__builtin_reduce_*` and masked loads, stores,
gathers and scatters ([clang-ext]). TinyCC has none of it, so every
vector form keeps the scalar lowering §6.14 already has behind `GS_VEC`.
GCC's `target_clones` attribute is the GCC counterpart of what `simd fn`
does under clang today ([gcc-clones]), a cheap extension of the existing
feature.

**The options for Goose.**

1. **Explicit fixed-width vector types** `f32x8`, `u8x32`, masks as
   `boolx8`, and a dozen builtins (`select`, `shuffle`, `reduce_add`,
   `gather` discouraged), lowered to the vector extensions. Goose can
   make the tail problem disappear in a way libraries cannot: it owns the
   layout *and* the stacks, so a full-width load past the end of an array
   can be made safe by committing one lane width beyond every data
   stack's top and placing static data with the same margin, which is
   Highway's padding rule made free. The exactness rule of §7.12 holds
   it together: a lane width is a constant per `simd` version, and every
   version computes what the baseline does. This is the honest next step
   for the library kernels (byte sets, base64, hashing), where the
   programmer wants to choose the shape.
2. **SPMD lowering of `par for`.** The admission rules of
   [4.3](#43-par-for-shared-memory-iteration-proven-race-free) are exactly
   ISPC's preconditions, met for free: iterations are independent by
   proof, so lanes may be iterations; the body is `abortfree`, so no lane
   can abort halfway through a vector; every call is inlined, so there is
   no varying call ABI; there are no pointers between lanes and no
   per-lane allocation. What remains is what ISPC had to build, mask
   maintenance for `if`, `while`, `break` and `return` inside the body,
   and a uniform/varying split that Goose can infer (everything not
   derived from the loop variable is uniform). The payoff is that one
   construct, "these iterations are independent", gets three lowerings
   chosen by the compiler: threads across chunks, lanes within a chunk,
   both; and later a GPU ([4.5](#45-the-same-bodies-on-a-gpu)). An
   external ISPC backend for `par for` bodies in `simd fn`s would be a
   shortcut to measure the gain before building the masks (ISPC is
   C-like, so it is a second printer), at the cost of a toolchain beside
   the one C file ([ispc-dev]).
3. **Cheap enablers**: `restrict` from roots ([4.1](#41-distinct-roots-as-restrict)),
   `#pragma GCC ivdep`-style hints on loops whose bodies are `abortfree`
   with proven-independent stores, `target_clones` for `simd fn` under
   GCC. No syntax, no guarantees, and performance cliffs that are hard to
   explain; worth doing because they are nearly free.

Not an option: the Bend/HVM route to parallelism, which gives up
sequential speed for parallelism by construction. Its own numbers put a
bitonic sort at 2.0 s against 0.4 s in C and its interpreter "about 5x
slower than GHC", growing to 100x with loops and mutable arrays
([bend], [bend-hn]). Goose's bet is the opposite one, sequential code at
C speed with parallelism where it is proven safe.

The measured value of `simd` versions over the whole benchmark suite was
1-4%, within noise (§6.13), because most loops are scalar or already
vectorize, and the wins concentrate in codecs and kernels that a library
writes once. So option 1 pays through the library, option 2 through the
kernels `par for` already targets, and neither is urgent before the
correctness ladder that both lean on.

### 4.5 The same bodies on a GPU

**The fit.** A GPU compute kernel is a function over fixed-size values,
indexing storage buffers of fixed-size elements, with no recursion, no
dynamic allocation, no pointers in data, no function pointers, no
exceptions, and a thread index as its loop variable. A `par for` body
under the rules of [4.3](#43-par-for-shared-memory-iteration-proven-race-free)
is almost exactly that, and `abortfree` answers the question every GPU
language dodges, what an out-of-bounds access does (WGSL clamps, Vulkan
makes it a device option, CUDA faults or reads garbage): a Goose kernel
would have none, by proof, and need no robust-access mode. Goose already
compiles GLSL to SPIR-V, MSL and HLSL at compile time through `gfx`'s
`embed_shader`, and dispatches compute pipelines over `gfx` buffers
([stdlib.md](../stdlib.md#compute)). The missing piece is an emitter from
the Goose subset to GLSL (or directly to SPIR-V), and the plumbing that
uploads a `T[>..]` to a buffer and reads it back.

**The mismatches,** which are the real cost:

* **Layout.** Goose structs are packed; GLSL's `std430` aligns a `vec3`
  to 16 bytes and every field to its size. A `Particle { p: float3, v:
  float3, m: f32 }` is 28 bytes in Goose and 32 or 48 in a buffer. Either
  kernels restrict element types to layouts that agree (scalars, `vec2`,
  `vec4`, structs of 4-byte fields with sizes a multiple of 16), or the
  upload translates, or the compiler offers an aligned struct form. The
  SoA proposal ([soa.md](soa.md)) would sidestep it for the common case.
* **Indices and widths.** GPUs prefer 32-bit indices and have no `i64`
  everywhere; `.len` is `i64` in Goose. Kernels would compute in `u32 in a`
  and `i32`, which index types make natural.
* **Workgroups, shared memory, barriers, atomics.** Nothing in the
  `par for` shape expresses them, and the kernels that need them
  (reductions, scans, stencils with shared tiles) are the ones where GPUs
  shine. A v1 would cover map-style kernels and leave reductions to a
  second pass on the host or to a two-kernel idiom.
* **Floats.** GPU float semantics (fast-math by default on some drivers,
  no denormals) break the exactness rule unless the kernel is declared to
  accept it. `mul_add` is the precedent for a sanctioned difference.
* **Debugging and drivers.** A kernel that misbehaves does so inside a
  driver, with no `print` and no abort; the proof is the debugger.

**Gain and cost.** For the programs that have such kernels, 10-100x over
the CPU and a single language for both sides with shared types, which
nothing in the prior art offers with compile-time bounds guarantees
(Futhark comes closest with its size types). For everyone else, nothing,
and a second code generator (a few thousand lines plus a layout story)
to keep working across three graphics APIs. This is the largest pain in
the note for the narrowest audience, and it should wait until `par for`
exists and the layout question has an answer, at which point the
emitter is a backend for a body shape the checker already admits.

**What the kernel languages forbid**, checked against Goose's rules:

| System | Forbidden or restricted in a kernel | Goose today |
|---|---|---|
| CUDA C++ ([cuda-restrictions]) | RTTI and exceptions in device code; recursion in `__global__` functions; a device heap whose size "cannot be changed after a module has been loaded" | no exceptions; recursion opt-in and excludable; no heap |
| OpenCL C, SYCL ([opencl-restrictions], [sycl-2020]) | "Recursion is not supported."; "Pointers to functions are not allowed."; no dynamic allocation, no virtual functions | no function pointers; no allocation; dispatch is a jump table over an enum |
| WGSL ([wgsl]) | "Function calls cannot be recursive"; "Pointers are not storable."; only 32-bit integers; atomics on `u32`/`i32` only | "pointers are not storable" is Goose's flat-value rule verbatim; `.len` is `i64`, so kernels would compute in `u32 in a` |
| rust-gpu ([rust-gpu]) | `no_std` without `alloc`; panics were an infinite loop until version 0.9 made them an early exit; a pinned nightly compiler | an abort has no GPU meaning: hence `abortfree` as the admission rule |
| Julia CUDA.jl ([cudajl]) | "you (generally) cannot allocate memory, I/O is disallowed"; no runtime dispatch; bounds checks on by default, a `KernelException` after synchronization | no I/O in an `abortfree` body; checks proven away |
| Mojo GPU ([mojo-gpu]) | kernels "must be non-raising", return nothing | the same shape |
| Chapel GPU ([chapel-gpu]) | eligible loops allocate nothing, call no externs, and "bounds checks and nil-dereference checks are automatically disabled" | Goose would *prove* them away instead |
| Taichi, Numba ([taichi-kernels], [numba-cuda]) | no runtime recursion; "Numba disallows any memory allocating features"; bounds checks off by default or debug-only | proven, not disabled |

The match is close, and the mismatches are the ones listed above:
workgroups, shared memory and barriers (which WGSL guards with a uniformity analysis), atomics (Goose
has none; 64-bit atomics are optional on Vulkan and absent in WGSL,
[vulkan-features]), and widths. The width point is sharper than it looks:
Vulkan's 64-bit integer support is an optional feature, on NVIDIA "All
64-bit integer operations are emulated" ([nvidia-int64]), and Futhark's
move to 64-bit sizes "makes Futhark programs slower" by 23-30% on some
of its benchmarks ([futhark-0181]). Index types are the natural fit:
`u32 in verts` is what the GPU wants and what the prover understands.

**What happens out of bounds.** WGSL lets an implementation return zero,
any value from the resource, or terminate the invocation; Vulkan's
`robustBufferAccess` returns "Values from anywhere within the memory
range(s) bound to the underlying buffer object" or zero and discards the
write, and `VK_EXT_robustness2` tightens that to read-zero-skip-write
but "may be expensive on some implementations" ([wgsl], [vulkan-robust],
[robustness2]); CUDA faults the whole context ([compute-sanitizer]);
Metal says "What happens next is undefined" and offers a validation
layer too slow to ship ([metal-wwdc]); naga offers `Restrict`,
`ReadZeroSkipWrite` and `Unchecked` policies ([naga-bounds]). Futhark
keeps real checks on the GPU by having a failing thread set a global
flag and skip to the next barrier, which the host reads at
synchronization points: a geomean of 1.66x for synchronous checking,
1.07x asynchronous, 1.04x after micro-optimization ([futhark-bounds]).
Dex makes indices types, so "one cannot simply index a table with an
integer" ([dex]). No shipping kernel language gives a compile-time
bounds guarantee for arbitrary indexing; an `abortfree` Goose kernel
would need neither a runtime check nor a robustness mode, and the
compiler would decline to compile a kernel whose checks did not go
away, which is a position none of the systems above can take.

**Size and pitfalls.** The translators are large: rust-gpu is 2.4 MB of
Rust, naga 3.0 MB, ISPC 4.2 MB of C++, Futhark 5.7 MB of Haskell, Halide
17 MB, Slang 25 MB ([rust-gpu], [ispc-dev], [futhark]). A printer from
an already-typechecked, already-restricted Goose body to HLSL or WGSL
text is a different order of magnitude, because SDL_shadercross takes
HLSL or SPIR-V and does the rest, and `embed_shader` already runs that
path at compile time ([sdl-shadercross], [sdl-compute]). The binding
model is fixed: read-only storage buffers at set 0, read-write at set 1,
uniforms at set 2, and a `threadcount` the pipeline must agree on with
the shader. The reported pitfalls are the same everywhere: debugging
through driver layers, validation too slow to leave on, compilers that
re-insert the checks a kernel opted out of (wgpu on Metal,
[rust-gpu-perf]), and `assert`/`print` that work on one backend and not
another ([taichi-debug]). rust-gpu's own numbers put its kernels between
parity and 1.84x of hand-written WGSL ([rust-gpu-perf]); Triton reaches
cuBLAS parity (219.7 against 221.1 TFLOPS at 4096²) from a language with
Python-level restrictions ([triton]), which says the restrictions are not
what costs performance.

## 5. Safety: pools

### 5.1 The hole that is left

Goose's pools (`slot_pool`, `slice_pool`; reference §14) give data with
non-nested lifetimes a home without an allocator, at a cost the design
states plainly: `free` ends no lifetime. The slot stays a valid value, so
a stale reference reads a *different value of the right type*, a double
free hands one slot to two owners, and a forgotten free leaks until the
pool's scope ends. None of this is memory unsafety, and that is the point
of the design. But for a reviewer of generated code it is the one place
where correctness depends on a discipline the compiler does not check:
every `alloc` paired with one `free`, no handle used after. The samples
show the discipline (`26_file_tree`'s `release` frees a node's run and
then the node, recursively) and
[scope_exit_cleanup.md §5](scope_exit_cleanup.md#5-examples) shows where
it slips.

Three mechanisms, in increasing cost: a library with generations (stale
reads become aborts), scoped allocations (the local case, freed at scope
exit by the compiler), and owning handles (every allocation consumed
exactly once, statically).

### 5.2 Generations, as a library

A `gen_pool<T>` whose handle is an index and a generation, with a
generation array beside `items`; `free` increments the slot's generation,
and every access through a handle checks it:

```goose
root struct gen_pool<T> { items: T[>..] = [], gens: u32[>..] = [], free_slots: i64[>..<] = [] }
struct Handle { i: u32, g: u32 }

fn get<T>(p: gen_pool<T>&, h: Handle) -> T& {
    assert(p.gens[h.i] == h.g);       // a stale handle aborts here, with this line
    return p.items[h.i];
}
```

With `items` and `gens` as fields of a global pool, `h.i` can be a
`u32 in pool.items` and `requires p.gens.len == p.items.len` the pool's
invariant, so the two index checks vanish and the generation compare is
the only cost: four bytes per slot and per handle, one load and compare
per access. Vale's measurements of the same scheme and the slotmap
libraries' experience are in [5.5](#55-prior-art). This needs no language
change at all, and it turns the silent stale read into a clean abort with
a location, which is what a reviewer most wants from a pool. A double
free is caught the same way (the slot's generation has moved on), so what
is left is the leak. For that, the same module can do what Zig's debug
allocator and Odin's tracking allocator do ([5.5](#55-prior-art)): in
debug builds, record the allocation site per live slot and report the
unfreed ones, with their lines, when the pool's scope ends or on request.
That is a report, not a guarantee, but it costs nothing to write and
catches the leak in the test run. It is the right first step and should
ship as a stdlib module whatever else happens.

### 5.3 Scoped allocations

[scope_exit_cleanup.md §3.4](scope_exit_cleanup.md#34-scoped-pool-allocations)
designs this already: `scoped let s = pool.alloc_slice(n);` roots the
result at the declaring variable instead of the pool, so the existing
store, return and branch rules forbid every escape; the compiler emits
the `free_slice` at every exit as `defer` does; a manual free of it is an
error. It is the "allocated here, released here" case with no new
analysis, and it covers scratch runs and temporary nodes. Its gain is
bounded by how often that case occurs; the note there says to build it
when real code asks. Owning handles subsume it: a `scoped` allocation is
an owning handle the compiler consumes at scope exit.

### 5.4 Owning handles: a free is a shrink

**The idea.** A pool operation can return an *owning* handle, a value
that is consumed at most once and freed exactly once: `let n =
nodes.take(Node { … });` (placeholder) gives `n` the type `own Node&`. On
every path out of its block the handle is either *consumed*, by
`nodes.release(n)`, by passing it to a parameter declared `own`, by
storing it into a field declared `own`, or by returning it, or it is
*unconsumed*, and then the compiler frees it at the block's exit, as a
`defer` it wrote itself. Consuming it twice is an error at the second
use. So a leak is impossible by construction and a double free is a
compile error, and the program says `release` only where it wants the
slot back early. This is the affine discipline Swift's non-copyable
types and Mojo's default take, with Austral's linear form (every handle
consumed explicitly, nothing implicit) available per pool for the frees
that need arguments ([5.5](#55-prior-art)). Goose's existing flow analysis
(definite assignment, liveness) does the path reasoning, and the exit
code is the `defer` machinery.

**A free is a shrink.** The handle is a reference, so the program reads
and writes through it as through any `T&`, and may bind plain references
to its target (`let r: Node& = n;`). The stale-read question is what
happens to `r` after `nodes.release(n)`. Goose has answered this exact
question for grow-shrink arrays (spec §5.2): a reference into a `T[>..<]`
lives in variables only, never in storage, and a shrink is an error while
any such variable is used afterwards. Treat a release as a shrink of the
slot: every plain reference rooted at `n` must be dead by then (the
liveness test, with the shrink summaries carrying it through calls), and
none may have been stored into a holder. With that, a stale read through a
reference is impossible, and the only stale access left is through a plain
integer index, which the design keeps as the escape hatch it has always
been. Relative links in pool elements (`Node&<u32 in nodes.items>`) are
stored references and are therefore the hard case: a `Node` freed while
another node links to it leaves a stale link. The pool finalizer design
([scope_exit_cleanup.md §3.3](scope_exit_cleanup.md#33-pool-finalizers))
is the complement: freeing a node runs the pool's finalizer, which must
consume the node's `own` fields; what the finalizer cannot do is unlink
the *other* nodes that point here, so stale relative links stay a logic
bug, as in every pool design short of reference counting.

**Rules, sketched.**

1. An `own` value is not copyable: no `copy`, no silent fixed-size copy,
   no `let m = n` (that is a move, after which `n` is dead), no passing
   to a non-`own` parameter by value. Binding a plain reference to its
   target is a borrow and allowed. A struct with an `own` field is itself
   non-copyable, which in Goose means that reading it out of an array or
   a field is by reference (`.=`, `&`) and never by the silent copy that
   fixed-size values get today; this is the virality every linear system
   has, and the one place where the feature changes how ordinary code
   reads.
2. On every path through its declaring block, including `break`,
   `continue`, `return`, a block `return` through a HOF and a
   `return … from` passing through the frame, it is consumed at most once;
   where it is not consumed, the compiler frees it at the exit, on the
   same paths and in the same order as a `defer` written at its
   declaration. A `defer` body may consume it explicitly, and then every
   path counts as consuming it there. A pool declared *linear* (opt-in)
   turns the implicit free into an error, for the handles whose release
   needs arguments or a second pool.
3. A struct field of `own` type makes the struct an owner: when a slot
   holding such a struct is freed, its `own` fields are freed too, by the
   pool's finalizer ([scope_exit_cleanup.md §3.3](scope_exit_cleanup.md#33-pool-finalizers)),
   which the compiler writes from the field types unless the program
   writes one. Reading an `own` field out of a struct is a *destructive
   read* (`take(n.entries)`), which leaves the field empty and hands the
   obligation to the reader; a plain read is a borrow. A struct literal
   must provide every `own` field with an `own` value, and `default<T>()`
   is unavailable for such a struct.
4. Arrays of `own` elements are excluded in v1, as references into
   grow-shrink arrays were restricted to variables: the consumption rules
   for `pop`, `clear`, scope exit and `for own x in xs` are a design of
   their own.
5. `own` values are not flat, so they do not cross queues or appear in
   images; `from_bytes` never produces one.
6. Releasing a handle is a shrink for every plain reference rooted at it
   (rule above); `index_of` on a handle gives a plain index with no
   obligation.

```goose
// The file-tree sample under owning handles: a directory owns its run,
// and releasing a node consumes both. The compiler rejects a release
// that forgets the run, and a second release of the same node.
struct Node { name: u8[..15], entries: own Link[:] }

recursive fn release(n: own Node&) {
    let entries = take(n.entries);                            // the run's obligation moves here
    for e in entries { if e { release(take_child(e)); } }     // each child consumed once
    runs.free_slice(entries);                                 // consumes the run
    nodes.release(n);                                         // consumes the handle (or: fall off the end)
}
```

The sketch glosses over the relative links: `take_child(e)` would have to
turn a stored `Link` into an `own` handle, which is the one step that needs
a runtime occupancy check or a generation, since two parents could link to
one child. A tree whose links are owned by exactly one parent (as a file
tree's are) can make `Link` itself `own Node&<u32 in nodes.items>?` and
store the obligation in the run, which rule 3 allows for a run of a
struct; it is then the `slice_pool`'s `free_slice` that must refuse a run
with unconsumed `own` elements, which rule 4 defers. So the honest v1 is:
owning handles in locals, parameters, results and struct fields; runs of
`own` later; relative links stay unowned and generation-checked where a
program needs to be sure.

**What it costs.** This is the most invasive item in the note: a value
class the language does not have (non-copyable), touching assignment,
`copy`, literals, `match` by value, HOF captures (a block that consumes a
captured `own` can be called twice by its HOF, so capture of an `own`
must be a move into the block, or forbidden), generics (a `T` instantiated
with an `own` type must reject bodies that copy `T`, as index types
already reject bodies that compute into `T`), `return … from`, `defer`,
workers and images. The checker side reuses definite assignment, the
shrink liveness test and the store record; the pool library gains the
`own`-returning operations beside the existing ones. Swift's experience
adding `~Copyable` to an existing language, and Austral's experience
designing with linearity from the start, calibrate the size
([5.5](#55-prior-art)). Expect several thousand lines and a spec section
the size of §5.2.

**What it buys.** Every allocation freed exactly once and no stale read
through a reference, as compiler-checked claims; a reviewer reading
`own Node&` in a signature knows the callee takes responsibility. And a
fact the optimizer can spend later: a pool whose handles are all owned
knows its live set exactly, so it could compact or return memory, which
no Goose pool can do today because a stale reference might observe it.

### 5.5 Prior art

The question "how does a language make a resource be released exactly
once" has a long record. What it says, by mechanism:

**Linear types, hand-released.** Austral is the cleanest statement of the
linear discipline without a borrow checker: "a value of a linear type
must be used once and only once"; a linear variable declared outside an
`if` must be consumed in every branch or none; one declared outside a
loop may not be consumed inside it; "You cannot return from a function
while there are any unconsumed linear values"; a record holding a linear
field is linear and is taken apart by destructuring; a non-consuming use
is a `borrow` statement whose reference cannot leave it ([austral-spec]).
Its author reports the checker at "600 lines of OCaml" and the algorithm
at "a page of text", and the cost as "you have to call destructors by
hand" and incompatibility with exceptions ([austral-checker],
[austral-cost]). ATS, Idris 2's multiplicities ("There are 0 uses of
linear name d''") and Cogent are the same discipline in research
languages; Cogent is the one place where leak freedom is a theorem, at the
price of a non-recursive purely functional language with a certifying
compiler ([cogent]). Vale pairs linear ownership with generational
references for the non-owning links and concludes of its own code: "It's
verbose, but it works!" ([vale-linear]).

**Affine types with a compiler-written release.** Swift added
`~Copyable` types to a mature language in two releases: every parameter
of such a type says `borrowing`, `consuming` or `inout`, a `deinit` runs
on the paths that did not consume the value, `consume x` makes a later
use an error, and generics over them came a release later after reviewers
found the repetition tedious ([se-0390], [se-0377], [se-0427]). Mojo
shipped the same hybrid in January 2026: destruction at last use by
default, and "first-class support for explicitly-destroyed types" as an
opt-in whose diagnostic reads "'file' abandoned without being explicitly
destroyed"; its standard containers had to be adapted to hold them
([mojo-0261], [mojo-anytype]). Rust is the cautionary tale of doing this
late: `#[must_use]` is a lint silenced by `let _ =`, the "must move
types" design needs every `match` arm to move the value and a story for
panics ([must-move]), the survey of the design space ends "I posit that
this system is deeply unpleasant" with "the library work" as the largest
burden ([linear-rust]), and the attempts to add a `Leak` or `Forget`
marker trait founder on backward compatibility ([changing-rules]); the
2026 Rust-for-Linux roadmap still lists the exploration ([rfl-2026]).
The lesson for Goose is to decide handle ownership before `i64` handles
are everywhere in user code, and to take the affine default with a
linear opt-in, which is where Swift and Mojo both arrived.

**Scoped bindings with an escape check.** OxCaml's `local_` mode is the
closest "mode on a binding" design: "Stack-allocated values are not
allowed to escape their region", `stack_ e` makes an escape an error
rather than a silent heap fallback, `exclave_` lets a callee return a
fresh local into the caller's region, "Mutable fields and array elements
require their contents to be `global`", a closure capturing a local is
local, and locals may not be passed to tail calls ([oxcaml-local]).
Those are exactly the rules [5.3](#53-scoped-allocations) gets from
Goose's root rules for free, and the two hard ones, no scoped handle in
a mutable container and no capture by an escaping block, are already
errors in Goose. Cyclone's unique pointers are the older relative: affine,
with a swap operator for stores into shared data and garbage collection
as the fallback for an unfreed one ([cyclone-unique]); Pony's `iso`
capability moves out of a field only by destructive read
([pony-destructive]).

**Generations.** Weissflog's handles essay is the industry baseline:
items in per-system arrays, a handle as index plus a tag, a per-slot
generation bumped on release, "otherwise this is a dangling access"
([handles]). Rust's `slotmap` keeps a version per slot and notes the wrap
after 2^31 reuses of one slot as "incredibly unlikely" ([slotmap]); Unity
entities carry an index and a version ([unity-entity]). Vale measured the
cost of checking on every dereference at 10.84% over unchecked code and
less than half the cost of reference counting on its terrain benchmark
([vale-gen]); a Goose pool checks once per lookup, the cheaper profile.
Zig's debug allocator "Never reuses memory addresses" and reports leaks
at `deinit` ([zig-debug-alloc]); Odin's tracking allocator does the same
under `ODIN_DEBUG` ([odin-tracking]). Neither is static, and both cost
nothing to write.

**Machine authors and ownership rules.** There is no study of models
writing Austral or Swift's non-copyable code; the evidence is Rust.
Compiler-guided repair reduced borrowing errors by 75% on CRUST-Bench,
whose authors call them "relatively tractable when explicit compiler
feedback is available" ([crust-bench]); guided repair resolved
borrow-checker violations at 74% across models in SafeTrans
([safetrans]); ownership and lifetime errors were 16.7% of 86,726
failing Rust samples in a 2026 taxonomy ([rust-errors]). The pattern
matches section [3.9](#39-what-the-llm-results-say): a complete, local
rule whose error names the fix ("0 uses of `d`", "`file` abandoned") is
the tractable class, and the cost that hurts a machine author is the one
that hurts a human, virality forcing a container to be taken apart and
rebuilt.

## 6. The cost picture

Gain is what the reviewer learns and what the compiler spends; pain is
implementation size, spec text, library work and what correct programs
it rejects. "Depends on" is the order forced by the designs.

| # | Feature | Gain for the reviewer | Gain for the compiler | Pain | Depends on |
|---|---|---|---|---|---|
| 1 | Certificate (`goose claims`, code lens) | sees every claim and the facts behind it | none; it is a report | small: a symmetric record in the pass, a printer | nothing |
| 2 | `restrict` from exact roots | none directly | about half of the 3.6x MSVC gap on flat `blur`, as the C++ `__restrict` row shows | small: codegen consults roots at parameter emission | nothing |
| 3 | `requires`/`ensures` | preconditions and result facts in the signature, checked at every caller | result facts, post-call length facts, entry facts in cycles; checks move to call sites and vanish | medium: clauses, two-way fact flow, predicated facts, `return … from`, spec core; about the size of index types | 1 for visibility |
| 4 | `abortfree` | "nothing in here can fail" | no side exits: vectorization, speculation; precondition of 6 and 8 | medium: new judgements are small; library audit is real; spec core per abort kind | 3 (asserts lift into `requires`) |
| 5 | `pure`, `writes`/`reads` | what the function touches, as a stable contract | `pure`/`const` attributes; later, compile-time evaluation | small: summaries exist; externs need declarations | 1 |
| 6 | `par for` | parallel because proven independent | the core count on kernels, with no copies | medium-high: the two-iteration disjointness analysis, outlining, a thread pool | 4, and `distinct` from 3 |
| 7 | Generations (`gen_pool`) | stale reads abort with a line | none | small: a stdlib module | nothing |
| 8 | Scoped allocations | local allocations cannot leak or escape | none | small: `defer` plus a root the checker enforces | nothing (subsumed by 10) |
| 9 | Termination | "terminates" on loops and arithmetic recursion | with 4 and 5, `const` in C's sense | medium: cheap for counted and monotone loops; variants; nothing for pool recursion | 4 |
| 10 | Owning handles | freed exactly once, no double free, no stale read through a reference | pools could compact later | high: a non-copyable value class touches everything that copies; containers of handles need destructive reads | 8's rules; finalizers |
| 11 | Explicit SIMD types, or SPMD lowering of `par for` | none | masks, shuffles, gathers where autovectorization stops; lanes as iterations | medium-high: a type family and builtins, or mask maintenance for the body's control flow | types: nothing; SPMD: 6 |
| 12 | GPU kernels | none beyond 6 | 10-100x on map-style kernels | high: a second emitter, a layout story, drivers | 6, `abortfree`, a layout decision |

Three observations the table makes. The cheapest items (1, 2, 5, 7) are
independent of each other and of everything else, and 2 alone is worth
more measured speed than anything short of 6. The correctness ladder
(3 → 4 → 9) is sequential and each rung is the precondition of a speed
feature (4 for 6, 6 for 12), so correctness is the path to speed here, not
a competitor for effort. And the safety item with the largest gain (10)
is also the one with the least connection to the rest; it can wait for
the evidence that 7 does not give enough.

## 7. Order

1. **The certificate** ([3.6](#36-the-certificate)) and **`restrict` from
   roots** ([4.1](#41-distinct-roots-as-restrict)): small, independent,
   measurable at once with `bench/bce_ab.py` and the MSVC `blur` row.
2. **`gen_pool`** in the standard library ([5.2](#52-generations-as-a-library)),
   and the file-tree and LRU samples rewritten on it to see what the
   generation check costs in the benchmarks.
3. **`requires`/`ensures`** ([3.2](#32-contracts-in-the-fact-language-requires-and-ensures)),
   starting with `requires` alone (one direction, the index-cast judgement
   reused), then `ensures` and the predicated facts. Convert the
   `checkfree` functions in `bench/goose` and the stdlib to clauses and
   check that the certificate shows the same checks removed.
4. **`pure` and frames** ([3.4](#34-effects-pure-and-frames)), emitted as
   C attributes; measure on the `interp` and `calc` rows, where helper
   calls in loops are common.
5. **`abortfree`** ([3.3](#33-abortfree-no-runtime-check-of-any-kind)),
   with the library audit, and the spec's core lists.
6. **`par for`** ([4.3](#43-par-for-shared-memory-iteration-proven-race-free)):
   the disjointness judgement first as a `--par-test` that reports which
   loops would qualify over the benchmark suite, then the runtime and
   codegen. Measure `blur`, `particles`, `mandelbrot` and `scene`.
7. **Termination** ([3.5](#35-termination)) as a classification first
   (the certificate reports "terminates: counted loops" with no new
   syntax), variants later.
8. **Owning handles** ([5.4](#54-owning-handles-a-free-is-a-shrink)) when
   the evidence from 2 says generations are not enough, or when a program
   needs the compaction they would allow.
9. **SIMD types and GPU kernels** ([4.4](#44-explicit-simd), [4.5](#45-the-same-bodies-on-a-gpu))
   after 6, as backends for the body shape it admits.

## 8. Open questions

* **Names.** `checkfree` is taken and slightly overpromises; `abortfree`
  (or `noabort`) says what it means but is long; `proven` says too little. One option:
  `checkfree` becomes the strong claim (every check gone) and the
  bounds-only rung is dropped, since a function with no bounds checks
  usually has no other checks either, and the stdlib conversion would
  show how often that fails.
* **Clause syntax and named results.** Clauses after the signature read
  well for one result; multiple results need names (`-> idx: i64, found:
  bool`) or positions. Named results are a change to §7.3 with uses beyond
  clauses (documentation, the language server) and may be worth doing
  first.
* **`requires` on an `export fn`.** Checked at entry, as proposed, or
  documented into the generated C header as a comment and trusted? The
  soundness bug of index_types.md §8.3 argues for the check.
* **Float reductions in `par for`.** Exclude, or fix a tree order and
  chunk count and accept a deterministic result that differs from the
  sequential one? The exactness principle of `simd` says exclude.
* **`pure` and global reads.** The proposed `pure` reads globals. A
  stricter `const fn` that reads only its arguments would be the
  compile-time-evaluable form; whether to have both words or infer the
  stronger one silently.
* **Capture of `own` values in blocks.** Move into the block, forbid, or
  allow when the HOF is proven to call the block at most once (which the
  effect summary could record).
* **Arrays of `own`**, and `realloc_slice` of an `own` run.
* **Relative links and ownership.** Whether a `Node&<u32 in pool>` link
  can carry an obligation at all, or whether generations are the permanent
  answer for stored links.
* **Compile-time evaluation** of `pure` calls through the in-process
  backend: its own note.
* **A `nowrap` rung** for signed arithmetic, and whether release-mode
  wrapping should stay the semantics for `abortfree` code or whether such
  code should be allowed to ask for the debug checks to be proven away
  instead of skipped.

## References

<!-- TODO: spec/implementation anchors and the remaining external links -->

[algoveri]: https://arxiv.org/abs/2602.09464 "AlgoVeri: 77 classical algorithms with aligned specifications in Dafny, Verus and Lean (2026)"
[vericoding]: https://arxiv.org/abs/2509.22908 "Vericoding: Benchmarking LLMs on Formally Verified Code Generation (2025)"
[clever]: https://arxiv.org/abs/2505.13938 "CLEVER: a curated benchmark for formally verified code generation in Lean (2025)"
[dafnybench]: https://arxiv.org/abs/2406.08467 "DafnyBench: a benchmark for formal software verification (2024)"
[verusage]: https://arxiv.org/abs/2512.18436 "VeruSAGE: agents on 849 proof tasks from eight verified Verus systems (2025/2026)"
[verina]: https://arxiv.org/abs/2505.23135 "Verina: benchmarking verifiable code generation (2025)"
[veribench]: https://icml.cc/virtual/2026/82455 "VeriBench: Python to Lean verification tasks (ICML 2026)"
[spec-harness]: https://arxiv.org/abs/2604.00280 "Spec-Harness: 22.7% of verifier-accepted JML specs have no postcondition (2026)"
[nl2contract]: https://arxiv.org/abs/2510.12702 "Beyond Postconditions: generating pre- and postconditions from natural language (2025)"
[alphaverus]: https://arxiv.org/abs/2412.06176 "AlphaVerus: bootstrapping formally verified code generation (2024)"
[prover-judge]: https://arxiv.org/abs/2607.14340 "The Prover Is the Judge: agents discharging 49,280 GNATprove obligations (2026)"
[exverus]: https://arxiv.org/abs/2603.25810 "ExVerus: source-level counterexamples for LLM proof repair in Verus (2026)"
[loris]: https://arxiv.org/abs/2605.17914 "LORIS: localized feedback for LLM loop invariants (TOPLAS 2026)"
[baldur]: https://arxiv.org/abs/2303.04910 "Baldur: whole-proof generation and repair with LLMs (2023)"
[kverus]: https://arxiv.org/abs/2605.03822 "KVerus: Verus proof synthesis across verifier releases (ASE 2026)"
[loopy]: https://arxiv.org/abs/2311.07948 "Loopy: LLM loop invariants checked by Frama-C (2023)"
[autoverus]: https://arxiv.org/abs/2409.13082 "AutoVerus: automated proof generation for Rust code (OOPSLA 2025)"
[type-constrained]: https://arxiv.org/abs/2504.09246 "Type-constrained code generation with language models (2025)"
[multi-lcb]: https://arxiv.org/abs/2606.20517 "Multi-LCB: 24 models across twelve languages (ICLR 2026)"
[rustassistant]: https://arxiv.org/abs/2308.05177 "RustAssistant: fixing Rust compilation errors with LLMs (2023)"
[fstar-synth]: https://arxiv.org/abs/2405.01787 "Towards Neural Synthesis for SMT-Assisted Proof-Oriented Programming (F*, ICSE 2025)"
[anthropic-fv]: https://www.anthropic.com/research/formal-verification "Anthropic and Theorem: formal verification of fuzz entry points in 123 libraries (October 2026)"
[sonar]: https://www.sonarsource.com/company/press-releases/sonar-data-reveals-critical-verification-gap-in-ai-coding/ "Sonar: the verification gap in AI coding (January 2026)"
[trust-calibrated]: https://arxiv.org/abs/2606.01969 "Trust-calibrated code review of agent changes (JetBrains, 2026)"
[kleppmann]: https://martin.kleppmann.com/2025/12/08/ai-formal-verification.html "Kleppmann: AI and formal verification (December 2025)"

[austral-spec]: https://austral-lang.org/spec/spec.html "The Austral specification: linear types"
[austral-checker]: https://borretti.me/article/how-australs-linear-type-checker-works "Borretti: how Austral's linear type checker works"
[austral-cost]: https://blog.lambdaclass.com/austral/ "Borretti: Austral, on LambdaClass (costs of linear types)"
[cogent]: https://trustworthy.systems/projects/OLD/cogent "Cogent: a certifying language for file systems (leak freedom as a theorem)"
[vale-linear]: https://verdagon.dev/blog/linear-types-borrowing "Verdagon: linear types and borrowing in Vale"
[se-0390]: https://github.com/apple/swift-evolution/blob/main/proposals/0390-noncopyable-structs-and-enums.md "Swift SE-0390: noncopyable structs and enums"
[se-0377]: https://github.com/apple/swift-evolution/blob/main/proposals/0377-parameter-ownership-modifiers.md "Swift SE-0377: borrowing and consuming parameter ownership modifiers"
[se-0427]: https://forums.swift.org/t/accepted-se-0427-noncopyable-generics/73560 "Swift SE-0427 accepted: noncopyable generics"
[mojo-0261]: https://mojolang.org/releases/v0.26.1/ "Mojo 0.26.1 release notes: explicitly-destroyed types"
[mojo-anytype]: https://mojolang.org/docs/std/traits/anytype/AnyType/ "Mojo: AnyType, Deinitable and linear types"
[must-move]: https://smallcultfollowing.com/babysteps/blog/2023/03/16/must-move-types/ "Matsakis: must move types (2023)"
[linear-rust]: https://faultlore.com/blah/linear-rust/ "Gankra: the pain of real linear types in Rust"
[changing-rules]: https://without.boats/blog/changing-the-rules-of-rust/ "boats: changing the rules of Rust (Leak)"
[rfl-2026]: https://goals.rust-lang.org/2026/roadmap-rust-for-linux.html "Rust project goals 2026: Rust for Linux roadmap"
[oxcaml-local]: https://oxcaml.org/documentation/stack-allocation/reference/ "OxCaml: stack allocation reference (local_, stack_, exclave_)"
[cyclone-unique]: https://cyclone.thelanguage.org/wiki/Pointers%20with%20Restricted%20Aliasing "Cyclone: pointers with restricted aliasing (unique pointers)"
[pony-destructive]: https://tutorial.ponylang.io/reference-capabilities/consume-and-destructive-read.html "Pony: consume and destructive read"
[handles]: https://floooh.github.io/2018/06/17/handles-vs-pointers.html "Weissflog: handles are the better pointers"
[slotmap]: https://docs.rs/slotmap/latest/slotmap/ "Rust slotmap: versioned slots"
[unity-entity]: https://docs.unity3d.com/Packages/com.unity.entities@1.3/api/Unity.Entities.Entity.html "Unity Entities: Entity index and version"
[vale-gen]: https://verdagon.dev/blog/generational-references "Verdagon: generational references and their measured cost"
[zig-debug-alloc]: https://raw.githubusercontent.com/ziglang/zig/master/lib/std/heap/debug_allocator.zig "Zig std.heap.DebugAllocator: leak reporting"
[odin-tracking]: https://odin-lang.org/docs/overview/ "Odin overview: the tracking allocator"
[crust-bench]: https://arxiv.org/abs/2504.15254 "CRUST-Bench: C to safe Rust transpilation benchmark (2025)"
[safetrans]: https://arxiv.org/abs/2505.10708 "SafeTrans: LLM-assisted translation from C to Rust (2025)"
[rust-errors]: https://arxiv.org/abs/2608.00661 "Unreliable in practice: a taxonomy of 86,726 erroneous LLM Rust samples (2026)"

[zig-simd]: https://raw.githubusercontent.com/ziglang/zig/master/lib/std/simd.zig "Zig std.simd: suggestVectorLength"
[dotnet-simd]: https://learn.microsoft.com/en-us/dotnet/standard/simd "Microsoft: SIMD-accelerated types in .NET (Vector<T>, remainder handling)"
[highway]: https://github.com/google/highway "Google Highway: portable SIMD with runtime dispatch"
[rust-simd-2026]: https://shnatsel.github.io/state-of-simd-rust-2026/ "The state of SIMD in Rust, 2026"
[ispc]: https://ispc.github.io/ispc.html "ISPC user's guide: uniform, varying, foreach"
[openmp-simd]: https://www.openmp.org/spec-html/5.0/openmpsu42.html "OpenMP 5.0: the simd construct"
[ispc-perf]: https://ispc.github.io/perf.html "ISPC performance: measured speedups"
[ispc-retro]: https://pharr.org/matt/blog/2018/04/29/ispc-retrospective.html "Pharr: ISPC retrospective"
[ispc-volta]: https://pharr.org/matt/blog/2018/04/21/ispc-volta-c-and-spmd.html "Pharr: Volta, C and SPMD (mask maintenance)"
[ispc-dev]: https://github.com/ispc/ispc/wiki/ISPC-Development-Guide "ISPC development guide: LLVM dependency"
[gcc-vec]: https://gcc.gnu.org/onlinedocs/gcc/Vector-Extensions.html "GCC: vector extensions"
[clang-ext]: https://clang.llvm.org/docs/LanguageExtensions.html "Clang language extensions: vectors, reductions, masked builtins"
[gcc-clones]: https://gcc.gnu.org/onlinedocs/gcc-14.2.0/gcc/Common-Function-Attributes.html "GCC: target_clones attribute"
[bend]: https://github.com/HigherOrderCO/Bend "Bend: a massively parallel high-level language"
[bend-hn]: https://news.ycombinator.com/item?id=40390287 "Bend launch discussion with benchmark numbers (2024)"
[cuda-restrictions]: https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-support.html "CUDA C++ programming guide: C++ language support in device code"
[opencl-restrictions]: https://registry.khronos.org/OpenCL/sdk/3.0/docs/man/html/restrictions.html "OpenCL C 3.0: restrictions"
[sycl-2020]: https://www.khronos.org/blog/sycl-2020-what-do-you-need-to-know "Khronos: SYCL 2020"
[wgsl]: https://www.w3.org/TR/WGSL/ "WebGPU Shading Language specification"
[rust-gpu]: https://github.com/Rust-GPU/rust-gpu "rust-gpu: Rust as a shader language"
[rust-gpu-perf]: https://github.com/Rust-GPU/rust-gpu/discussions/614 "rust-gpu discussion 614: performance against hand-written WGSL"
[cudajl]: https://cuda.juliagpu.org/stable/usage/overview/ "CUDA.jl: overview and kernel restrictions"
[mojo-gpu]: https://max.modular.com/stable/gpu/fundamentals "Mojo GPU programming fundamentals"
[chapel-gpu]: https://chapel-lang.org/docs/technotes/gpu.html "Chapel: GPU programming technote"
[taichi-kernels]: https://docs.taichi-lang.org/docs/kernel_function "Taichi: kernels and functions"
[taichi-debug]: https://docs.taichi-lang.org/docs/debugging "Taichi: debugging (assert and print per backend)"
[numba-cuda]: https://numba.readthedocs.io/en/stable/cuda/cudapysupported.html "Numba: supported Python features in CUDA kernels"
[vulkan-features]: https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceVulkan12Features.html "Vulkan 1.2 features: 64-bit atomics"
[nvidia-int64]: https://forums.developer.nvidia.com/t/question-about-64-bit-integer-performance/64147 "NVIDIA forums: 64-bit integer performance"
[futhark-0181]: https://futhark-lang.org/blog/2020-10-08-futhark-0.18.1-released.html "Futhark 0.18.1: 64-bit sizes and their cost"
[vulkan-robust]: https://docs.vulkan.org/spec/latest/chapters/shaders.html "Vulkan specification: robust buffer access"
[robustness2]: https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_robustness2.html "VK_EXT_robustness2"
[compute-sanitizer]: https://docs.nvidia.com/compute-sanitizer/ComputeSanitizer/index.html "NVIDIA Compute Sanitizer"
[metal-wwdc]: https://developer.apple.com/videos/play/wwdc2020/10616/ "WWDC20: debug GPU-side errors in Metal"
[naga-bounds]: https://docs.rs/naga/latest/naga/proc/index/enum.BoundsCheckPolicy.html "naga: BoundsCheckPolicy"
[futhark-bounds]: https://futhark-lang.org/blog/2020-07-13-bounds-checking.html "Futhark: bounds checking on GPUs"
[dex]: https://arxiv.org/abs/2104.05372 "Dex: getting to the point, index sets as types"
[futhark]: https://futhark-lang.org/ "Futhark"
[sdl-shadercross]: https://github.com/libsdl-org/SDL_shadercross "SDL_shadercross"
[sdl-compute]: https://wiki.libsdl.org/SDL3/SDL_CreateGPUComputePipeline "SDL3: SDL_CreateGPUComputePipeline (binding model)"
[triton]: https://triton-lang.org/main/getting-started/tutorials/03-matrix-multiplication.html "Triton tutorial: matrix multiplication against cuBLAS"

[wuffs]: https://github.com/google/wuffs "Wuffs: Wrangling Untrusted File Formats Safely"
[wuffs-facts]: https://github.com/google/wuffs/blob/main/doc/note/facts.md "Wuffs: facts"
[wuffs-assert]: https://github.com/google/wuffs/blob/main/doc/note/assertions.md "Wuffs: assertions"
[wuffs-bounds]: https://github.com/google/wuffs/blob/main/doc/note/bounds-checking.md "Wuffs: bounds checking"
[wuffs-png]: https://nigeltao.github.io/blog/2021/fastest-safest-png-decoder.html "Tao: the fastest, safest PNG decoder in the world"
[no-panic]: https://docs.rs/no-panic "Rust no_panic crate"
[rust-no-panic-2015]: https://internals.rust-lang.org/t/no-panic/1356 "Rust internals: #[no_panic] (2015)"
[rust-no-panic-2025]: https://internals.rust-lang.org/t/what-is-the-status-on-the-panic-free-subset-of-rust-idea/22155 "Rust internals: status of a panic-free subset (2025)"
[rust-immediate-abort]: https://github.com/rust-lang/rust/issues/118393 "Rust: panic=immediate-abort"
[ada-restrictions]: https://docs.adacore.com/gnat_rm-docs/html/gnat_rm/gnat_rm/standard_and_implementation_defined_restrictions.html "GNAT reference manual: restrictions"
[frama-c-rte]: https://frama-c.com/fc-plugins/rte.html "Frama-C: the RTE plugin"
[nim-effects]: https://nim-lang.org/docs/manual.html "Nim manual: effect system"
[koka]: https://ar5iv.labs.arxiv.org/html/1406.2061 "Leijen: Koka, programming with row-polymorphic effect types"
[flix]: https://flix.dev/ "The Flix programming language"
[d-pure]: https://dlang.org/spec/function.html "D language specification: functions (pure, nothrow, @nogc)"
[d-inference]: https://www.digitalmars.com/d/archives/digitalmars/D/Avoid_attribute_soup_by_doing_attribute-infering_for_function_s_of_377370.html "D forum: attribute soup and attribute inference"
[fortran-pure]: https://j3-fortran.org/doc/year/18/18-007r1.pdf "Fortran 2018 draft standard: pure procedures (clause 15.7)"
[rust-const-fn]: https://doc.rust-lang.org/reference/const_eval.html "Rust reference: constant evaluation"
[gcc-attrs]: https://gcc.gnu.org/onlinedocs/gcc/Common-Attributes.html "GCC: common function attributes (pure, const)"
[ocaml-effects]: https://ocaml.org/manual/effects.html "OCaml manual: effect handlers"
[dafny-reads]: https://github.com/dafny-lang/dafny/pull/4440 "Dafny PR 4440: reads clauses on methods"
[spark-gg]: https://docs.adacore.com/spark2014-docs/html/ug/en/source/how_to_write_subprogram_contracts.html "SPARK user's guide: generation of dependency contracts"
[prusti]: https://www.pm.inf.ethz.ch/research/prusti.html "Prusti: verification of Rust programs"
[idris-total]: https://idris2.readthedocs.io/en/latest/tutorial/theorems.html "Idris 2: totality checking"
[agda-termination]: https://agda.readthedocs.io/en/latest/language/termination-checking.html "Agda: termination checking"
[lean-termination]: https://lean-lang.org/doc/reference/latest/Definitions/Recursive-Definitions/ "Lean 4 reference: recursive definitions and termination"
[dafny-termination]: https://dafny.org/dafny/OnlineTutorial/Termination "Dafny tutorial: termination"
[spark-variants]: https://docs.adacore.com/spark2014-docs/html/ug/en/source/assertion_pragmas.html "SPARK user's guide: loop variants"
[acl2-defun]: https://www.cs.utexas.edu/~moore/acl2/v6-3/DEFUN.html "ACL2: defun and measures"
[loop-patterns]: https://eceasst.org/index.php/eceasst/article/view/2191 "Pani, Veith, Zuleger: Loop patterns in C programs (2015)"
[loopus]: https://arxiv.org/abs/1203.5303 "Zuleger et al.: bound analysis of imperative programs with the size-change abstraction (Loopus)"
[stackanalyzer]: https://www.absint.com/stackanalyzer/index.htm "AbsInt StackAnalyzer"
[gnatstack]: https://www.adacore.com/press/adacore-announces-innovative-stack-analysis-tool "AdaCore: GNATstack"
[cargo-call-stack]: https://github.com/japaric/cargo-call-stack "cargo-call-stack"
[zig-stack]: https://github.com/ziglang/zig/issues/1639 "Zig issue 1639: eliminate stack overflow"
[sel4-faq]: https://sel4.systems/About/FAQ.html "seL4 FAQ: no heap"
[misra-21-3]: https://www.mathworks.com/help/bugfinder/ref/misrac2012rule21.3.html "MISRA C:2012 rule 21.3"
[jasmin-ct]: https://jasmin-lang.readthedocs.io/en/stable/tools/ct.html "Jasmin: constant-time checking"
[subtle]: https://docs.rs/subtle "Rust subtle crate"

[spark-avocs]: https://www.adacore.com/uploads/papers/spark_avocs_2018.pdf "Dross et al.: Climbing the software assurance ladder (AVoCS 2018)"
[spark-guidance]: https://www.adacore.com/uploads/books/Spark-Guidance-1.2-web.pdf "AdaCore and Thales: Implementation Guidance for the Adoption of SPARK, release 1.2"
[spark-hcss]: https://www.sos-vo.org/system/files/sos_files/Levels_of_Software_Assurance_in_SPARK.pdf "Moy: Levels of software assurance in SPARK (HCSS 2017)"
[spark-usage]: https://docs.adacore.com/spark2014-docs/html/ug/en/usage_scenarios.html "SPARK user's guide: applying SPARK in practice"
[spark-nvidia]: https://www.adacore.com/blog/nvidia-security-team-what-if-we-just-stopped-using-c "AdaCore: NVIDIA security team, what if we just stopped using C"
[spark-keynote]: https://proteancode.com/keynote.pdf "Chapman and Schanda: Are we there yet? Twenty years of industrial theorem proving with SPARK (ITP 2014)"
[spark-ai]: https://www.adacore.com/blog/foundational-cryptography-in-spark "AdaCore: foundational cryptography in SPARK, written with AI (2026)"
[dafny-guide]: https://dafny.org/dafny/OnlineTutorial/guide "Dafny: getting started guide"
[dafny-brittle]: https://dafny.org/blog/2023/12/01/avoiding-verification-brittleness/ "Dafny blog: avoiding verification brittleness"
[ironfleet]: https://6826.csail.mit.edu/2019/papers/ironfleet.pdf "Hawblitzel et al.: IronFleet (SOSP 2015)"
[verus-sosp]: https://par.nsf.gov/servlets/purl/10630619 "Lattuada et al.: Verus, a practical foundation for systems verification (SOSP 2024)"
[sel4-whitepaper]: https://sel4.systems/About/seL4-whitepaper.pdf "The seL4 whitepaper"
[mariposa]: https://www.microsoft.com/en-us/research/publication/mariposa-measuring-smt-instability-in-automated-program-verification/ "Zhou et al.: Mariposa, measuring SMT instability in automated program verification"
[creusot]: https://github.com/creusot-rs/creusot "Creusot: deductive verification of Rust"
[aeneas]: https://github.com/AeneasVerif/aeneas "Aeneas: verification of Rust by functional translation"
[frama-c-wp]: https://frama-c.com/fc-plugins/wp.html "Frama-C: the WP plugin"
[why3]: https://www.why3.org/ "Why3"
[flux]: https://arxiv.org/abs/2207.04034v2 "Lehmann et al.: Flux, liquid types for Rust"
[liquid-rw]: https://ranjitjhala.github.io/static/real_world_liquid.pdf "Vazou et al.: LiquidHaskell, experience with refinement types in the real world (Haskell 2014)"
[liquid-termination]: https://ranjitjhala.github.io/static/refinement_types_for_haskell.pdf "Vazou et al.: Refinement types for Haskell (ICFP 2014)"
[whiley]: https://whileydave.com/publications/PG13_FTSCS_preprint.pdf "Pearce and Groves: Whiley, a platform for research in software verification"
[lean-about]: https://lean-lang.org/about/ "About Lean"
[lean-beans]: https://arxiv.org/abs/1908.05647 "Ullrich and de Moura: Counting immutable beans"
[lean-omega]: https://leanprover-community.github.io/mathlib4_docs/Lean/Elab/Tactic/Omega.html "Lean: the omega tactic"
[sampcert]: https://arxiv.org/html/2412.01671v3 "SampCert: verified differential-privacy samplers in Lean, deployed at AWS"
[kani]: https://arxiv.org/html/2607.01504 "Kani: a model checker for Rust (2026)"
[kani-std]: https://arxiv.org/html/2606.17374 "Verifying the Rust standard library with Kani (2026)"
[cbmc]: https://arxiv.org/abs/2302.02384 "Kroening et al.: CBMC, the C bounded model checker"
[astree]: https://www.astree.ens.fr/ "Astrée"
[astree-erts]: https://cs.nyu.edu/~pmc309/publications.www/KastnerEtAl-ERTS2-May-2010.pdf "Kästner et al.: Astrée, proving the absence of runtime errors (ERTS 2010)"
[infer]: https://6826.csail.mit.edu/2020/papers/facebook-infer-cacm.pdf "Distefano et al.: Scaling static analyses at Facebook (CACM 2019)"
