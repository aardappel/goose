# Compiler analyses: inventory and simplification options

Step 1 of the effort to stop the fix-a-hole, add-a-flag cycle in the
checker's analyses. This document inventories every analysis the compiler
runs, what each one tracks and how, roughly how much code it is, how
solid it is, and where a different representation would cover the language
more conclusively. The ranked recommendations are at the end.

Numbers are as of master `bfea86b` (2026-09-24). Line counts are of
function bodies, attributed by function (a throwaway scan of the function
definitions in `src/typecheck*.h`, `bce.h`, `optimize*.h` and
`codegen*.h`), so they are rough; the state each analysis keeps is listed
separately, since that is where the growth actually happens.

## 1. Summary

* The compiler runs about 20 distinct analyses. The lifetime family
  (provenance, read-back, stores, shrinks, growth, recursion, return
  roots, specialization keys) is ~5,000 lines of checker function bodies
  plus ~400 lines of state declarations, and accounts for nearly all of the
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
     A call back into a cycle sees an incomplete callee, so every
     interprocedural analysis has a second, syntactic "callee still being
     checked" path, plus take-back and settle machinery (cycle return-root
     prediction, threaded classes, assumed-balanced shrinks, cycle sites).
     Roughly 1,500 lines exist only for this.
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
* Top recommendations, in order of payoff per effort: (1) replace the
  single root with an explicit set of alternatives and derive the patch
  bits from it; (2) derive "what is live in this statement" and "what is
  used after this point" from the tree with one walker each, instead of
  maintained stacks; (3) split lifetime analysis out of type checking and
  run it as a per-cycle fixpoint over summaries, which deletes the
  in-progress machinery; (4) one storage/effect model (regions) shared by
  the checker, BCE and codegen; (5) move class comparisons out of the
  specialization key into call-site obligations. Details in section 5.

## 2. What is tracked where

The analysis state, by owner. Everything below is what a fix touches.

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
**C** accreted, hole-prone; **D** structurally fragile. "Signal" is fixture
count in `test/errors_tc` mentioning the rule's spec section, and commit
subjects since 2026-09-12 that touch it.

| # | Analysis | Spec | Main code | Body LOC | Quality | Signal |
|---|---|---|---|---|---|---|
| 1 | Types, generics, instantiation, overloads, dispatch, builtins, control constructs | §3, §6, §7.1–7.7, §8 | `typecheck_types.h`, `typecheck_calls.h` (resolution half), `typecheck_builtins.h` (`CheckBuiltin`), `typecheck_nodes.h`, `typecheck_flow.h` (constructs) | ~5,200 | B+ | low churn |
| 2 | Root provenance and merged values | §9.2 | `Ident::Check`, `CheckRefOf`, `DecayRef`, `SlotView`, `MergeVals`, `Hides`, `InnerRoot`, `TempRoot`, `TempCopy`, `CheckBranchRoot`, `CheckBindingRoot`, `BindProv`, `RefProvOf`, `CheckRefRebindRoot`, `PrebindLoopRefs` | ~1,000 (+`Prov`/`Val`/`VarDef` state) | C | §9.2: 91 fixtures; 22 commits mention roots |
| 3 | Read-back roots | §9.5 | `RootCandidates`, `ReadBackRoot`, `ReadBackLVal`, `ContainerRead`, `TempContents`, `CanContain`, `ReachesThroughRefs`, `VisibleVars` | ~250 | B- | 16 commits mention read-backs/slots |
| 4 | Store rule and store record | §9.2 store, §5.1 | `FitsAt` (store half, ~100 lines), `RecordStore`, `AddStoreEvent`, `NoteContentRoot`, `ApplyCalleeStores`, `ShrinkTargets`, `ClassArgRoot` | ~250 | C+ | 11 commits |
| 5 | Grow-shrink storage exclusion (taint, slot reads) | §5.2 | `GrowShrinkTaint`, `StoredIntoGrowShrink`, `IntoGrowShrink`, `GrowShrinkCanHold`, `SlotReadable`, `SlotReadMayRetarget`, `RefMayPointInto`, `RefMayRetarget` | ~110 | C | §5.2: 158 fixtures |
| 6 | Shrink checks within a body (grow-only, grow-shrink) | §5.1, §5.2 | `CheckGrowShrink`, `GrowOnlyShrinkAt`, `CheckShrinkHolders`, `CheckHeldShrinks`, `HeldRefsMayPointInto`, `HolderMayPointInto`, `ResolvePendingShrinks`, `ShrinkThrough`, `ShrinkMayFree`, `UsedAfter`, `HoldValue`/`HoldLocation`/`HoldSequence` | ~440 | C- | §5.1: 65 fixtures; 225 fixture names contain "shrink" |
| 7 | Shrink summaries, caller-judged pairs, balanced calls | §5.1 calls, §5.2 balanced | `NoteRootEvent`, `NoteShrink`, `ApplyCalleeShrinks`, `NoteLiveViews`, `NoteLiveShrink`, `MapLiveShrinks`, `ApplyCalleeLiveShrinks`, `ResolveCycleSites`, `ResizesToMark`, `SamePath`, `KeepShrinkChecks`, `SettleAssumedShrinks`, `SyntacticShrinks`, `ScanReceivers` | ~560 (+`LiveShrink`, `AssumedShrink`, `CycleSite` state) | D | 14 commits; `ApplyCalleeShrinks` alone is 183 lines |
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
| 21 | Codegen: free variables, globals and `gs_sp` need (fixpoint), stack assignment, top caching, NRVO, evaluation snapshots | C.2, C.3, §6.10 | `CollectSpecs`, `CanCacheTops`, `RefTopsOk`, `PlanTopCaches`, `ExpandTopMarkers`, `SyncReach`, `DetectNrvo`, `NamedResult`, `Snapshot`, `AllocStk/SaveBase`, `HoistAggregateDecls` | ~520 of ~9,500 | B (marker expansion C) | 17 commits mention stacks/caching |

