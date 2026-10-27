# Compiler analyses: inventory and simplification options

<details>
<summary>Table of contents</summary>

- [1. Summary](#1-summary)
- [2. What is tracked where](#2-what-is-tracked-where)
- [3. Inventory](#3-inventory)
- [4. Per-analysis notes](#4-per-analysis-notes)
- [5. Cross-cutting findings and recommendations](#5-cross-cutting-findings-and-recommendations)
- [6. Sequencing and validation](#6-sequencing-and-validation)
- [7. Status](#7-status)

</details>

This is step 1 of the effort to stop the fix-a-hole, add-a-flag cycle in
the checker's analyses. This document inventories every analysis the
compiler runs, what each one tracks and how, roughly how much code it is,
how solid it is, and where a different representation would cover the
language more conclusively. The ranked recommendations are at the end.

Numbers are as of master `bfea86b` (2026-09-24). Line counts are of
function bodies, attributed by function using a throwaway scan of the
function definitions in `src/typecheck*.h`, `bce.h`, `optimize*.h` and
`codegen*.h`, so they are rough. The state each analysis keeps is listed
separately, since that is where the growth actually happens.

## 1. Summary

* The compiler runs about 20 distinct analyses. The lifetime family
  (provenance, read-back, stores, shrinks, growth, recursion, return
  roots, specialization keys) is ~5,000 lines of checker function bodies
  plus ~400 lines of state declarations. It accounts for nearly all of the
  churn: of the 60 most recent commits, about 30 fix a construct one of
  these analyses did not cover, and another dozen refactor the same code.
  BCE (~2,300 lines) and the optimizer's and codegen's small analyses are
  structurally sound and rarely churn.
* The code quality is not the problem. The comments are unusually good and
  the style is consistent. The churn comes from four architectural choices:
  1. **A reference's provenance is one root plus patch bits.** A value that
     may point at several places is collapsed to its innermost root
     (`InnerRoot`), and what the collapse lost is carried as side bits
     (`intogs`, `cyclelocal`, `hidesclass`, `rootfrom`, `slotread`,
     `holderroot`, ...). Every bit must be propagated at ~25 transformation
     points, and every new construct is a new chance to drop one. This is
     the single largest source of fixes.
  2. **Bodies are checked once, inside the call that first reaches them.**
     A call back into a cycle reaches a callee whose check is unfinished,
     so every interprocedural analysis has a second, syntactic "callee
     still being checked" path, plus take-back and settle machinery (cycle
     return-root prediction, threaded classes, assumed-balanced shrinks,
     cycle sites). Roughly 1,500 lines exist only for this.
  3. **Three passes keep three models of storage and callee effects.** The
     checker (`NoteRootEvent`, `ShrinkTargets`, `RootCandidates`,
     `MayAliasRoots`, `NamedOutside`), BCE (`UltOf`, `Reach`,
     `AffectedByWrite`, `Effects`) and codegen (`CollectSpecs`, `RefTopsOk`,
     `SyncReach`) each answer "what may this reach / alias / mutate" with
     their own call-graph traversal.
  4. **Facts are pushed into state at many sites instead of derived from
     the tree.** Held temporaries, growth logs, liveness, loop-assigned
     names and pending shrinks are maintained by calls scattered over the
     checker; a construct without the call is a hole.
* Top recommendations, in order of payoff per effort (section 5 has the
  details):
  1. Replace the single root with an explicit set of alternatives and
     derive the patch bits from it.
  2. Derive "what is live in this statement" and "what is used after this
     point" from the tree with one walker each, instead of maintained
     stacks.
  3. Split lifetime analysis out of type checking and run it as a
     per-cycle fixpoint over summaries, which deletes the in-progress
     machinery.
  4. Use one storage/effect model (regions), shared by the checker, BCE
     and codegen.
  5. Move class comparisons out of the specialization key into call-site
     obligations.

## 2. What is tracked where

This is the analysis state, grouped by owner. A fix touches the fields
below.

| Owner | Fields | Purpose |
|---|---|---|
| `Prov` (ast.h, 11 fields) | `root`, `rootexact`, `rootfrom`, `writable`, `reusable`, `byteview`, `intogs`, `cyclelocal`, `hidesclass`, `slotread`, `reached` | where a reference points, and what the merge or read-back lost |
| `Val` (adds 13) | `holderroot/holderexact/holderset/holderfrom`, `lvalue`, `storagebranches`, `implicitcopy`, `nonneg`, `unsized/unsizedparam`, `strlit/emptyarr/isnull` | holder contents, copy semantics, constants |
| `VarDef` (17 analysis fields) | `ref` (a `Prov`), `refrootknown`, `refprebound`, `refidentityused`, `contentroot/contentexact/contentset/contentbyteview`, `rootalias`, `classfrom`, `poolclass`, `classpool`, `growshrink`, `viewslot`, `captured`, `markof`, `nonneg` | a variable's binding, its contents, and what a class root stands for |
| `RootArg` (14) | `cls`, `depth`, `depthkey`, `writable`, `reusable`, `growshrink`, `byteview`, `viewslot`, `intogs`, `exact`, `heldexact`, `concrete`, `via`, `pool` | the specialization key's view of one argument |
| `FnSpec` summaries (17) | `shrinkexternals/params`, `shrinkexternalbounds/parambounds`, `unbalancedshrink`, `liveshrinks`, `growexternals/params`, `classevents`, `eventstart`, `retroots`, `reboundoptionals`, `narrowedenv`, `needs`, `neededges`, `litadapts`, `litflows` | what a body does that its callers must replay |
| `RetAlt`/`RetRoot` (19) | `alts`, `pred`, `seeded`, `predunknown`, `predlost`, `used*` (6), `usedthreads`, `byteview`, `set` | return roots and the cycle prediction's take-back state |
| `StoreEvent` (9), `LiveShrink` (10), `BoundShrink`, `ShrinkBalance` | | the store record and the caller-judged shrink pairs |
| `TypeCheck` per-pass (≈30) | `heldtemps`, `shrinkrest`, `invalue`, `inreturn`, `curdst`, `constslot`, `argpath`, `storeevents`, `pendingshrinks`, `loopassigned`, `shrinkcache`, `growcache`, `cyclestores`, `threadedclasses`, `assumedshrinks/open/specs`, `livecapture`, `guessedshrink`, `cyclesites`, `growlog`, `growconflicts`, `namedoutside`, `litchecks`, `cyclecache`, `fitnode`, `fitfail` | in-flight state |
| `BCE` | `Flow` (facts + generations), `places`, `addrof/intvars/wdecl/wbad/wkinds/relpids`, `cands/ge0/lelen/mono`, `effects`, `ifaces/sites/nsites/opaquesites`, `callees/calleesfirst` | its own domain, aliasing and summaries |
| `Optimizer` | `facts` (writes/addrof per VarDef), `consts`, `inlineinfo`, `postorder`, `viewed` | |
| `CodeGen` | `SpecInfo` (freevars, globals, needssp), `nrvo`, `toporder/growth/loopparent`, `TopCachePlan`, `cscopes` watermarks, `views` | |

`ast.h` declares 119 `bool` fields; the structs above account for about
half of them.

## 3. Inventory

Quality: **A** sound structure, holes unlikely; **B** works, some accretion;
**C** accreted, hole-prone; **D** structurally fragile. "Signal" is the
number of fixtures in `test/errors_tc` that mention the rule's spec section,
and of commit subjects since 2026-09-12 that touch the rule.

| # | Analysis | Spec | Main code | Body LOC | Quality | Signal |
|---|---|---|---|---|---|---|
| 1 | Types, generics, instantiation, overloads, dispatch, builtins, control constructs | §3, §6, §7.1–7.7, §8 | `typecheck_types.h`, `typecheck_calls.h` (resolution half), `typecheck_builtins.h` (`CheckBuiltin`), `typecheck_nodes.h`, `typecheck_flow.h` (constructs) | ~5,200 | B+ | low churn |
| 2 | Root provenance and merged values | §9.2 | `Ident::Check`, `CheckRefOf`, `DecayRef`, `SlotView`, `MergeVals`, `Hides`, `InnerRoot`, `TempRoot`, `TempCopy`, `CheckBranchRoot`, `CheckBindingRoot`, `BindProv`, `RefProvOf`, `CheckRefRebindRoot`, `PrebindLoopRefs` | ~1,000 (+`Prov`/`Val`/`VarDef` state) | C | §9.2: 91 fixtures; 22 commits mention roots |
| 3 | Read-back roots | §9.5 | `RootCandidates`, `ReadBackRoot`, `ReadBackLVal`, `ContainerRead`, `TempContents`, `CanContain`, `ReachesThroughRefs`, `VisibleVars` | ~250 | B- | 16 commits mention read-backs/slots |
| 4 | Store rule and store record | §9.2 store, §5.1 | `FitsAt` (store half, ~100 lines), `RecordStore`, `AddStoreEvent`, `NoteContentRoot`, `ApplyCalleeStores`, `ShrinkTargets`, `ClassArgRoot` | ~250 | C+ | 11 commits |
| 5 | Grow-shrink storage exclusion (taint, slot reads) | §5.2 | `GrowShrinkTaint`, `StoredIntoGrowShrink`, `IntoGrowShrink`, `GrowShrinkCanHold`, `SlotReadable`, `SlotReadMayRetarget`, `RefMayPointInto`, `RefMayRetarget` | ~110 | C | §5.2: 158 fixtures |
| 6 | Shrink checks within a body (grow-only, grow-shrink) | §5.1, §5.2 | `CheckGrowShrink`, `GrowOnlyShrinkAt`, `CheckShrinkHolders`, `CheckHeldShrinks`, `HeldRefsMayPointInto`, `HolderMayPointInto`, `ResolvePendingShrinks`, `ShrinkThrough`, `ShrinkMayFree`, `UsedAfter`, `HoldValue`/`HoldLocation`/`HoldSequence` | ~440 | C- | §5.1: 65 fixtures; 225 fixture names contain "shrink" |
| 7 | Shrink summaries, caller-judged pairs, balanced calls | §5.1 calls, §5.2 balanced | `NoteRootEvent`, `NoteShrink`, `ApplyCalleeShrinks`, `NoteLiveViews`, `NoteLiveShrink`, `ApplyCalleeLiveShrinks`, `ResolveCycleSites`, `ResizesToMark`, `SamePath`, `KeepShrinkChecks`, `SettleAssumedShrinks`, `SyntacticShrinks`, `ScanReceivers` | ~560 (+`LiveShrink`, `AssumedShrink`, `CycleSite` state) | D | 14 commits; `ApplyCalleeShrinks` alone is 183 lines |
| 8 | Growth during construction, uses during whole assignment | §1.3(4), §4.4 | `NoteGrow`, `CheckGrowsSince`, `ApplyCalleeGrows`, `SyntacticGrows`, `MayAliasRoots`, `ResolveGrowConflicts`, `CheckBuiltUses`, `ReachesBuilt`, `EachUse`, `FieldsApart`, `NamedOutside`, `BuiltInPlace`, `AppendedRun/Copies` | ~310 | B- | §1.3: 14, §4.4: 17 fixtures; 15 commits mention growth |
| 9 | Specialization keys and root classes | §3.4 (impl. notes), §10.2 | `GetOrCreateSpec` (239 lines), `CheckSpecBody` (class roots), `ClassDepth`, `EnvReach`, `SettleParamRootExactness`, `CanonRoot`, `UltimateRoot` | ~690 (+`RootArg`) | B- | key has 9 parts plus `depthkey` compared apart; `exact`/`concrete` ANDed after the fact |
| 10 | Recursion: cycles, cycle return-root prediction, threaded and pool classes, cycle stores | §7.8 | `typecheck_cycles.h` (all, 690 lines), `ValidateCycle`, `JoinCycle`, `ValidatePoolArgs`, `ValidateThreadArgs`, `NoteThreadedClass`, `ThreadStorable`, `RelyOnThread`, `Unthread`, `CycleStorable`, `CheckCycleInit`, `InProgressRebinds`, the `inprogress` branches of every `ApplyCallee*` | ~770 (+~600 inside other functions) | D | §7.8: 58 fixtures; 12 commits |
| 11 | Return roots, inferred results, long-distance returns | §9.2 return, §7.9 | `RecordReturn`, `RetAltVal`, `CallResult`, `CheckReturn`, `CheckInferredResult`, `ValidateNeeds`, `AddNeed` | ~360 | B- | 4 commits this week |
| 12 | Flow state: definite assignment, optional narrowing | §3.8 | `SaveFlow/RestoreFlow/MergeFlow`, `NarrowCond`, `CollectAssignedNames`, `KillNarrowingsAssignedIn`, `FinishLoopNarrowing`, `ApplyCalleeRebinds`, `ExternalOptionals` | ~250 | B | name-based loop rule; snapshot per branch is O(vars) |
| 13 | Copy semantics and reference transparency | §4.1, §3.8 | `CheckValue`, `DecayRef`, `KeepsRef`, `AutoRef`, `BindsRef`, `ImplicitCopy`, `CheckBranchCopy`, `BindBranchesByRef`, `UnrefForValueParam`, `IsOwnLocal`, `inreturn`, `argpath` | ~170 | B- | §4.1: 35 fixtures; flags `argpath`, `storagebranches`, `implicitcopy`, `inreturn`, `branchcopy`, `inferred`, `synth` |
| 14 | Writability | §9.5 | `constslot`, `Prov::writable`, `PointeeWritable`, `WholeWritable`, `NoLetAssign`, `NoCopyWrite`, `RootArg::writable` | ~60 | A- | §9.5: 62 fixtures (mostly read-back, not constness) |
| 15 | Relative references and pools | §3.9 | `ResolvePools`, `PoolOf`, `ValidatePool`, `FitsAt` (relative half), `CheckSelfInit`, `RootedAtReceiver`, `classpool` | ~130 | A- | §3.9: 29 fixtures, stable |
| 16 | Format overload effects | §3.7 | `CheckPrintable`, `CheckRenderable`, `UserFormatIn`, `heldtemps.render`, `shrinkrest` | ~110 | B- | 8 commits |
| 17 | Literal parameters | §7.7 | `RecordLitAdapt`, `NoteLitArgs`, `VerifyLiterals`, `LitAdapt/LitFlow` | ~50 | B+ | stable |
| 18 | Held temporaries and liveness (support for 6–8) | §5.1 | `heldtemps`, `TempScope`, `shrinkrest`, `RestScope`, `blockpos`, `UsedAfter`, `MentionsName`, `invalue`, `loopassigned` | ~120 | C+ | pushed at ~12 sites |
| 19 | Bounds-check elimination | §10.5 | `bce.h` | ~2,300 | A- | 8 commits total; the two recent ones fixed operand types |
| 20 | Optimizer facts: reachability, writes/addr-of, constants, inlining classification, views of copies, base-case and TRE eligibility | §4 (impl. notes) | `Reach`, `Analyze`, `Scan`, `OptViewed`, `NamesStorage`, `CodeFree`, `BaseOK`, `Pure`, `RootStable`, `Rebindable` | ~230 of ~2,000 | B+ | `OptViewed` is a patch for a checker decision the tree does not record |
| 21 | Codegen: free variables, globals and `gs_sp` need (fixpoint), stack assignment, top caching, NRVO, evaluation snapshots | C.2, C.3, §6.10 (impl. notes) | `CollectSpecs`, `PlanTopClasses`, `RefTopsOk`, `PlanTopCaches`, `ExpandTopMarkers`, `SyncReach`, `DetectNrvo`, `NamedResult`, `Snapshot`, `AllocStk/SaveBase`, `HoistAggregateDecls` | ~520 of ~9,500 | B (marker expansion C) | 17 commits mention stacks/caching |

Beyond the code itself, `docs/implementation.md` needs 800 lines
(§3.4–3.11) to describe analyses 2–11, `test/errors_tc` holds 647 fixtures
(8,400 lines), and `test/lifetimes` holds 51 (2,400 lines). Any rework
relies on that test suite.

## 4. Per-analysis notes

Each entry covers what the analysis tracks, how it does so, where the holes
come from, and the option.

### 4.1 Root provenance (2)

**Tracks.** For every reference-like value: the variable whose scope bounds
the pointee (`root`), whether that variable owns it (`rootexact`), and the
ordering "A outlives B iff `depth(A) <= depth(B)`" along the compile-time
call path. Temporaries get a `VarDef` of their own one scope deeper.

**How.** Each value has one root. When branches, rebinds, call returns, or
dispatch arms give a value several possible roots, `InnerRoot` keeps the
innermost one. Additional fields record information that this choice would
otherwise lose:

* `intogs`: the value may point into a grow-shrink array that the kept root does
  not hold.

* `cyclelocal`: it may be rooted where a cycle stores nothing.

* `hidesclass`: it may be a parameter class's pointee.

* `rootfrom`: the container an inexact read-back came from, used for diagnostics
  and the store record's `src`.

* `slotread`: every alternative was loaded from storage.

The record also includes `byteview` and `reached`. Holder values carry a second
root (`holderroot`), and variables carry a third (`contentroot`).

**Holes.** Each bit has to survive ~25 transformations: `MergeVals`,
`DecayRef`, `SlotView`, `ReadBackLVal`, `DerefLValue`, `ContainerRead`,
`RetAltVal`, `CallResult`, `RecordReturn`, `CheckRefRebindRoot`,
`GetOrCreateSpec` (into `RootArg`), `CheckSpecBody` (into class roots),
`TempCopy`, `NoteLitElem`, `CheckFor`/`CheckMatch` binders, `FitsAt` (whole
array to slice), `LoadSliceArgs`, `HoldValue`, `AppendedCopies`,
`CheckBreak`, `TryDispatch`, `Ident::Check`, `CheckRefOf`. The commits
"keep what a merged value may be when only one root is kept" (365b87c),
"root a construct's by-value result at a temporary of its own" (b7c6de1),
"root a function value's result at a temporary" (bed55a1) and the
`goose-provenance-consumers` checklist all belong to this class of hole.
`RetRoot::alts` and `CycleRoots::RootSet` are already sets; `CallResult`
maps each alternative and then collapses them with `MergeVals` again.

**Option.** Make the alternative set the representation: `Prov` holds a
small vector of `{root, exact, bound-type}` and merging is set union. The
bits become queries over the set at the consumer: "may point into a
grow-shrink array" is "some alternative's root is one", "cycle-storable"
is "every alternative is", "hides a class" is "some alternative is a class
root", `rootfrom` is the alternative's own record. The outlives check loops
over alternatives. This removes five flags and their propagation, and any
new construct that merges values gets the right answer from the set union
without extra code. `holderroot` can be the same set on the holder's
contents. This is the prerequisite for every other recommendation.

### 4.2 Read-back roots (3)

**Tracks.** Where a reference loaded out of a container points.

**How.** The root is re-derived at the load by enumerating the variables in
scope whose type can hold the pointee by value (`RootCandidates` over
`CanContain`), plus globals, static data, and parameter class roots whose
references lead there. The deepest candidate is taken, and it is exact only
when it is unique. A temporary answers with its holder root.

**Assessment.** The analysis is principled and small. Its weakness is that
it is a type-based approximation consulted at the use. It reasons about the
scope as it is at the use rather than about what was stored, while the
store record (4.3) records exactly what was stored. The two are reconciled
by hand in `RecordStore` (`src` via `rootfrom`) and `HolderMayPointInto`.

**Option.** With root sets (4.1) and a per-container contents set kept by
the store record, a read-back from a container that is a local of the
activation is that container's contents set. The type-based enumeration is
the fallback for containers the activation cannot see into (parameters,
globals). The rule stays the same, with one source of truth.

### 4.3 Store rule and store record (4)

**Tracks.** That a stored reference outlives its destination. For the
shrink rules, it also tracks every store of a reference or holder into a
container (`storeevents`, a program-wide list; per-container
`contentroot`).

**How.** `FitsAt` runs the rule for every value meeting a typed slot,
against every storage an inexact destination may stand for
(`ShrinkTargets` over `Dest::reached`). A store through a parameter's class
is kept on the specialization (`classevents`) and replayed at each call
(`ApplyCalleeStores`), where it is widened again. A back edge
conservatively records every reference argument as stored into every
container argument.

**Assessment.** The rule is fine. The record is a flat list scanned
linearly per query (`HolderMayPointInto`, `EachHolderRoot`), with `src`
chains to follow copies. `reached` and `bound` were added this month to
widen inexact destinations. Two recent fixes (3362c37, 12e291b) were about
which storages an inexact destination enumerates.

**Option.** Keep the record, but as per-container contents sets (the same
set type as 4.1) updated at stores, with a copy recorded as a reference to
the source container's set. `ShrinkTargets` and `RootCandidates` then
become one function: "the storages a root of type T may stand for".

### 4.4 Grow-shrink storage exclusion (5)

**Tracks.** That no reference that may point into a grow-shrink array is
ever stored, so the §5.2 shrink scan can look at variables only.

**How.** `GrowShrinkTaint` checks the root, then `intogs`, then a reference
to a slice's slot. `slotread` says a value came out of storage and is
therefore clean. `SlotReadMayRetarget` and `RefMayRetarget` re-admit `var`
references that a later rebind in a loop could move.

**Assessment.** The analysis is small, but it is the most recent class of
holes (`slotread`: 1a06b13; `viewslot`; `intogs` on keys). All of it comes
down to "which alternatives does this value have" again.

**Option.** The analysis is absorbed into 4.1. Taint becomes "some
alternative is a grow-shrink root" and cleanliness becomes "every
alternative was read out of storage", both derived from the set.

### 4.5 Shrink checks within a body (6)

**Tracks.** At a shrink, every live thing that may point into the array:
held temporaries of the statement, reference and slice variables in scope
(and what a reference to a slice or holder reaches), holders by their store
record, and, once the whole program has been checked, other globals by
theirs (`CheckGlobalShrinks`).

**How.** There are three scans (`CheckHeldShrinks`, `GrowOnlyShrinkAt` over
`vars`, `CheckShrinkHolders` over `VisibleVars`), each with its own case
analysis of "may point into" (`RefMayPointInto`, and
`HeldRefsMayPointInto`, which alone has eight outcomes). Liveness comes
from `UsedAfter` (syntactic mention after the statement, `shrinkrest`,
loops), and `pendingshrinks` handles stores later in a loop body.

**Assessment.** This is the most patched area. Grow-only and grow-shrink
arrays use different scans, byte views and slot reads are exempted from
each, and "through an inexact root" multiplies the scan by `ShrinkTargets`.

**Option.** Use one scan: for each live view (variables plus the statement's
earlier operands, see 4.14), intersect its alternative set with the shrunk
array's alternative set. Grow-only versus grow-shrink then differs only in
whether holders' contents sets are included. The eight-way case analyses
collapse into set intersection plus the two existing exemptions (byte
views, relative references).

### 4.6 Shrink summaries, caller-judged pairs, balanced calls (7)

**Tracks.** What a body shrinks (by parameter, external, or bound type),
which still-used views only its callers can tell apart from the shrunk
array (`liveshrinks`), and which shrinks are balanced resizes.

**How.** Summaries are kept on `FnSpec` and applied at calls by
`ApplyCalleeShrinks` (183 lines), which expands them, judges balance
against aliasing, and either scans or defers. Pairs are mapped through
arguments at every call and again when a cycle closes (`cyclesites`). A
back edge is assumed balanced. Its skipped checks are run anyway, with
errors captured by catching `CompileError` (`KeepShrinkChecks`), and are
settled when the cycle closes.

**Assessment.** This area is structurally fragile. It has three interleaved
deferral mechanisms (pending shrinks, cycle sites, assumed shrinks), error
capture by exception, and a `guessed` bit for wording. `liveshrinks` is the
right idea (record an obligation, let the caller judge), but it is applied
to one rule only.

**Option.** Make obligations the general mechanism (section 5, item 5) and
compute summaries to a fixpoint per cycle (item 3), which removes the
deferrals. A summary then consists of the shrunk regions with their
balance, and the pairs.

### 4.7 Growth during construction, uses during whole assignment (8)

**Tracks.** That nothing grows an array while a value is built in place in
it, and that a whole assignment's right-hand side does not use the array it
rebuilds.

**How.** A per-activation log (`growlog`) is checked against the
constructed root after the expression (`CheckGrowsSince`), using
`MayAliasRoots`. Conflicts between two parameter classes are deferred to
the end (`growconflicts`, `ResolveGrowConflicts`). Callees contribute
`growparams`/`growexternals`, or, while still in progress, their text.
`CheckBuiltUses` walks the right-hand side and every callee's
`NamedOutside`.

**Assessment.** The analysis is reasonable. `MayAliasRoots` is a fourth
alias oracle (beside `RefMayPointInto`, BCE's `AffectedByWrite`, codegen's
`RefTopsOk`).

**Option.** Compute aliasing over alternative sets, and treat growth as one
more effect in the shared summary (item 4).

### 4.8 Specialization keys and root classes (9)

**Tracks.** Which call sites may share one checked body.

**How.** The key consists of types, bindings, literal parameters, function
values, narrowed environment, `needs`, and per argument a `RootArg` with
class number, `depthkey`, writability, pool, and four grow-shrink and view
bits. `exact` and `concrete` are ANDed across sites afterwards and settled
by `SettleParamRootExactness`. A back edge reuses the in-progress body
whatever its roots.

**Assessment.** The code is well commented, but the key mixes two
strategies: facts the body needs to answer comparisons inline
(`depthkey`, `growshrink`, `heldexact`, `viewslot`, `intogs`) and facts
settled afterwards (`exact`, `concrete`, `growconflicts`). Each new hole
prompts a debate about which side to put it on
(`goose-spec-key-depth-ties`, `goose-holder-param-exactness`). A larger key
also multiplies the number of bodies checked.

**Option.** This is item 5: keep types, writability and pools in the key,
and let the body record obligations over its class roots (`A outlives B`,
`A ≠ B`, `A holds no grow-shrink array`) that call sites discharge, as
`liveshrinks` does today. `depthkey`/`EnvReach` and the
`exact`/`concrete` settling are then removed.

### 4.9 Recursion (10)

**Tracks.** Which functions are in a cycle; the roots of results a back
edge returns before the returns are checked; which parameter classes a
cycle may store through (pools, threaded classes); stores made before a
function was known to be in a cycle; non-fixed locals held across a call
into the cycle.

**How.** A syntactic fixpoint over returns (`CycleRoots`, 690 lines)
predicts roots. Real returns are checked against the prediction with
take-back rules (`ReturnConflict`, `RetRoot::used*`, `predlost`). Threaded
classes are tracked with heirs, `relied` and retroactive `Unthread` errors.
`cyclestores` are refused retroactively by `JoinCycle`. Every
`ApplyCallee*` has an `inprogress` branch using syntactic scans
(`SyntacticShrinks`, `SyntacticGrows`, `InProgressRebinds`,
`NamedOutside` pending). The §7.8 conservatisms are listed in
`implementation.md` §10.

**Assessment.** This is the hardest code in the compiler, and all of it
follows from checking a body once inside the call that first reaches it.
BCE and codegen face the same recursion and solve it with a fixpoint over
summaries in ~30 lines each (`ComputeEffects`, `CollectSpecs`).

**Option.** This is item 3: run the lifetime analysis after type checking,
per strongly connected component, iterating bodies with optimistic
summaries until stable. Predictions, take-backs, threaded-class heirs,
cycle stores, assumed shrinks and every syntactic fallback are then
removed. What remains of §7.8 is the stack rule (`JoinCycle`'s
non-fixed-local check) and the pool argument rule.

### 4.10 Return roots (11)

**Tracks.** Every root a function's results may have, mapped at each call.

**How.** `RetRoot::alts` holds one `RetAlt` per distinct root, with ANDed
guarantees. `RetAltVal` maps a class to the argument's root, and
`CallResult` merges the mapped alternatives with `MergeVals`. Long-distance
returns are restricted to globals (TODO 0d). Inferred results follow `let`
rules.

**Option.** With 4.1, the result is the mapped set without merging, and
`RetAlt` and `Prov` alternatives become one type. Long-distance returns can
then use the real rule ("rooted at or above the target's frame").

### 4.11 Flow state: definite assignment and narrowing (12)

**How.** Every variable in scope is snapshotted at each branch, and the
snapshots are joined by `MergeFlow`. Loops kill narrowings of the names the
body rebinds. Those names are found by a syntactic scan that follows calls
by name (`CollectAssignedNames`) and by callee summaries
(`reboundoptionals`). `FinishLoopNarrowing` then reports an error when the
scan missed a rebind through a call.

**Assessment.** This works. The name-based loop rule and the "error if the
guess was wrong" pattern are the same symptom of a single pass over loops
as `PrebindLoopRefs` and `refidentityused`. Snapshots are O(variables in
scope) per branch, which is what makes deep call paths cost gigabytes.

**Option.** Snapshot only variables the construct touches (a prescan of the
branch, or copy-on-write), and treat loops with a two-iteration check
(item 6) instead of name scans.

### 4.12 Copy semantics and reference transparency (13)

**How.** `CheckValue` decays references unless the destination keeps them,
rewrites lvalues to `&x` (`AutoRef`, `synth`), and decides implicit copies
with `inreturn`, `argpath`, `storagebranches`, `implicitcopy`,
`branchcopy`, `inferred`. Calls check arguments twice (phase 1 without a
parameter type, phase 2 with one), and `BindBranchesByRef` re-checks
constructs at reference parameters.

**Assessment.** Several recent fixes were in the two-phase argument check
and in the handling of constructs as arguments (4c05a0f, fe901f5,
3259cd7). Six context flags for one rule is a smell.

**Option.** Decide "by value or by reference" once per argument from the
parameter type and the argument's shape (lvalue, reference, construct of
lvalues) in one function, before phase 2, and record the decision on the
node; the flags become locals of that function.

### 4.13 Writability (14), relative references (15), literal parameters (17)

These three are sound and small. Writability is a bit plus `const` in the
type, with one enforcement point (`FitsAt` under `constslot`). Relative
references need root identity and ask `rootexact`, which is the right
dependency. Literal parameters are a clean record-and-verify design. No
action is needed.

### 4.14 Held temporaries and liveness (18)

**Tracks.** Values evaluated earlier in the statement that are still live
(`heldtemps`), the parts of the statement that run after the shrink
(`shrinkrest`), whether a variable is mentioned later (`UsedAfter`).

**How.** Held temporaries are pushed at ~12 sites (`CheckArg`, index and
slice receivers, assignment locations, `Binary` operands, `==` views,
render, dispatch arguments, literal fields, returns) and popped by
`TempScope`. On every shrink, `UsedAfter` rescans blocks with
`MentionsName`, following nested functions by name.

**Assessment.** A site missing a push is a hole (7027e7f, 5896dbd,
756e1fe), and `shrinkrest` exists because `heldtemps` cannot see the
right-hand side of the statement.

**Option.** Derive both from the tree with one walker each: "the
reference-typed subexpressions evaluated before node N within its
statement, in evaluation order" and "the variables mentioned at or after
position P in the enclosing blocks", computed once per statement or body
(a backward pass gives every position's later-use set in O(n)). There are
then no push sites to forget, and evaluation order is defined in one
function shared with codegen's `Snapshot`.

### 4.15 Bounds-check elimination (19)

BCE is a self-contained difference-constraint domain with generations,
places with an ultimate owner, kills, per-spec effect summaries to a
fixpoint, entry facts from call sites, recorded invariants, and loop-view
hoisting. Its structure is sound, its gaps are documented, and it has low
churn. It consumes checker provenance (`ref.root`, `rootexact`, class
roots) and re-derives aliasing and effects for itself. No structural action
is needed; it is the model for item 3, and a consumer of item 4.

### 4.16 Optimizer analyses (20)

The optimizer's analyses are `Analyze` (writes and address-taking per
variable, flow-insensitive), reachability with use counts, inline
classification with C-nesting depth, `OptViewed` (re-materialize a value
the checker rooted at a temporary when folding reduces it to storage), and
purity checks for base-case inlining and tail-recursion elimination. They
are sound. `OptViewed` re-derives syntactically a decision the checker made
(`TempCopy`); recording "this value is a temporary copy" on the node would
let the optimizer read it instead.

### 4.17 Codegen analyses (21)

`CollectSpecs` computes free variables, touched globals and whether `gs_sp`
is needed, by a fixpoint over the call graph (the compiler's third
computation of the call graph). Top caching decides per function and class
of stack (`PlanTopClasses`, `RefTopsOk` reading
`RootArg::exact`/`concrete`), plans regions, and then rewrites the emitted
C text: `ExpandTopMarkers` parses `goto`s and labels out of the generated
code to place flushes, and `HoistAggregateDecls` moves declarations as text
to work around an MSVC bug. NRVO is a syntactic scan of returns.

**Option.** Emit into a minimal statement list with explicit loop and
flush nodes and expand from that instead of from text (medium, isolated).
Consume the shared storage summary (item 4) in `RefTopsOk` and `SyncReach`.

## 5. Cross-cutting findings and recommendations

Ranked by expected reduction in future churn per unit of effort. Effort is
a rough size in lines touched; "deletes" names what goes away.

### 1. Alternative sets instead of one root plus bits (medium effort, highest payoff per line)

Replace `Prov::root/rootexact/rootfrom/intogs/cyclelocal/hidesclass/
slotread` with a small set of alternatives `{root, exact, fromcontainer,
slotread}` and merge by union. Consumers loop over the alternatives:
outlives means every alternative outlives; taint means some alternative is
a grow-shrink root; cycle-storable means every alternative is
cycle-storable; class-hidden means some alternative is a class root.
`holderroot` becomes the contents set; `RetAlt` and `CycleRoots::RootSet`
become the same type. The change touches ~1,500 lines mechanically
(`MergeVals`, `Hides`, `InnerRoot`, `GrowShrinkTaint`, `RetAltVal`,
`CallResult`, `CheckRefRebindRoot`, `RootArg`, the scans). Deletes: five
flags, `Hides`, `InnerRoot`'s tie rule, the `intogs`/`hidesclass`
propagation in ~20 places, and the class of bugs "merged value hid X".
Risk: low; the fixture suite pins every rule. Do this first; every later
item assumes it.

### 2. Derive statement liveness and later-use from the tree (small effort)

One walker for "operands evaluated before N in its statement" replaces the
`heldtemps` pushes, `HoldValue`/`HoldLocation`/`HoldSequence`,
`shrinkrest`/`RestScope`, `render` and `invalue`; one precomputed
later-use table per body replaces `UsedAfter`/`MentionsName`/`blockpos`
rescans and `loopassigned`. This replaces ~300 lines with ~150, leaves no
push sites to forget, and states evaluation order once (codegen's
`Snapshot` should read the same function). Risk: low.

### 3. Split lifetime analysis from type checking; fixpoint per cycle (large effort, largest deletion)

Type checking keeps its single pass and creates specializations on types,
bindings, literal parameters, function values, writability and pools.
Lifetime analysis (2–11 in the inventory) then runs over the finished
specialization graph in callee-first order, iterating each recursive
cycle's bodies with optimistic summaries until the summaries stabilize, as
`BCE::ComputeEffects` and `CodeGen::CollectSpecs` already do.
Deletes: `typecheck_cycles.h` (prediction and `ReturnConflict`),
`RetRoot::pred/used*/predlost/usedthreads`, the `cycleroot` sentinel,
threaded-class heirs/`relied`/`Unthread`, `cyclestores`, `assumedshrinks`
/`SettleAssumedShrinks`/`KeepShrinkChecks`, `cyclesites`/
`ResolveCycleSites`, `SyntacticShrinks`/`SyntacticGrows`/`ScanReceivers`,
`InProgressRebinds`, `NamedOutside`'s pending case, and the `inprogress`
branch of every `ApplyCallee*`. That is around 1,500 lines, and the §7.8
conservatisms listed in `implementation.md` §10 are replaced by the spec's
real rule.
Cost: a body is analyzed more than once inside a cycle (bounded by the
lattice height, in practice two or three rounds), and the analysis needs
its own walk over checked bodies (the `Check` methods stay for types).
Risk: medium; it changes when errors are reported, not which programs are
legal, so the fixtures still decide. Prerequisite: item 1.

### 4. One storage and effect model for checker, BCE and codegen (medium effort)

Define regions for variables, class roots, temporaries, globals, and static
data. Compute one summary per specialization that records which regions it
reaches, writes, resizes (including balance), stores into (including the stored
values), grows, and returns. The summary also records rebound optionals, free
variables, and integers written.

Three sets of helpers would then query this summary: the checker's
`NoteRootEvent`/`ShrinkTargets`/`RootCandidates`/`MayAliasRoots`/`NamedOutside`,
BCE's `UltOf`/`Reach`/`AffectedByWrite`/`Effects`, and codegen's
`CollectSpecs`/`RefTopsOk`/`SyncReach`. This would remove three call-graph
traversals and four separate alias analyses. It fits with item 3, which gives
the checker its own summary fixpoint.

### 5. Obligations instead of key facts (medium effort, after 3)

Keep the specialization key to what changes the body's meaning (types,
bindings, literal parameters, function values, writability, pools,
`needs`). Let the analysis record obligations over class roots
(`A outlives B`, `A is not B`, `A holds no grow-shrink array`, `A's
contents are exactly one array`) that each call site discharges with its
concrete roots, generalizing `liveshrinks`. Deletes `depthkey`/`EnvReach`,
`growshrink`/`byteview`/`viewslot`/`intogs`/`heldexact` from `RootArg`,
`SettleParamRootExactness`, `growconflicts`/`ResolveGrowConflicts`, and
the specialization splits they cause (a function called once with a
global and once with a local today gets two bodies).

### 6. Loops: two-iteration check instead of name scans (small effort, independent)

Check a loop body twice, the second time with the bindings, stores and
narrowings that the first pass found (a widening step), and report on the
second pass. Deletes `PrebindLoopRefs`/`ResolvePrebind`, `refprebound`,
`refidentityused`, `loopassigned`/`AssignedInEnclosingLoop`,
`pendingshrinks`/`ResolvePendingShrinks`, `CollectAssignedNames`'s call
following and `FinishLoopNarrowing`'s "guess was wrong" error. Cost: loop
bodies are checked twice (nested loops 2^depth; cap at two rounds per
loop by checking inner loops once inside each outer round, or memoize).
This can be done before or after item 3.

### 7. Smaller items

* Decide by-value versus by-reference once per argument (4.12) and record
  it on the node; drop `argpath`/`storagebranches`/`implicitcopy`/
  `branchcopy`.
* Record "temporary copy" on nodes so `OptViewed` reads it (4.16).
* Emit codegen's top-cache flushes from a statement list, not from text
  (4.17).
* Snapshot flow state per construct rather than per scope (4.11).

## 6. Sequencing and validation

Order: 1 → 2 → 6 → (3 + 4 together) → 5 → 7. Items 1, 2 and 6 are each a
week-scale change that is worth doing on its own. Items 3 + 4 are the
structural rewrite, which should start only once item 1 has landed, since
the rewrite's summaries are made of sets.

Validation for every step is the existing suite: `test/errors_tc` (647
rejections with expected messages), `test/lifetimes` and `test/expected`
(positive programs whose output is fixed), samples and the standard
library, at both optimization levels and through TinyCC. A change that
reports an error at a different line, or with different wording, needs the
expectation updated. A change that accepts a program the old compiler
rejected needs a review of the fixture (many rejections in `errors_tc`
are conservatisms, and item 3 in particular will lift some). Keep a scratch
build of the pre-change compiler and diff verdicts over all fixtures and
samples before each landing (`docs/testing.md`,
`goose-branch-validation-snapshots`).

## 7. Status

* **1. Alternative sets** landed 2026-09-25: `Roots`/`RootAlt` in `ast.h`
  replace `root`/`rootexact`/`rootfrom`/`intogs`/`cyclelocal`/`hidesclass`/
  `slotread`, `Val::contents` replaces `holderroot`/`holderexact`,
  and `VarDef::contents` replaces `contentroot`/`contentexact`/`contentset`.
  Read-backs enumerate their candidates once, at the read, as exact
  alternatives (§9.5), and the cycle predictor holds each alternative of a
  return separately. `RootArg::intogs` became `gsvia`; `InnerRoot`,
  `CanonRoot` (`rootalias` was never set), `Hides` and `NoteContentRoot`
  are gone. Inside a loop, a `var` with several roots is read as bounded by
  its roots (`loopretargets`) when the loop rebinds it to a root that the
  syntactic scan cannot show it has. This is the single-pass replacement for
  what the collapsed inexact root used to guarantee; item 6 removes it too.
* **6. Loops checked to a fixpoint** landed 2026-09-25. `CheckLoopPasses`
  (`typecheck_flow.h`) re-checks a loop body, starting from the join of the
  loop's entry with the previous pass's back edges, until no fact fed back
  to the head changes (`LoopPass`, `NoteFact` at
  `BindProv`/`CheckRefRebindRoot`/`RecordStore`, and a comparison of the
  flow state). Every pass but the last is a discovery pass. In a discovery
  pass, a variable read before its binding has no roots (`RefProvOf`, and
  `RootArg::unknown` for a call given such a variable), and the shrink scans
  let it pass. Deleted: `PrebindLoopRefs`/`ResolvePrebind`/
  `refprebound`, `RefExactOf`/`refidentityused`, `PushLoopAssigned`/
  `loopassigned`/`AssignedInEnclosingLoop`/`CollectAssignedBases`,
  `loopretargets`, `pendingshrinks`/`ResolvePendingShrinks`,
  `CollectAssignedNames`/`KillNarrowingsAssignedIn`, `NarrowedOptionals`/
  `CollectCheckedRebinds`/`FinishLoopNarrowing`, `SlotReadMayRetarget`,
  `EndLoop`. Three fixtures now report at the use instead of at the rebind
  or after the loop (rebind_root_identity, optional_loop_fnval_rebind,
  optional_while_condition_value). Loops with no rebind or store of an
  outer variable take one pass; the test suite's checking time is unchanged.
* **3. Cycles checked in rounds** landed 2026-09-25, without the split of
  lifetime analysis from type checking that the item proposed. The fixpoint
  that the split was meant to provide is obtained instead by checking a
  cycle's bodies again. `CheckSpecBody` runs `CheckSpecBodyOnce` for the
  head until no member's record changes, and members are re-checked as
  calls reach them (`FnSpec::stale`). Each round reads the previous round's
  records at its back edges (`FnSpec::prev`, `RecordOf`). The first round
  reads none, so a back edge's result does not point anywhere yet
  (`Roots::unknown`, the same bottom as in item 6's discovery passes, and
  now also a flag that holder contents carry), and every rule lets it pass.
  Type checking is repeated each round, at no measurable cost.
  Deleted: `typecheck_cycles.h` (669 lines: the syntactic return-root
  predictor, `ReturnConflict`), `RetRoot::pred/used*`, the `cycleroot`
  sentinel, `cyclestores` (a re-check with `incycle` set replaces the
  replay), `ThreadedClass::relied`/`RelyOnThread` (a re-check finds the
  class broken), `assumedshrinks`/`SettleAssumedShrinks`/`KeepShrinkChecks`/
  `SB_ASSUMED`/`livecapture`, `cyclesites`/`ResolveCycleSites`/`CycleOpen`,
  `SyntacticShrinks`/`SyntacticGrows`/`ScanReceivers`, `InProgressRebinds`,
  `LiveShrink::guessed`, and the `inprogress` branch of every
  `ApplyCallee*`: 719 lines deleted against 433 added. The predictor's
  conflict errors ("use one source", "back edges were not given") are gone.
  A cycle's result is the union of what its returns give, and it is checked
  at every use like any call's result. As a result, 13 fixtures now report
  at the use (a store that does not outlive, a view into a grow-shrink
  array), and two programs that the predictor refused because of its own
  imprecision are now accepted and became positive fixtures
  (cycle_holder_unknown_store, cycle_threaded_return). Cycles settle in two
  or three rounds (`GOOSE_ROUNDS=1` traces them).
* **4. One storage and effect model** was reconsidered after item 3. BCE
  and codegen summarize the optimized tree, after inlining and folding, and
  the checker's records, made before the optimizer runs, cannot stand in
  for those summaries. The region vocabulary (a variable, a class root, a
  temporary, static data) is already shared through `VarDef`. This item was
  left as is.
* **5. Obligations instead of key facts** was measured before being
  attempted (2026-09-25, using a temporary `GOOSE_KEYSTATS` count over
  every sample and the largest tests). No two specializations of any
  function differed only in the depth key. The fact bits (`growshrink`,
  `byteview`, `viewslot`, `heldexact`) split one to four specializations
  per program (twelve of 847 in the physics sample), and class grouping
  split a few more. The redesign would move every class-dependent check in
  a body to its call sites (outlives, grow-shrink taint, exactness, the
  shrink scans' byte-view and view-slot exemptions), along with the errors
  and their fixtures, for a two-percent reduction in bodies, so it was not
  done. `RootArg::unknown` (item 3) is the only key bit added since.
* **7. Smaller items** (2026-09-25): flow snapshots cover the variables
  that the code between a save and its restore can name (`NamedFrames`,
  `SaveFlow`, `CheckSpecBodyOnce`'s restore of outer narrowings), instead
  of every variable on the call path. Measured on a chain of 3,000 nested
  calls, checking is a fifth faster and peak memory is unchanged, so the
  "gigabytes" of §4.11 came from the per-activation bodies and records, not
  from the snapshots. The by-value/by-reference decision (4.12), the
  temporary-copy annotation for `OptViewed` (4.16) and codegen's marker
  expansion (4.17) were left as they are. The first three of the six flags
  were each the subject of a recent fix, and the fixtures pin them.
  `OptViewed`'s syntactic test (`r != n && NamesStorage(r)`) always matches
  the checker's `TempCopy` sites, because folding only rewrites a node that
  was a construct or a call, whose value is a temporary copy. The text
  expansion is isolated and correct.
* **2. Statement liveness from the tree** landed 2026-09-25, as `nodepath`
  (`PathEntry`, `NodeScope`), `nodevals` and `ForOperands` in
  `typecheck.h`/`typecheck_exprs.h`. `HeldOperands` replaces `heldtemps`,
  `TempScope`, `HoldValue`/`HoldLocation`/`HoldSequence`, the receiver and
  render pushes and the `render` flag (now `renderarg`/`renderwhere`).
  `LaterOperands` replaces `shrinkrest`/`RestScope` and gives `UsedAfter`
  the rest of the statement. The two phases of a call are handled in one
  place: a call that is `discovering` its overload holds nothing, and a call
  applying its callee's summary has consumed its own operands. `UsedAfter`'s
  block and loop rescans (`blockpos`, `MentionsName`) stay for item 3, whose
  liveness pass subsumes them. §5.1's syntactic position rules
  (`Call::standalone`, `invalue`) were later replaced by the held operands,
  the parts a construct's head leads to (`AfterHead`), and shrinks logged
  against values under construction.
