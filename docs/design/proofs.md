# Proofs: contracts, verified functions, and external provers

> **Status:** proposal (aardappel/goose#160). Phase 1, executable contracts,
> is implemented in part; §17 says what is in and how it differs from the
> text. It builds on index types and `checkfree` (spec §3.11, §7.14,
> [index_types.md](index_types.md)) and on the bounds-check analysis (§10.5,
> `src/bce.h`).

<details>
<summary>Table of contents</summary>

- [1. Why Goose is unusually easy to verify](#1-why-goose-is-unusually-easy-to-verify)
- [2. What a proof guarantees, in levels](#2-what-a-proof-guarantees-in-levels)
- [3. Contracts](#3-contracts)
- [4. The specification language](#4-the-specification-language)
- [5. Loops, recursion, and termination](#5-loops-recursion-and-termination)
- [6. Refinement types and struct invariants](#6-refinement-types-and-struct-invariants)
- [7. `abortfree fn` and `proven fn`](#7-abortfree-fn-and-proven-fn)
- [8. Ghost code and lemmas](#8-ghost-code-and-lemmas)
- [9. Trust boundaries: extern, assume, threads](#9-trust-boundaries-extern-assume-threads)
- [10. External provers: export and import](#10-external-provers-export-and-import)
- [11. How it is checked](#11-how-it-is-checked)
- [12. Diagnostics and counterexamples](#12-diagnostics-and-counterexamples)
- [13. Worked examples](#13-worked-examples)
- [14. Phasing](#14-phasing)
- [15. Prior art](#15-prior-art)
- [16. Open questions](#16-open-questions)
- [17. Implementation status](#17-implementation-status)

</details>

Goose proves memory safety and, where it can, the absence of bounds checks.
It has no way to state what a function *computes*, and no way to hand a
property it cannot prove to a tool that can. This proposal adds both:

* **Contracts** (`requires`, `ensures`, loop `invariant`, `decreases`) and
  **refinement types** (`T where P`), written in ordinary Goose expressions.
  Every contract is executable, so it is checked at run time in debug builds
  from day one, with no prover involved.
* **`proven fn`**, which the compiler rejects unless every contract it
  touches, and the absence of every abort of §9.3 other than resource
  exhaustion, is proved statically. `abortfree fn` is the weaker level
  between `checkfree` and `proven`: no aborts, no functional claims.
* **A prover pipeline**: the existing difference-constraint analysis first,
  an SMT solver second, and an **export to Lean 4 (and Why3)** with a hook
  that links a Goose obligation to an external, machine-checked proof.
* **ACSL emission** for the generated C, so the C artifact can be
  cross-checked with Frama-C instead of being the only thing anyone verifies.

## 1. Why Goose is unusually easy to verify

Most of the cost of verifying a systems language is the heap: aliasing,
framing, ownership and separation logic. Rust verifiers (Prusti, Creusot,
Verus) spend most of their machinery recovering what the borrow checker
knows. Goose removes most of that problem by construction:

| Property of the language | What it buys a verifier |
|---|---|
| No heap, no allocator (§1.2) | Program state is a finite set of named roots: globals, locals, parameters. No heap model is needed. |
| Every reference's root is static per specialization (§9.2) | A write through `p: T&` is a functional update of a known root. Verification conditions can use plain value semantics, as for SPARK's in/out parameters. |
| Values, in-place construction (§4) | `a = b` copies. There is no hidden sharing that a postcondition about `a` must account for. |
| Effects are already summarized (§10.5) | Frame conditions (`modifies`) are **inferred**, not written. "Zero lifetime annotations" becomes "zero frame annotations". |
| Whole program, monomorphized (§10.1, §10.2) | No open-world reasoning. Each specialization is verified with its concrete types and roots. Call-site facts already flow into callees. |
| Integer semantics are total and defined (§6.2) | Unsigned arithmetic wraps by definition and `%` is Euclidean, which match Lean's `UIntN` and `Int.emod` and SMT bitvectors exactly. No C UB leaks into the model. |
| Grow-only arrays never shrink (§5.1) | A fact like `i < a.len` is monotone. Index types are already a refinement type with a soundness proof (§3.11). |
| No shared memory between threads (§11.2) | No interleavings to reason about. A queue is an input or output stream. |
| Recursion is opt-in (§7.8) | Termination obligations arise only where `recursive` or a `while` loop is written. |

The proposal's central claim: in Goose, the proof that a function meets its
contract needs **no logic beyond first-order logic over values, arrays and
integers**. That fits SMT solvers well and gives a direct shallow embedding
in Lean.

## 2. What a proof guarantees, in levels

Each level includes the one before it.

| Level | Written as | Guarantees | Status |
|---|---|---|---|
| L0 Memory safety | (always) | No out-of-bounds access, use-after-free, type confusion or data race. | Implemented |
| L1 No bounds checks | `checkfree fn` | No index, slice or index-type conversion check is left. | Implemented |
| L2 No aborts | `abortfree fn` | No abort of §9.3 can fire except resource exhaustion (guard pages, native stack). That covers overflow (debug), division by zero, `pop` on empty, lossy `as`, limited-array overflow, `assert`, and every callee's `requires`. | Proposed |
| L3 Functional correctness | `proven fn` | L2, plus every contract of the function and its loops holds, and the function terminates, for every input that satisfies its `requires`. | Proposed |

A `proven` function's guarantee, stated exactly: for every argument state
satisfying `requires`, execution either returns normally with `ensures`
holding, or makes a long-distance return through it that satisfies its
`escapes` clause (§3.4), or aborts on resource exhaustion. Nothing else.
"Resource exhaustion" can be bounded as a separate later feature (§16).

The guarantee is about Goose semantics as the specification defines them.
The C compiler and Goose's code generator stay in the trusted base. §10.3
gives a way to narrow that.

## 3. Contracts

### 3.1 Syntax

Contracts go between the signature and the body, in the order `requires`,
`ensures`, `escapes`, `decreases`. Each clause takes one boolean expression.
A clause may be repeated, and repeated clauses are conjoined.

```goose
fn isqrt(n: u64) -> u64
    ensures result * result <= n
    ensures (result + 1) * (result + 1) > n
{ ... }

fn find(xs: const i64[:], key: i64) -> i64, bool
    ensures (i, found): found == any k in 0..xs.len: xs[k] == key
    ensures (i, found): found implies 0 <= i < xs.len && xs[i] == key
{ ... }
```

* `result` names a single result. A function with several results binds
  them in a pattern before the colon: `ensures (i, found): ...`.
* `old(e)` in `ensures` is `e` evaluated on entry. It is allowed on
  anything reachable from parameters, globals and captures. It is not
  allowed on locals, which do not exist on entry.
* Arithmetic inside a contract is **mathematical**, not machine arithmetic
  (§4.2), so `(result + 1) * (result + 1) > n` above means what it says
  even where the square overflows `u64`.
* `requires` and `ensures` may be written on any function. They are not
  limited to `proven` ones. What happens to them depends on the build and
  on whether they were proved (§3.3).

### 3.2 Generic functions

A generic function's contracts are checked per specialization (§7.7), the
way `checkfree` is. A contract may need operations the type parameter
provides, such as `<=`. It then holds only for instantiations whose
operations satisfy the laws the contract relies on. Laws are stated as
`lemma`s on the operation (§8), and `std.spec` states them for the builtin
types.

### 3.3 Run-time behaviour

| | Debug build | Release build |
|---|---|---|
| Proved statically | Erased | Erased |
| Not proved | Checked: aborts `goose runtime error: requires failed: <clause>` at the call site, or `ensures failed` at the return | Erased by default. `--check-contracts` keeps them. |
| In a `proven fn` | Compile error unless proved | Compile error unless proved |

A `requires` is the caller's obligation, so its check, and its error
location, belong to the caller (the call site is reported, as for library
aborts in §9.3). An `ensures` belongs to the callee.

Erasing an unproved contract in release builds is the same deal as signed
overflow (§6.2): debug builds find bugs, and release builds never pay for
checks that were not proved. A team that wants contracts as a safety net in
production turns `--check-contracts` on.

Proved contracts also **feed bounds-check elimination**: a proved `requires`
becomes an entry fact of the specialization, and a proved `ensures` becomes
a fact at every call site. So writing contracts can make code faster, which
is a better incentive than correctness alone.

### 3.4 Long-distance returns

`return e from f` (§7.9) leaves every frame between the `return` and `f`.
Those frames do not reach their `ensures`. A function that a long-distance
return may cross can state what holds when it does:

```goose
fn parse_all(text: const u8[:], out: Rec[>..]&)
    ensures out.len >= old(out.len)
    escapes out.len >= old(out.len)       // a failed parse keeps what came before
{ ... }
```

With no `escapes` clause, nothing is promised on that path except the
inferred frame. The target `f` sees the returned value as an ordinary
result, so its own `ensures` covers it.

### 3.5 Frames are inferred

There is no `modifies` clause. The effect summary that §10.5 already
computes (what each specialization may resize or overwrite, through which
parameter, in which global) is the frame. Everything outside it is unchanged
across a call, in the proof as in the analysis. A `modifies` clause may be
added later as checked documentation, but it is never required.

## 4. The specification language

### 4.1 What a contract may contain

A contract is a Goose expression that:

* has type `bool`;
* calls only **pure** functions: ones whose inferred effect summary is
  empty, which allocate nothing that outlives them, and which do not abort
  under the contract's own assumptions. Purity is inferred, not declared;
  `spec fn` (below) is for functions that exist only for specifications;
* may use the additions in this section.

### 4.2 Additions

| Form | Meaning | Executable? |
|---|---|---|
| `int`, `nat` | Ghost-only mathematical integers. Any integer in a contract promotes to `int`. | Yes, in 128 bits; a value outside that aborts with `contract not evaluable` rather than giving a wrong answer |
| `all i in lo..hi: P` | Bounded universal quantifier | Yes, O(hi - lo) |
| `any i in lo..hi: P` | Bounded existential quantifier | Yes |
| `forall x: T: P` | Unbounded quantifier | No: allowed only in `lemma`s and `spec fn`s, never in a run-time check |
| `a implies b`, `a iff b` | Logical connectives | Yes |
| `old(e)` | Entry value | Yes: debug builds snapshot what the clause reads, where an `ensures` names `old` |
| `result` | The single result | Yes |
| `spec fn` | A function usable only in specs and ghost code; may be recursive with `decreases` | Yes, if its body has no unbounded quantifier |

Bounded quantifiers over index ranges cover nearly all array
specifications. They have obvious triggers (`a[i]`) for an SMT solver, and
they run as ordinary loops in debug builds. Unbounded quantifiers are
needed for lemmas but kept out of run-time checks.

### 4.3 `std.spec`

A small standard library of `spec fn`s with proved lemmas:

```goose
spec fn sorted<T>(a: const T[:]) -> bool { all i in 1..a.len: a[i - 1] <= a[i] }
spec fn count<T>(a: const T[:], x: T) -> nat
spec fn is_perm<T>(a: const T[:], b: const T[:]) -> bool   // multiset equality
spec fn sum(a: const i64[:]) -> int
spec fn distinct<T>(a: const T[:]) -> bool
spec fn prefix<T>(a: const T[:], b: const T[:]) -> bool
```

`is_perm` is executable in O(n log n) in debug builds (sort and compare
copies on a scratch stack), so even the property that sorting routines are
proved for is checkable at run time.

## 5. Loops, recursion, and termination

### 5.1 Loop invariants

```goose
var i: i64 = 0;
while i < a.len
    invariant 0 <= i <= a.len
    invariant all k in 0..i: a[k] != key
    decreases a.len - i
{
    if a[i] == key { return i, true; }
    i++;
}
```

* `invariant` clauses go between the condition and the body of a `while`
  or `for`, as contracts go before a function body.
* A `for i in lo..hi` loop gets `lo <= i < hi` for free, and an invariant
  is checked at the head of each iteration, so a `for` loop's invariant may
  mention `i` as "elements before `i` are done".
* Invariants the analysis already infers (§10.5: `v >= 0`,
  `v <= len(P) + c`, wrap-freedom) are added automatically. A written
  invariant is only needed for what the analysis does not infer, which is
  usually the functional part.
* In debug builds an invariant is checked on entry and after each
  iteration.

### 5.2 Termination

A `proven fn` must terminate. A `for` loop always does. A `while` loop or a
`recursive fn` needs a `decreases` clause: an `int` expression that is
non-negative wherever the clause is checked and strictly smaller at each
back edge or recursive call. A tuple `decreases (a, b)` is ordered
lexicographically.

Most `while` loops in practice have an obvious measure (`a.len - i`,
`hi - lo`), and the compiler tries the inferred bounds of §10.5 as
candidates before asking for one, so a `decreases` clause should be needed
rarely.

A function that is meant to run forever (a server loop, a `thread_fn`) is
not `proven`. Its body may still call `proven` functions.

## 6. Refinement types and struct invariants

### 6.1 Refinement types

```goose
type Percent = u8 where it <= 100;
type Even    = i64 where it % 2 == 0;
type NonEmpty<T> = T[>..] where it.len > 0;
```

A refinement type `T where P` is `T` plus a predicate on the value, named
`it` as in function-value blocks. It behaves exactly like an index type
(§3.11), which this generalizes:

* **Making one.** `x as Percent` checks `P` in every build (unless proved),
  as `x as (u32 in a)` does. A value that is already known to satisfy `P`
  converts implicitly.
* **Using one.** A value of `T where P` converts implicitly to `T`, and the
  analysis and prover know `P` about it at every later point.
* **Passing through.** Fields, elements, parameters and results carry the
  type, as for index types.

`u32 in a` is, in this vocabulary, `u32 where it < a.len`, plus the rule
that `a` never shrinks, which is what keeps the predicate true after `a`
changes. A refinement whose predicate mentions mutable state other than a
grow-only length is rejected, because nothing would keep it true. That is
the same restriction index types already have, for the same reason.

### 6.2 Struct invariants

```goose
struct Span { lo: i64, hi: i64 } where lo <= hi
```

A struct invariant is a refinement on every value of the struct type. The
rule that keeps it simple and sound: **the invariant holds after every store
to a field of the struct.** A single-field write must preserve it. An update
that must temporarily break it (move `lo` and `hi` together) assigns the
whole value, `s = Span { new_lo, new_hi }`, which in-place construction
(§4.3) already makes as cheap as two field stores.

This avoids the "pack/unpack" or "valid at public boundaries only" machinery
of Dafny, Spec# and SPARK type invariants. Goose has no encapsulation
boundary to hang that on, and value semantics means there is no aliased
copy that could observe a broken invariant.

## 7. `abortfree fn` and `proven fn`

Both are function modifiers, positioned like `checkfree` (§7.14), and both
follow its rules for calls:

* every call is to a function at the same level or above, to a builtin, or
  to an `extern fn` with a contract (§9.1);
* a nested function or block written inside one is part of it;
* the obligation covers every specialization, every `simd` version and
  every inlined copy;
* under `--no-prove` nothing is verified, and the compiler warns, as
  `checkfree` does under `--no-bce`.

| | `checkfree` | `abortfree` | `proven` |
|---|---|---|---|
| Index, slice and index-type checks | proved absent | proved absent | proved absent |
| Signed overflow, division by zero, lossy `as` | allowed | proved absent | proved absent |
| `pop` on empty, limited-array overflow | allowed | proved absent | proved absent |
| `assert`, callee `requires` | allowed | proved to hold | proved to hold |
| Own `ensures`, loop invariants | not considered | not considered | proved |
| Termination | not considered | not considered | proved |
| Explicit `abort(msg)` | allowed | proved unreachable | proved unreachable |

`abortfree` is useful on its own: it is SPARK's "absence of run-time errors"
level, it needs few annotations beyond what `checkfree` code already has
(entry `assert`s become `requires`), and most of it can be proved by
extending the analysis of §10.5 with no SMT solver at all (§11.1).

`assert` keeps its meaning in all three. In `abortfree` and `proven` code it
is an obligation to prove, which also makes it the natural way to write an
intermediate proof step.

## 8. Ghost code and lemmas

Some proofs need facts that are not in the program: a running sum, a model
of what has been seen, an inductive lemma.

```goose
ghost var seen: int = 0;           // erased in every build except --check-contracts
lemma fn sorted_split(a: const i64[:], m: i64)
    requires sorted(a) && 0 <= m <= a.len
    ensures sorted(a[0..m]) && sorted(a[m..a.len])
{ }                                // proved by the solver; a body may give steps
```

* `ghost` declarations and statements are checked as code, take part in
  proofs, and are **erased** from generated C. Ghost code may not write
  non-ghost state, and non-ghost code may not read ghost state, which keeps
  erasure sound.
* A `lemma fn` is a ghost function whose `requires`/`ensures` are its
  statement. Calling it in a proof brings its `ensures` into scope. Its body
  is a proof: `assert` steps, case splits (`if`, `match`), induction (a
  `recursive lemma fn` with `decreases`), and calls to other lemmas.
* Proofs are written in Goose syntax, not a tactic language. That keeps them
  readable by Goose programmers and writable by the same LLMs that write the
  code. The external route (§10) is there for what this cannot express.

## 9. Trust boundaries: extern, assume, threads

### 9.1 `extern fn`

C is not checked. An `extern fn` may carry contracts, which are **trusted**.
A `proven` or `abortfree` function may call an `extern fn` only if it has
contracts. With no `ensures`, the `extern` gives no facts, and any `requires`
it has must still be proved at the call.

### 9.2 `assume`

```goose
assume(table.len.is_power_of_two(), "established by init_table, see #412");
```

`assume(e, reason)` adds `e` as a fact without proof. The reason string is
mandatory. In debug builds it is checked like an `assert`. An `assume` is a
hole in the proof, so it is never silent:

### 9.3 The trust report

`goose prove --trust-report` lists everything a program's proofs rest on:
every `assume`, every contract on an `extern fn`, every external proof
(§10.2) with the tool and version that checked it, and every `proven`
function whose verification was skipped. A CI job can require the list to be
empty, or unchanged. This matters most for generated code: an LLM that
"proves" a function by adding an `assume` shows up in the diff of the trust
report.

### 9.4 Threads and queues

Threads share no memory (§11.2), so a `proven` function never needs to
reason about interference. Values that arrive through `qget` are
unconstrained inputs unless the queue's element type is refined, in which
case the refinement is checked by the sender (as `as` would be) and known by
the receiver. Protocol-level properties across threads are out of scope.

### 9.5 Pools and stale references

A stale reference into a `reusable` pool or a limited array reads *some*
value of the right type (§9.4). The proof model says exactly that: such a
read is an arbitrary value of the type, satisfying the type's refinement.
Proofs that rely on what a slot holds must not go through a reference that
may be stale, which the checker's provenance already tells apart.

## 10. External provers: export and import

SMT is automatic but incomplete. Nonlinear integer arithmetic, deep
induction and mathematical facts (a hash function's distribution, the
correctness of an FFT against the DFT) need an interactive prover. The
current gap is that there is no hook for one.

### 10.1 Export

```
goose prove --export lean out/lean   prog.goose
goose prove --export why3 out/why3   prog.goose
```

Lean export produces a Lake project:

* `Goose/Prelude.lean`: Goose's semantics. Integers are `UInt8`..`UInt64`
  and `Int8`..`Int64` with Goose's operations (wrapping unsigned, Euclidean
  `%`, signed overflow as an obligation), arrays are `Array T`, a slice is
  an array plus bounds, optionals are `Option`.
* One module per Goose module. Each function becomes a Lean `def` by
  **shallow embedding**: reference parameters become in/out values (the
  function returns the updated roots), which is valid because every root is
  static (§1). Loops become well-founded recursion on their `decreases`
  measure.
* Each obligation the SMT pass could not discharge becomes a `theorem` with
  the obligation as its statement and `sorry` as its proof, and a stable
  name derived from the source (`Sort.insertion_sort.ensures_1`).

Why3 export targets WhyML for teams that want multiple SMT solvers and
Coq/Isabelle as back ends. Why3 is the route SPARK already uses.

### 10.2 Import: linking an external proof

```goose
proven fn fft(x: complex[:], out: complex[>..]&)
    ensures out.len == x.len
    ensures all k in 0..x.len: approx_eq(out[k], dft(x, k), 1e-9 * x.len)
        by external "proofs/lean/Fft.lean:fft_matches_dft"
{ ... }
```

`by external "<file>:<theorem>"` on a clause (or on an `assert` or a
`lemma fn`) marks that obligation as proved by a named external theorem.
The compiler:

1. regenerates the obligation's statement and checks that it matches the
   theorem's statement, by a hash recorded in the export. A proof of a stale
   statement is rejected, so changing the code invalidates the proof
   instead of silently keeping it;
2. runs the external checker (`lake build`, `why3 replay`) if
   `--check-external` is given, or else trusts a cached result keyed by the
   statement hash and the checker's version;
3. lists the link in the trust report.

### 10.3 Checking the C artifact too

`goose --emit-acsl` annotates the generated C with ACSL contracts derived
from Goose contracts (`requires`, `ensures`, `loop invariant`,
`loop variant`) and with the memory facts Goose already proved (`\valid`
ranges for each data stack and slice). Frama-C's WP plugin can then verify
the C directly. This does not replace Goose-level proofs, but it moves the
Goose code generator out of the trusted base for the functions where it
succeeds, which is the "verify the generated C separately" path that is
needed today, with the specifications written once in Goose instead of by
hand in C.

## 11. How it is checked

### 11.1 Pipeline

```
checked AST ──► VC generation ──► (1) difference/interval domain (bce.h, extended)
   (pre-optimization)               │ unproved
                                    ▼
                                 (2) SMT (SMT-LIB 2: z3 or cvc5)
                                    │ unproved
                                    ▼
                                 (3) external proof link, or error / run-time check
```

* VCs are generated from the **checked, pre-optimization** AST, which is
  the semantics the specification defines. Bounds-check elimination keeps
  running after optimization as it does now. The two meet through facts:
  proved contracts are handed to BCE as entry and call-site facts (§3.3).
* **Stage 1** reuses `src/bce.h`. Its difference constraints, intervals,
  `Lin` bases and inferred invariants already prove most of `abortfree`'s
  obligations: overflow freedom is "the interval fits the width", division
  by zero is "the divisor's lower bound is at least 1", `pop` on empty is
  `len >= 1`. Only obligations it cannot decide go further.
* **Stage 2** encodes each remaining VC in SMT-LIB with integers as
  bitvectors at their width (or as unbounded `Int` where the analysis
  already proved no wrap, which solvers handle much better), arrays as SMT
  arrays plus a length, and bounded quantifiers with `a[i]` triggers.
  Function bodies are opaque to callers; only contracts cross calls, so the
  work is modular per specialization.
* **Determinism.** Solver runs use a resource limit (`rlimit`), never a
  wall-clock timeout, so a proof succeeds or fails the same way on every
  machine. Results are cached in the build directory by the hash of the VC
  and the solver version, so an unchanged function is never re-proved,
  which matters for whole-program compilation.
* **Solver distribution.** Goose bundles TinyCC; it should not bundle Z3.
  The solver is found on `PATH` or given with `--solver`. Without one,
  stage 1 still runs, `abortfree` works as far as it reaches, and a
  `proven fn` that needs stage 2 fails with a message saying a solver is
  required.

### 11.2 Proof stability

SMT-based verifiers are known for proofs that break on unrelated changes
(Dafny, F*). Mitigations, from experience there:

* bounded quantifiers only in executable contracts, with fixed triggers;
* `spec fn` bodies are **opaque** by default outside their module, and
  `reveal f` exposes one inside a proof;
* each obligation is a separate solver query with only the facts its
  function's contracts and the path give;
* the resource-limit count per query is reported by `--prove-stats`, so a
  proof that is close to the limit is visible before it breaks.

## 12. Diagnostics and counterexamples

A failed obligation names the clause, the path, and, when the solver gives
a model, a **concrete counterexample in Goose terms**:

```
sort.goose:14:13: error: postcondition might not hold
    ensures sorted(result)
            ^~~~~~~~~~~~~~
  on the path: loop at 9 exits after 2 iterations, return at 21
  counterexample:
    a = [3, 1, 2]
    after the loop: i = 3, result = [1, 3, 2]
  the loop invariant at 10 does not mention the elements after j;
  invariant `all k in 1..i: result[k-1] <= result[k]` would be needed
```

Because every contract is executable, a counterexample can also be emitted
as a test: `goose prove --emit-tests` writes a `test` that calls the
function with the counterexample and checks the contract at run time
([testing.md](../testing.md)). That gives a person, or an LLM in an edit
loop, something to run, not just a solver's verdict.

## 13. Worked examples

### 13.1 Binary search

```goose
import std.spec;

proven fn lower_bound(a: const i64[:], key: i64) -> i64
    requires sorted(a)
    ensures 0 <= result <= a.len
    ensures all k in 0..result: a[k] < key
    ensures all k in result..a.len: a[k] >= key
{
    var lo: i64 = 0;
    var hi = a.len;
    while lo < hi
        invariant 0 <= lo <= hi <= a.len
        invariant all k in 0..lo: a[k] < key
        invariant all k in hi..a.len: a[k] >= key
        decreases hi - lo
    {
        let mid = lo + (hi - lo) / 2;      // the overflow-free midpoint is proved, not hoped for
        if a[mid] < key { lo = mid + 1; } else { hi = mid; }
    }
    return lo;
}
```

The bounds, the midpoint's freedom from overflow and termination are stage 1
obligations. The quantified invariants need the solver, with
`sorted` unfolded at `a[mid]`.

### 13.2 Insertion sort, in place

```goose
proven fn insertion_sort(a: i64[:])
    ensures sorted(a)
    ensures is_perm(a, old(a))
{
    for i in 1..a.len
        invariant sorted(a[0..i])
        invariant is_perm(a, old(a))
    {
        var j = i;
        while j > 0 && a[j - 1] > a[j]
            invariant 0 <= j <= i
            invariant sorted(a[0..j]) && sorted(a[j..i + 1])
            invariant j < i implies a[j] < a[j + 1]   // abbreviated; the full proof has one more
            invariant is_perm(a, old(a))
            decreases j
        {
            swap(a, j - 1, j);                  // std: ensures is_perm and the two-element exchange
            j--;
        }
    }
}
```

### 13.3 A refined ring buffer

```goose
struct Ring { head: u32, len: u32 } where len <= RING_CAP
const RING_CAP: u32 = 1024;
var ring_data: u64[1024];
var ring: Ring = Ring { 0, 0 };

abortfree fn ring_push(x: u64) -> bool
    ensures result == (old(ring.len) < RING_CAP)
    ensures result implies ring.len == old(ring.len) + 1
{
    if ring.len == RING_CAP { return false; }
    ring_data[(ring.head + ring.len) % RING_CAP] = x;   // % is Euclidean: in range, no check
    ring.len = ring.len + 1;                            // keeps len <= RING_CAP: proved
    return true;
}
```

### 13.4 What index types become

The CSR breadth-first search of [index_types.md §7](index_types.md#7-worked-example-csr-breadth-first-search)
compiles unchanged. Index types are a special case of §6.1, and `checkfree`
is a special case of `abortfree` that ignores everything but bounds checks.
No existing program changes meaning.

## 14. Phasing

Each phase is useful without the ones after it.

1. **Executable contracts.** `requires`, `ensures`, `old`, `result`,
   `invariant`, bounded quantifiers, `implies`, `std.spec`, run-time checks
   in debug builds, `--check-contracts`. No prover. Contracts become test
   oracles immediately, including for LLM-written code.
2. **`abortfree fn`.** Extend `bce.h` to the other aborts of §9.3 and to
   `requires` at call sites. No solver. Proved `requires` start feeding BCE.
3. **SMT and `proven fn`.** VC generation, SMT-LIB back end, `decreases`,
   ghost code, `lemma fn`, counterexamples, caching, `--prove-stats`.
4. **Refinement types and struct invariants**, with index types re-expressed
   as a refinement (no change in behaviour).
5. **External provers.** Lean and Why3 export, `by external`, the trust
   report, `--emit-acsl`.

Phases 1 and 2 are mostly front-end and analysis work in the existing
compiler. Phase 3 is the large one: a VC generator over the checked AST and
an SMT encoding, comparable in size to `bce.h` itself.

## 15. Prior art

* **SPARK / GNATprove** (Ada). The closest model: no-aliasing value
  semantics, absence of run-time errors as a distinct level (Silver) below
  functional correctness (Gold), contracts executable at run time, Why3 as
  the back end. Goose's static roots give it what SPARK gets from banning
  aliasing, without the ban.
* **Dafny.** Contracts, ghost code, lemmas as methods, SMT through Boogie.
  The source of the proof-stability lessons in §11.2.
* **Verus, Creusot, Prusti** (Rust). Show that contracts on a systems
  language work, and how much effort goes into modelling ownership, which
  Goose does not need.
* **Liquid Haskell, F\* refinements.** Refinement types decided by SMT. §6.1
  is a deliberately small version.
* **Lean 4.** The export target for proofs SMT cannot do. Its `UIntN` and
  Euclidean `Int.emod` match §6.2.
* **Frama-C / ACSL.** The target of §10.3.
* **Low\*/KaRaMeL.** Verified code compiled to C, showing a proof about the
  source language plus a trusted C step is accepted in practice
  (HACL\*, used in Firefox and Linux).

## 16. Open questions

* **Floating point.** Contracts over `f32`/`f64` need either SMT
  floating-point theories (slow) or a real-number model with explicit error
  bounds. Leaving floats as uninterpreted values in phase 3 and handling
  them in Lean seems right.
* **Bounded resources.** Proving a program never hits a guard page needs a
  bound on data-stack use per call. Stack assignment is static (§10.3), so
  this may be feasible for non-recursive code, as a later level.
* **Contracts on case functions** (§8.2): one contract for the overload set
  or one per case, and whether cases must refine a shared contract as
  virtual methods do.
* **Coroutines** ([coroutines.md](coroutines.md)): what an invariant across
  a `yield` means.
* **`from_bytes` and refinements.** A verified image could also check
  refinement predicates of its element type, making refined data safe to
  load. That extends the verifier, and its cost needs measuring.
* **Spelling.** `all`/`any` versus `forall`/`exists` in executable
  contracts, and whether `implies` should be `==>`.

## 17. Implementation status

Phase 1 is implemented in part. No prover runs: every clause is a run-time
check.

**What is in.**

* `requires` and `ensures` on any function with a body, `result`, `old(e)`,
  `invariant` on `while` and `for` loops, `implies`, and the bounded
  quantifiers `all`/`any i in lo..hi: p`.
* The contract words are keywords only where a clause can start or a
  contract expression can continue. Elsewhere they stay ordinary names.
* The parser keeps the clauses beside the code they describe, so `--dump`
  prints them as written and roundtrips. `LowerContracts`
  (`src/contracts.h`) runs once, before typecheck, and turns each clause
  into a check in the body:
  * `requires` at the top of the body;
  * `ensures` before every return, the end of the body included, with `old`
    values bound on entry;
  * `invariant` at the top of every iteration.

  A returned variable is checked in place and anything else is first bound
  where it is built, so nonfixed results are not copied.
* A check is a call of a builtin whose name no program can write. Codegen
  emits it under `#if GS_CONTRACTS`, which `runtime.h` sets to `GS_DEBUG`
  unless it is defined. So `--debug` checks contracts, and
  `--check-contracts` (`-DGS_CONTRACTS=1`) checks them in a release build.
  A failure aborts with `goose runtime error: <kind> failed: <clause text>`
  and the clause's location.
* The checks give the checker and the analyses no facts: they narrow nothing
  and are not bounds-check facts, since a release build runs without them.

**Where it differs from the text above.**

* Contract arithmetic is the program's arithmetic, not mathematical (§3.1,
  §4.2). There is no `int`/`nat` yet.
* A failed `requires` is reported at the clause, not at the call site (§3.3).
* `ensures` on a function with several results, and the `ensures (a, b):`
  binding, are not supported. Neither are `escapes`, `decreases`, `spec fn`
  and `std.spec`.
* A function with `ensures` and no `->` must not end in a value, since
  whether a tail is the result is not known before typecheck.
* A bare `return;` that is not a statement or a block's tail, in a function
  with `ensures`, is an error that asks for `{ return; }`.
* Quantifiers are allowed only in contracts, because there is no `spec fn`
  to name one in yet.
* `old(e)` copies are made in every build, even where nothing checks them.
  They are dead in a release build, and the C compiler removes those of
  fixed-size values.
* The contract checks are part of a `checkfree` function's text, so a bounds
  check left in a clause counts against it, even though a release build
  never runs the clause.

**Tests.** `test/typing/contracts.goose` checks every form in a debug run
whose output matches the other runs. `test/runtime/contract_*_abort.goose`
check failures under `GS_CONTRACTS=1`, and `contract_unchecked.goose` checks
that a release build runs past false clauses. `test/errors_tc/contract_*`
cover the rejections.