Supporting weight: `docs/implementation.md` needs 800 lines (§3.4–3.11) to
describe analyses 2–11; `test/errors_tc` holds 647 fixtures (8,400 lines),
`test/lifetimes` 51 (2,400 lines). That suite is the asset any rework
leans on.

## 4. Per-analysis notes

Each entry: what it tracks, how, where the holes come from, and the option.

### 4.1 Root provenance (2)

**Tracks.** For every reference-like value: the variable whose scope bounds
the pointee (`root`), whether that variable owns it (`rootexact`), and the
ordering "A outlives B iff `depth(A) <= depth(B)`" along the compile-time
call path. Temporaries get a `VarDef` of their own one scope deeper.

**How.** One root per value. A value that may be any of several (branches,
rebinds, a call's returns, dispatch arms) keeps the innermost root
(`InnerRoot`) and records what that hides in bits: `intogs` (may point
into a grow-shrink array the kept root does not hold), `cyclelocal` (may
be rooted where a cycle stores nothing), `hidesclass` (may be a parameter
class's pointee), `rootfrom` (the container an inexact read-back came out
of, for diagnostics and store-record `src`), `slotread` (every alternative
was loaded out of storage), `byteview`, `reached`. Holder values carry a
second root (`holderroot`), variables a third (`contentroot`).

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
`goose-provenance-consumers` checklist are this hole class. `RetRoot::alts`
and `CycleRoots::RootSet` are already sets; `CallResult` maps each
alternative and then collapses them with `MergeVals` again.

**Option.** Make the alternative set the representation: `Prov` holds a
small vector of `{root, exact, bound-type}` and merging is set union. The
bits become queries over the set at the consumer: "may point into a
grow-shrink array" is "some alternative's root is one", "cycle-storable"
is "every alternative is", "hides a class" is "some alternative is a class
root", `rootfrom` is the alternative's own record. The outlives check loops
over alternatives. This removes five flags and their propagation, and any
new construct that merges values gets the right answer by construction.
`holderroot` can be the same set on the holder's contents. This is the
prerequisite for every other recommendation.

### 4.2 Read-back roots (3)

**Tracks.** Where a reference loaded out of a container points.

**How.** Re-derived at the load by enumerating the variables in scope whose
type can hold the pointee by value (`RootCandidates` over `CanContain`),
plus globals, static data, and parameter class roots whose references
lead there; deepest candidate, exact only when unique. Temporaries answer
with their holder root.

**Assessment.** Principled and small. Its weakness is that it is a
type-based approximation consulted at the use, so it reasons about the
scope as it is at the use rather than about what was stored, while the
store record (4.3) knows exactly what was stored. The two are reconciled by
hand in `RecordStore` (`src` via `rootfrom`) and `HolderMayPointInto`.

**Option.** With root sets (4.1) and a per-container contents set kept by
the store record, a read-back is simply the container's contents set when
the container is a local of the activation, and the type-based enumeration
is the fallback for containers the activation cannot see into (parameters,
globals). Same rule, one source of truth.

### 4.3 Store rule and store record (4)

**Tracks.** That a stored reference outlives its destination; and, for the
shrink rules, every store of a reference or holder into a container
(`storeevents`, program-wide list; per-container `contentroot`).

**How.** `FitsAt` runs the rule for every value meeting a typed slot,
against every storage an inexact destination may stand for
(`ShrinkTargets` over `Dest::reached`). A store through a parameter's class
is kept on the specialization (`classevents`) and replayed at each call
(`ApplyCalleeStores`), widened again at the call. A back edge conservatively
records every reference argument as stored into every container argument.

**Assessment.** The rule is fine. The record is a flat list scanned
linearly per query (`HolderMayPointInto`, `EachHolderRoot`), with `src`
chains to follow copies; `reached` and `bound` were added this month to
widen inexact destinations. Two recent fixes (3362c37, 12e291b) were about
which storages an inexact destination enumerates.

**Option.** Keep the record, but as per-container contents sets (the same
set type as 4.1) updated at stores, with a copy recorded as a reference to
the source container's set. `ShrinkTargets` and `RootCandidates` then are
one function: "the storages a root of type T may stand for".

### 4.4 Grow-shrink storage exclusion (5)

**Tracks.** That no reference that may point into a grow-shrink array is
ever stored, so the §5.2 shrink scan can look at variables only.

**How.** `GrowShrinkTaint` asks the root, then `intogs`, then a reference to
a slice's slot; `slotread` says a value came out of storage and therefore
is clean; `SlotReadMayRetarget` and `RefMayRetarget` re-admit `var`
references a later rebind in a loop could move.

**Assessment.** Small but the most recent hole class (`slotread`: 1a06b13;
`viewslot`; `intogs` on keys). All of it is "which alternatives does this
value have", again.

**Option.** Disappears into 4.1: taint is "some alternative is a
grow-shrink root", cleanliness is "every alternative was read out of
storage", both derived.

### 4.5 Shrink checks within a body (6)

**Tracks.** At a shrink, every live thing that may point into the array:
held temporaries of the statement, reference and slice variables in scope
(and what a reference to a slice or holder reaches), holders by their store
record, other globals by type.

**How.** Three scans (`CheckHeldShrinks`, `GrowOnlyShrinkAt` over `vars`,
`CheckShrinkHolders` over `VisibleVars`), each with its own case analysis
of "may point into" (`RefMayPointInto`, `HeldRefsMayPointInto` alone has
eight outcomes), liveness by `UsedAfter` (syntactic mention after the
statement, `shrinkrest`, loops), and `pendingshrinks` for stores later in a
loop body.

**Assessment.** The most patched area. Grow-only and grow-shrink take
different scans, byte views and slot reads take exemptions from each, and
"through an inexact root" multiplies the scan by `ShrinkTargets`.

**Option.** One scan: for each live view (variables plus the statement's
earlier operands, see 4.14), intersect its alternative set with the shrunk
array's alternative set. Grow-only versus grow-shrink then differs only in
whether holders' contents sets are included. The eight-way case analyses
collapse into set intersection plus the two existing exemptions (byte
views, relative references).

### 4.6 Shrink summaries, caller-judged pairs, balanced calls (7)

**Tracks.** What a body shrinks (by parameter, external, or bound type),
which still-used views only its callers can tell apart from the shrunk
array (`liveshrinks`), and which shrinks are balanced resizes.

**How.** Summaries on `FnSpec`, applied at calls by `ApplyCalleeShrinks`
(183 lines) which expands, judges balance against aliasing, and either
scans or defers. Pairs are mapped through arguments at every call and again
when a cycle closes (`cyclesites`). A back edge is assumed balanced, its
skipped checks are run anyway with errors captured by catching
`CompileError` (`KeepShrinkChecks`), and settled when the cycle closes.

**Assessment.** Structurally fragile: three interleaved deferral mechanisms
(pending shrinks, cycle sites, assumed shrinks), error capture by exception,
and a `guessed` bit for wording. `liveshrinks` is the right idea (record an
obligation, let the caller judge) applied to one rule only.

**Option.** Make obligations the general mechanism (section 5, item 5) and
compute summaries to a fixpoint per cycle (item 3), which removes the
deferrals. A summary is then: shrunk regions with balance, and the pairs.

### 4.7 Growth during construction, uses during whole assignment (8)

**Tracks.** That nothing grows an array while a value is built in place in
it, and that a whole assignment's right-hand side does not use the array it
rebuilds.

**How.** A per-activation log (`growlog`) checked against the constructed
root after the expression (`CheckGrowsSince`) with `MayAliasRoots`;
conflicts between two parameter classes deferred to the end
(`growconflicts`, `ResolveGrowConflicts`); callees contribute `growparams`
/`growexternals` or, in progress, their text. `CheckBuiltUses` walks the
right-hand side and every callee's `NamedOutside`.

**Assessment.** Reasonable. `MayAliasRoots` is a fourth alias oracle
(beside `RefMayPointInto`, BCE's `AffectedByWrite`, codegen's `RefTopsOk`).

**Option.** Aliasing over alternative sets; growth as one more effect in the
shared summary (item 4).

### 4.8 Specialization keys and root classes (9)

**Tracks.** Which call sites may share one checked body.

**How.** The key is types, bindings, literal parameters, function values,
narrowed environment, `escaped`, `needs`, and per argument a `RootArg` with
class number, `depthkey`, writability, pool, and four grow-shrink and view
bits; `exact` and `concrete` are ANDed across sites afterwards and settled
by `SettleParamRootExactness`. A back edge reuses the in-progress body
whatever its roots.

**Assessment.** Well-commented, but the key mixes two strategies: facts the
body needs to answer comparisons inline (`depthkey`, `growshrink`,
`heldexact`, `viewslot`, `intogs`) and facts settled after the fact
(`exact`, `concrete`, `growconflicts`). Each new hole prompts a debate about
which side to put it on (`goose-spec-key-depth-ties`,
`goose-holder-param-exactness`). Key growth also multiplies bodies checked.

**Option.** Item 5: keep types, writability and pools in the key; let the
body record obligations over its class roots (`A outlives B`, `A ≠ B`,
`A holds no grow-shrink array`) that call sites discharge, as
`liveshrinks` does today. `depthkey`/`EnvReach` and the `exact`/`concrete`
settling then go.

### 4.9 Recursion (10)

**Tracks.** Which functions are in a cycle; the roots of results a back
edge returns before the returns are checked; which parameter classes a
cycle may store through (pools, threaded classes); stores made before a
function knew it was in a cycle; non-fixed locals held across a call into
the cycle.

**How.** A syntactic fixpoint over returns (`CycleRoots`, 690 lines)
predicts roots; real returns are checked against the prediction with
take-back rules (`ReturnConflict`, `RetRoot::used*`, `predlost`). Threaded
classes are tracked with heirs, `relied` and retroactive `Unthread` errors.
`cyclestores` are refused retroactively by `JoinCycle`. Every
`ApplyCallee*` has an `inprogress` branch using syntactic scans
(`SyntacticShrinks`, `SyntacticGrows`, `InProgressRebinds`,
`NamedOutside` pending). The §7.8 conservatisms are listed in
`implementation.md` §10.

**Assessment.** The hardest code in the compiler, and all of it a
consequence of checking a body once inside the call that first reaches it.
BCE and codegen face the same recursion and solve it with a fixpoint over
summaries in ~30 lines each (`ComputeEffects`, `CollectSpecs`).

**Option.** Item 3: run the lifetime analysis after type checking, per
strongly connected component, iterating bodies with optimistic summaries
until stable. Predictions, take-backs, threaded-class heirs, cycle stores,
assumed shrinks and every syntactic fallback go. What remains of §7.8 is
the stack rule (`JoinCycle`'s non-fixed-local check) and the pool argument
rule.

### 4.10 Return roots (11)

**Tracks.** Every root a function's results may have, mapped at each call.

**How.** `RetRoot::alts`, one `RetAlt` per distinct root with ANDed
guarantees; `RetAltVal` maps a class to the argument's root; `CallResult`
merges the mapped alternatives with `MergeVals`. Long-distance returns are
restricted to globals (TODO 0d). Inferred results follow `let` rules.

**Option.** With 4.1 the result is the mapped set, unmerged; `RetAlt` and
`Prov` alternatives are one type. Long-distance returns can then use the
real rule ("rooted at or above the target's frame").

### 4.11 Flow state: definite assignment and narrowing (12)

**How.** Snapshot of every variable in scope at each branch, joined by
`MergeFlow`. Loops kill narrowings of names the body rebinds, found by a
syntactic scan that follows calls by name (`CollectAssignedNames`) and
callee summaries (`reboundoptionals`), then `FinishLoopNarrowing` errors
when the scan missed a rebind through a call.

**Assessment.** Works; the name-based loop rule and the "error if the
guess was wrong" pattern are the same single-pass-over-loops symptom as
`PrebindLoopRefs` and `refidentityused`. Snapshots are O(variables in
scope) per branch, which is what makes deep call paths cost gigabytes.

**Option.** Snapshot only variables the construct touches (a prescan of the
branch, or copy-on-write), and treat loops with a two-iteration check
(item 6) instead of name scans.

### 4.12 Copy semantics and reference transparency (13)

**How.** `CheckValue` decays references unless the destination keeps them,
rewrites lvalues to `&x` (`AutoRef`, `synth`), and decides implicit copies
with `inreturn`, `argpath`, `storagebranches`, `implicitcopy`,
`branchcopy`, `inferred`. Calls check arguments twice (phase 1 without a
parameter type, phase 2 with) and `BindBranchesByRef` re-checks constructs
at reference parameters.

**Assessment.** The two-phase argument check plus construct-as-argument
handling is where several recent fixes landed (4c05a0f, fe901f5,
3259cd7). Six context flags for one rule is a smell.

**Option.** Decide "by value or by reference" once per argument from the
parameter type and the argument's shape (lvalue, reference, construct of
lvalues) in one function, before phase 2, and record the decision on the
node; the flags become locals of that function.

### 4.13 Writability (14), relative references (15), literal parameters (17)

Sound and small. Writability is a bit plus `const` in the type with one
enforcement point (`FitsAt` under `constslot`). Relative references need
root identity and ask `rootexact`, which is the right dependency. Literal
parameters are a clean record-and-verify design. No action.

### 4.14 Held temporaries and liveness (18)

**Tracks.** Values evaluated earlier in the statement that are still live
(`heldtemps`), the parts of the statement that run after the shrink
(`shrinkrest`), whether a variable is mentioned later (`UsedAfter`).

**How.** Pushed at ~12 sites (`CheckArg`, index and slice receivers,
assignment locations, `Binary` operands, `==` views, render, dispatch
arguments, literal fields, returns), popped by `TempScope`. `UsedAfter`
rescans blocks with `MentionsName`, following nested functions by name,
on every shrink.

**Assessment.** A site missing a push is a hole (7027e7f, 5896dbd,
756e1fe), and `shrinkrest` exists because `heldtemps` cannot see the
right-hand side of the statement.

**Option.** Derive both from the tree with one walker each: "the
reference-typed subexpressions evaluated before node N within its
statement, in evaluation order" and "the variables mentioned at or after
position P in the enclosing blocks", computed once per statement or body
(a backward pass gives every position's later-use set in O(n)). No sites
to forget; evaluation order lives in one function shared with codegen's
`Snapshot`.

### 4.15 Bounds-check elimination (19)

A self-contained difference-constraint domain with generations, places
with an ultimate owner, kills, per-spec effect summaries to a fixpoint,
entry facts from call sites, recorded invariants, and loop-view hoisting.
Sound structure, documented gaps, low churn. It consumes checker provenance
(`ref.root`, `rootexact`, class roots) and re-derives aliasing and effects
for itself. No structural action; it is the model for item 3, and a
consumer of item 4.

### 4.16 Optimizer analyses (20)

`Analyze` (writes and address-taking per variable, flow-insensitive),
reachability with use counts, inline classification with C-nesting depth,
`OptViewed` (re-materialize a value the checker rooted at a temporary when
folding reduces it to storage), and purity checks for base-case inlining
and tail-recursion elimination. Sound. `OptViewed` re-derives syntactically
a decision the checker made (`TempCopy`); recording "this value is a
temporary copy" on the node would let the optimizer read it instead.

### 4.17 Codegen analyses (21)

`CollectSpecs` computes free variables, touched globals and `gs_sp` need
by a fixpoint over the call graph (its third computation of the call
graph). Top caching decides per function (`CanCacheTops`, `RefTopsOk`
reading `RootArg::exact`/`concrete`), plans regions, and then rewrites the
emitted C text: `ExpandTopMarkers` parses `goto`s and labels out of the
generated code to place flushes, and `HoistAggregateDecls` moves
declarations by text for an MSVC bug. NRVO is a syntactic scan of returns.

**Option.** Emit into a minimal statement list with explicit loop and
flush nodes and expand from that instead of from text (medium, isolated).
Consume the shared storage summary (item 4) in `RefTopsOk` and `SyncReach`.

## 5. Cross-cutting findings and recommendations

Ranked by expected reduction in future churn per unit of effort. Effort is
a rough size in lines touched; "deletes" names what goes away.

### 1. Alternative sets instead of one root plus bits (medium effort, highest payoff per line)

Replace `Prov::root/rootexact/rootfrom/intogs/cyclelocal/hidesclass/
slotread` with a small set of alternatives `{root, exact, fromcontainer,
slotread}` and merge by union. Consumers loop: outlives means every
alternative outlives; taint means some alternative is a grow-shrink root;
cycle-storable means every alternative is; class-hidden means some
alternative is a class root. `holderroot` becomes the contents set;
`RetAlt` and `CycleRoots::RootSet` become the same type. Touches ~1,500
lines mechanically (`MergeVals`, `Hides`, `InnerRoot`, `GrowShrinkTaint`,
`RetAltVal`, `CallResult`, `CheckRefRebindRoot`, `RootArg`, the scans).
Deletes: five flags, `Hides`, `InnerRoot`'s tie rule, the
`intogs`/`hidesclass` propagation in ~20 places, and the class of bugs
"merged value hid X". Risk: low; the fixture suite pins every rule. Do this
first; every later item assumes it.

### 2. Derive statement liveness and later-use from the tree (small effort)

One walker for "operands evaluated before N in its statement" replaces the
`heldtemps` pushes, `HoldValue`/`HoldLocation`/`HoldSequence`,
`shrinkrest`/`RestScope`, `render` and `invalue`; one precomputed
later-use table per body replaces `UsedAfter`/`MentionsName`/`blockpos`
rescans and `loopassigned`. ~300 lines replaced by ~150, no push sites
left to forget, and evaluation order stated once (codegen's `Snapshot`
should read the same function). Risk: low.

### 3. Split lifetime analysis from type checking; fixpoint per cycle (large effort, largest deletion)

Type checking keeps its single pass and creates specializations on types,
bindings, literal parameters, function values, writability and pools.
Lifetime analysis (2–11 in the inventory) then runs over the finished
specialization graph in callee-first order, iterating each recursive
cycle's bodies with optimistic summaries until the summaries stabilize,
exactly as `BCE::ComputeEffects` and `CodeGen::CollectSpecs` already do.
Deletes: `typecheck_cycles.h` (prediction and `ReturnConflict`),
`RetRoot::pred/used*/predlost/usedthreads`, the `cycleroot` sentinel,
threaded-class heirs/`relied`/`Unthread`, `cyclestores`, `assumedshrinks`
/`SettleAssumedShrinks`/`KeepShrinkChecks`, `cyclesites`/
`ResolveCycleSites`, `SyntacticShrinks`/`SyntacticGrows`/`ScanReceivers`,
`InProgressRebinds`, `NamedOutside`'s pending case, and the `inprogress`
branch of every `ApplyCallee*` — around 1,500 lines, and the §7.8
conservatisms in `implementation.md` §10 become the spec's real rule.
Cost: a body is analyzed more than once inside a cycle (bounded by the
lattice height, in practice two or three rounds), and the analysis needs
its own walk over checked bodies (the `Check` methods stay for types).
Risk: medium; it changes when errors are reported, not which programs are
legal, so the fixtures still decide. Prerequisite: item 1.

### 4. One storage and effect model for checker, BCE and codegen (medium effort)

Define regions (a variable, a class root, a temporary, a global, static
data) and one per-specialization summary computed once: regions reached,
written, resized (with balance), stored into (with what), grown, returned,
rebound optionals, free variables, and integers written. The checker's
`NoteRootEvent`/`ShrinkTargets`/`RootCandidates`/`MayAliasRoots`/
`NamedOutside`, BCE's `UltOf`/`Reach`/`AffectedByWrite`/`Effects`, and
codegen's `CollectSpecs`/`RefTopsOk`/`SyncReach` become queries on it.
Deletes three call-graph traversals and four alias oracles. Natural to do
together with item 3, since that is when the checker gets a summary
fixpoint of its own.

### 5. Obligations instead of key facts (medium effort, after 3)

Keep the specialization key to what changes the body's meaning (types,
bindings, literal parameters, function values, writability, pools,
`escaped`, `needs`). Let the analysis record obligations over class roots
(`A outlives B`, `A is not B`, `A holds no grow-shrink array`, `A's
contents are exactly one array`) that each call site discharges with its
concrete roots, generalizing `liveshrinks`. Deletes `depthkey`/`EnvReach`,
`growshrink`/`byteview`/`viewslot`/`intogs`/`heldexact` from `RootArg`,
`SettleParamRootExactness`, `growconflicts`/`ResolveGrowConflicts`, and
the specialization splits they cause (a function called once with a
global and once with a local today gets two bodies).

### 6. Loops: two-iteration check instead of name scans (small effort, independent)

Check a loop body twice, the second time with the bindings, stores and
narrowings the first found (a widening step), and report on the second
pass. Deletes `PrebindLoopRefs`/`ResolvePrebind`, `refprebound`,
`refidentityused`, `loopassigned`/`AssignedInEnclosingLoop`,
`pendingshrinks`/`ResolvePendingShrinks`, `CollectAssignedNames`'s call
following and `FinishLoopNarrowing`'s "guess was wrong" error. Cost: loop
bodies are checked twice (nested loops 2^depth; cap at two rounds per
loop by checking inner loops once inside each outer round, or memoize).
Can be done before or after item 3.

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
week-scale change that pays for itself; 3 + 4 is the structural rewrite
and should start only once 1 has landed, since sets are what its summaries
are made of.

Validation for every step is the existing suite: `test/errors_tc` (647
rejections with expected messages), `test/lifetimes` and `test/expected`
(positive programs whose output is fixed), samples and the standard
library, at both optimization levels and through TinyCC. A change that
reports an error at a different line, or with different wording, needs the
expectation updated; a change that accepts a program the old compiler
rejected needs a review of the fixture (many rejections in `errors_tc`
are conservatisms, and item 3 in particular will lift some). Keep a scratch
build of the pre-change compiler and diff verdicts over all fixtures and
samples before each landing (`docs/testing.md`,
`goose-branch-validation-snapshots`).

## 7. Status

* **1. Alternative sets** landed 2026-09-25: `Roots`/`RootAlt` in `ast.h`
  replace `root`/`rootexact`/`rootfrom`/`intogs`/`cyclelocal`/`hidesclass`/
  `slotread`, `Val::contents` replaces `holderroot`/`holderexact`,
  `VarDef::contents` replaces `contentroot`/`contentexact`/`contentset`;
  read-backs enumerate their candidates once, at the read, as exact
  alternatives (§9.5), and the cycle predictor holds each alternative of a
  return on its own. `RootArg::intogs` became `gsvia`; `InnerRoot`,
  `CanonRoot` (`rootalias` was never set), `Hides` and `NoteContentRoot`
  are gone. A `var` of several roots that a loop rebinds to one the
  syntactic scan cannot show it has is read as bounded by its roots inside
  the loop (`loopretargets`), the single-pass replacement for what the
  collapsed inexact root used to guarantee; item 6 removes that too.
* **2. Statement liveness from the tree** landed 2026-09-25: `nodepath`
  (`PathEntry`, `NodeScope`), `nodevals` and `ForOperands` in
  `typecheck.h`/`typecheck_exprs.h`; `HeldOperands` replaces `heldtemps`,
  `TempScope`, `HoldValue`/`HoldLocation`/`HoldSequence`, the receiver and
  render pushes and the `render` flag (now `renderarg`/`renderwhere`);
  `LaterOperands` replaces `shrinkrest`/`RestScope` and gives `UsedAfter`
  the rest of the statement. The two phases of a call are stated once: a
  call `discovering` its overload holds nothing, a call applying its
  callee's summary has consumed its own operands. `UsedAfter`'s block and
  loop rescans (`blockpos`, `MentionsName`) and `invalue` (§5.1's rule 3, a
  language rule rather than tracking) stay for item 3, whose liveness pass
  subsumes them.
