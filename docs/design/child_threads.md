# Child threads: fork-join over shared memory inside one program, and how it meets `par for`

<details>
<summary>Table of contents</summary>

- [0. Summary](#0-summary)
- [1. The problem](#1-the-problem)
  - [1.1 What share-nothing costs on a kernel](#11-what-share-nothing-costs-on-a-kernel)
  - [1.2 What "cheap" has to mean](#12-what-cheap-has-to-mean)
- [2. What Goose already decides](#2-what-goose-already-decides)
  - [2.1 A dormant parent freezes every lifetime](#21-a-dormant-parent-freezes-every-lifetime)
  - [2.2 Writability is inferred, so "captured as `const`" is one rule](#22-writability-is-inferred-so-captured-as-const-is-one-rule)
  - [2.3 Bodies are already outlined, and free variables already pass by pointer](#23-bodies-are-already-outlined-and-free-variables-already-pass-by-pointer)
  - [2.4 Two body shapes the stack model allows](#24-two-body-shapes-the-stack-model-allows)
  - [2.5 Program instances, and what a pool thread must adopt](#25-program-instances-and-what-a-pool-thread-must-adopt)
  - [2.6 Backends without threads run the loop](#26-backends-without-threads-run-the-loop)
  - [2.7 What the checker knows about effects today](#27-what-the-checker-knows-about-effects-today)
- [3. What others built](#3-what-others-built)
  - [3.1 Thread level and lane level, side by side](#31-thread-level-and-lane-level-side-by-side)
  - [3.2 The safety rule: who may write what](#32-the-safety-rule-who-may-write-what)
  - [3.3 Pools](#33-pools)
  - [3.4 Reductions](#34-reductions)
  - [3.5 What to take](#35-what-to-take)
- [4. The design](#4-the-design)
  - [4.1 One construct, two levels](#41-one-construct-two-levels)
  - [4.2 Which level a loop gets](#42-which-level-a-loop-gets)
  - [4.3 What a body may touch](#43-what-a-body-may-touch)
  - [4.4 Disjoint writes: structural first, proven second](#44-disjoint-writes-structural-first-proven-second)
  - [4.5 Stackless and stackful bodies](#45-stackless-and-stackful-bodies)
  - [4.6 Results, reductions, determinism](#46-results-reductions-determinism)
  - [4.7 Aborts](#47-aborts)
  - [4.8 In the spec](#48-in-the-spec)
  - [4.9 The runtime](#49-the-runtime)
  - [4.10 What the compiler needs](#410-what-the-compiler-needs)
- [5. The query engine's phases, written out](#5-the-query-engines-phases-written-out)
- [6. Options side by side](#6-options-side-by-side)
- [7. Recommendation and order](#7-recommendation-and-order)
- [8. Open questions](#8-open-questions)
- [References](#references)

</details>

Research note, November 2026. The chunking builtins `chunk` and
`chunk_at` of [4.4](#44-disjoint-writes-structural-first-proven-second)
are implemented, as ordinary slicing (step 1 of
[section 7](#7-recommendation-and-order); spec §3.10), and so is
`par for` with structural disjointness, R1 to R5, integer reductions and
the pool of [4.9](#49-the-runtime) (step 3, option A; spec §11.2), the
query engine's phases of [section 5](#5-the-query-engines-phases-written-out)
running as written there but for the blur's row slice, which needs the
proven rule and is written with `dst.chunk(y, H)` instead
(`test/threads/par_for.goose`). A body with growable locals runs on the
data stacks of the pool's workers, which keep their regions from one task
to the next, rather than as a child program of its own (4.5). `simd for`,
`par simd for`, the effect summary of 2.7 beyond the builtins and the
standard library's `os` and `ffi` functions, and proven disjointness are
not implemented, and their syntax is a placeholder. The question, from
[goose#234](https://github.com/aardappel/goose/issues/234), is how a Goose
program can split work over a large array across cores *without copying
the array*: threads that read the parent's data in place and each write a
disjoint part of an output, started and joined cheaply enough to be used
once per pass of a query engine or an image pipeline. The second question is
how such threads combine with the SIMD-level `par for` proposed in
[llm_frontier.md §4.3](llm_frontier.md#43-par-for-shared-memory-iteration-proven-race-free):
an outer split over pooled threads and an inner split over lanes, in one
language. Section 1 is the problem with measurements, section 2 what the
language already settles, section 3 the prior art, section 4 the design,
section 6 the alternatives, section 7 the order to build in.

## 0. Summary

* **One claim, two keywords, two levels.** A loop marked `simd for`,
  `par for` or `par simd for` makes one claim, "these iterations are
  independent", checked by one set of rules; the keywords say which
  lowering it gets. `simd` puts iterations on *SIMD lanes* of the calling
  thread and never forks; `par` cuts the range into contiguous chunks on
  *child threads* of the same program and leaves each chunk a plain loop;
  `par simd` does both, chunks on threads and lanes within each chunk,
  which is OpenMP's `parallel for simd`. A `simd for` written inside a
  `par for` body is the other way to get both: ISPC's `launch` plus
  `foreach`, Mojo's `parallelize` plus `vectorize`, Halide's
  `.parallel(y).vectorize(x)`
  ([3.1](#31-thread-level-and-lane-level-side-by-side)). Each level is
  useful alone, and neither has a meaning of its own: the program computes
  what the sequential loop computes
  ([4.1](#41-one-construct-two-levels), [4.2](#42-which-level-a-loop-gets)).
* **The parent is dormant, so sharing is safe.** While the loop runs, no
  code of the parent runs, nothing it owns can grow, shrink or go out of
  scope, and every reference it holds stays valid: Rust's
  `std::thread::scope` argument, which every language that lets children
  borrow a parent's stack makes the same way
  ([2.1](#21-a-dormant-parent-freezes-every-lifetime),
  [3.2](#32-the-safety-rule-who-may-write-what)). The only question left is
  which writes may collide, and the answer is a short list: everything a
  body captures is read-only, except an element it was handed by `for &x`,
  a chunk it was handed by a chunking builtin, and a place the prover shows
  no other iteration touches ([4.3](#43-what-a-body-may-touch),
  [4.4](#44-disjoint-writes-structural-first-proven-second)). Structural
  disjointness needs no new analysis and covers every phase in the issue;
  the two-iteration proof of llm_frontier §4.3 comes second.
* **Two body shapes.** A body with only fixed-size locals runs on a pool
  thread's native stack, with nothing reserved, and may also run on lanes.
  A body with growable locals is compiled as a *child program* with a stack
  block of its own, which the pool thread keeps across forks and rewinds
  between tasks; it runs on threads only, and the chunk the parent's own
  thread runs uses the parent's stacks above its watermarks
  ([4.5](#45-stackless-and-stackful-bodies)).
* **Cheap, measured.** On the 32-thread development machine, today's
  `thread_spawn` plus `thread_wait` costs about 40 µs per worker and 7-10 µs
  per data stack region, all of it the operating system's; a pool of parked
  threads forks and joins 8 children in 9-16 µs and 32 in 43-57 µs, and a
  pool that spins briefly before parking in 0.6-2.8 µs. The copies in and
  out, which the issue measured at 2.5x the single-core time, go away
  entirely ([1.1](#11-what-share-nothing-costs-on-a-kernel),
  [4.9](#49-the-runtime)).
* **What changes against llm_frontier §4.3.** Aborts are allowed at the
  thread level (the program aborts as it would from main's thread; which of
  several possible aborts is reported is unspecified), growable locals are
  allowed through child programs, structural disjointness joins the proven
  kind, and the runtime's pool is shared by both levels
  ([4.7](#47-aborts)).
* **Order.** Structural chunks and stackless children first, which needs no
  prover work and only a small checker rule; then child programs; then the
  two-iteration proof; then `simd for` as a vectorizer assertion and later
  as SPMD ([7](#7-recommendation-and-order)).

## 1. The problem

### 1.1 What share-nothing costs on a kernel

Goose's threads share nothing: a worker is a separate program with its own
data stacks and its own copies of the globals it uses, fed by typed queues
of flat values ([spec §11.2](../goose_spec.md#112-concurrency)). That is
why the language has no locks and no data races, and it is the right shape
for servers and pipelines. For a kernel over one large array it is the
wrong shape, because the data is already in place and the work is a loop
whose iterations are independent. The issue's measurements, 8 cores at
`-O2`:

| Workload | 1 core | 8 workers through queues | 8 pooled workers in place |
|---|---|---|---|
| `samples/22_image_filters.goose` at 3840x2160, 20 blur passes and Sobel | 78 ms | 38 ms | 10 ms |
| 128 MB `i64` transform with an 8 MB lookup table, 20 passes | 151 ms | 418 ms | 50 ms |

The queue version is slower than one core on the second workload because
the main thread slices and copies 128 MB into `qput` messages and copies
the results back out of `qget` messages, alone, twenty times, and the 8 MB
table goes to each worker as a copy. The third column is the issue
author's prototype of what this note proposes: workers that read `const
T[:]` views of the parent's arrays and write disjoint `T[:]` runs of the
output, with their data stacks kept across passes.

Spawning is the second cost, and it is worth knowing its size before
designing a pool. Measured here on the 32-thread development machine
(Windows, clang), with `bench/fork_cost/spawn_cost.goose` and
`bench/fork_cost/pool_cost.c`:

| Children | `thread_spawn` + `thread_wait`, empty worker | the same, worker with two data stacks | `CreateThread` + join, C | pool, parked, fork + join | pool, spinning, fork + join |
|---|---|---|---|---|---|
| 8 | 327 µs | 451 µs | 338-346 µs | 8.5-15.5 µs | 0.6-0.7 µs |
| 16 | 637 µs | 865 µs | 624-651 µs | 21-23 µs | 1.2-1.4 µs |
| 31 or 32 | 1238 µs | 1637 µs | 1214-1256 µs | 43-57 µs | 2.3-2.8 µs, once 39 µs |

So today's spawn is the operating system's thread creation, about 40 µs
per worker, plus 7-10 µs for each 2 GB region a worker's data stacks
reserve, commit a page of, and release; Goose's own packing and unpacking
is noise beside that. A persistent pool woken through `WaitOnAddress`
(Linux: a futex) is 25-40x cheaper, and a pool whose workers spin for a
bounded time before parking, which is what every production runtime does
([3.3](#33-pools)), is 200-500x cheaper. The one slow spinning round at
32 children is the oversubscription hazard: with every logical CPU busy
spinning, a worker that the scheduler preempts delays the join, which is
why libgomp spins only 1000 times when there are more threads than CPUs.

Against the issue's numbers: a pass of the 128 MB transform takes 2.5 ms
pooled, so spawning eight stack-owning workers per pass (0.45 ms) would be
18% of it, and a parked pool's fork (10 µs) 0.4%. A query engine's phases
over partitions of a few megabytes are shorter than that, and there the
spawn would exceed the work.

### 1.2 What "cheap" has to mean

Four costs, in the order they matter:

1. **No copies of inputs.** The children read the parent's arrays where
   they are. This is the whole 418 ms.
2. **No copies of outputs.** Each child writes its part of the output
   where the parent will read it. Per-child growable results, where a
   program needs them, are the one place a copy is tolerable, and it is
   one copy of the result rather than two of everything
   ([4.6](#46-results-reductions-determinism)).
3. **No operating system thread per pass.** A pool, started lazily, kept
   for the program's life.
4. **No data stack regions per pass.** A child that owns no growable local
   needs no regions at all; one that does keeps its block across passes
   and rewinds it ([4.5](#45-stackless-and-stackful-bodies)).

And one cost the issue did not name, which the share-nothing design pays
on every spawn: a worker's globals are copied at `thread_spawn`
([implementation.md §6.8](../implementation.md#68-threads-and-queues)). A
child reads the parent's globals in place and copies none.

## 2. What Goose already decides

### 2.1 A dormant parent freezes every lifetime

The safety of a child reading the parent's memory rests on one fact: no
code of the parent runs while the children do. The loop is a statement of
the parent; the parent's thread is inside it until every iteration has
ended. So nothing the parent owns is assigned, grown, shrunk or released
in the meantime, and every reference and slice the body captures is as
valid at the end of the loop as at its start. Goose adds two things that
most languages do not have:

* **Addresses are stable.** Growth "never moves memory; references into a
  data stack are stable for the life of the data beneath them"
  ([spec §1.2](../goose_spec.md#12-memory-model)). A child holding a
  slice of a parent's growable array holds raw addresses that cannot
  change, whatever the parent's array is (it cannot grow anyway, by the
  point above).
* **The outlives rule is already checked.** The body is a block of the
  parent, so a reference made inside it dies inside it, by the same rules
  as any block ([spec §9.1](../goose_spec.md#91-the-outlives-rule),
  [§9.2](../goose_spec.md#92-roots-and-the-depth-check)). Nothing a child
  makes can escape to the parent except through a write to a captured
  place, which [4.3](#43-what-a-body-may-touch) governs.

This is exactly the argument Rust's `std::thread::scope` makes: "scoped
threads can borrow non-`'static` data, as the scope guarantees all threads
will be joined at the end of the scope" ([rust-scope]). Rust learned the
hard way that the guarantee has to be the construct itself and not a
droppable guard (the 2015 "leakpocalypse", [rust-24292], [rfc-1066]), and
every later design (Swift's task groups, Java's structured task scopes
([jep-453]), Kotlin's `coroutineScope` ([kotlin-scope]), Trio's nurseries
("the nursery block doesn't exit until all the tasks inside it have
exited", [njs]), X10's `finish`, Chapel's `forall`) blocks the parent in a
lexical scope for the same reason
([3.2](#32-the-safety-rule-who-may-write-what)). Goose has no destructors
and no way to leak, so the loop statement is that scope and there is
nothing to add.

### 2.2 Writability is inferred, so "captured as `const`" is one rule

Whether a reference or slice may be written through is "inferred per
instantiation, in the same way as roots, with no annotation needed"
([spec §9.5](../goose_spec.md#95-writability)), and the language already
has bindings that are read-only views of writable data: a by-value `for`
binding, a `match` binding, a field of a `const` value. The rule a child
needs, "every variable of the parent is read-only inside the body except
what the loop handed this iteration", is a view of that kind at the
loop's boundary, and the diagnostics exist ("cannot write through a
read-only slice"). Checker analysis 14 in
[analysis_inventory.md](analysis_inventory.md#3-inventory) is 60 lines and
rated A-.

### 2.3 Bodies are already outlined, and free variables already pass by pointer

A child body has to become a C function over a range plus an environment,
which three existing mechanisms give:

* A nested function or function value already receives "the free
  variables of a nested function or function value (fixed ones by pointer,
  resizables as header pointer plus stack, pools as `gs_pref`)"
  ([implementation.md §6.4](../implementation.md#64-the-calling-convention-c3)).
  An outlined body is a nested function whose free variables are packed
  into one struct, with the loop's chunk bounds beside them.
* The thread thunk (`EnsureThreadThunk`, `EmitThreadThunk`) already
  outlines a worker's entry over a packet; a child thunk is the same with
  pointers in the packet instead of images.
* `simd` versions already emit a body several times with a dispatch in
  front ([implementation.md §6.13](../implementation.md#613-simd-functions)).
  The chunk loop a child runs is the loop such a version vectorizes.

Halide lowers its `parallel` dimension the same way: "The loop body is
lowered to a separate function ... we finally generate code that enqueues
the iterations of the loop onto a task queue, which a thread pool consumes
at runtime" ([halide-pldi13]).

### 2.4 Two body shapes the stack model allows

The recursion rule, "no function in the cycle may call into the cycle
while one of its nonfixed locals is in scope"
([spec §7.8](../goose_spec.md#78-recursion)), is the coroutine rule
([coroutines.md §2.2](coroutines.md#22-the-recursion-rule-is-the-coroutine-rule)),
and it is the rule for a body that runs on another thread's native stack
with no data stacks of its own: no nonfixed local, so nothing to put on a
stack the thread does not have. llm_frontier §4.3 states it as its rule 2.
Codegen already decides this per specialization: a body that builds
nothing on a stack "needs no `gs_sp`" and `EmitSpec` rejects one that
allocates without it ([implementation.md §6.4](../implementation.md#64-the-calling-convention-c3)).
That is the **stackless** shape.

The other shape is what the thread machinery already builds: a thread
program is compiled "as a separate program with its own stack assignment
(its own N′)", and "each thread carries a pointer to its own block of
N′ stacks" ([spec §11.2](../goose_spec.md#112-concurrency)). A body with
growable locals can be compiled the same way, as a **child program** whose
block a pool thread reserves once and keeps, and the functions it calls
specialize per program as they do for workers today ("any static stack use
specializes it again per program, like a template instantiation"). The
one difference from a worker is where its references point: into the
parent, which is what [2.1](#21-a-dormant-parent-freezes-every-lifetime)
makes safe.

### 2.5 Program instances, and what a pool thread must adopt

Every global that is not a static lives in one `gs_globals_t` per program
instance, reached through the thread-local `gs_gl` in a program with
workers, and a thread program's stacks are reached through the
thread-local `gs_stks` ([implementation.md §6.3](../implementation.md#63-globals-and-program-instances),
[§7](../implementation.md#7-the-runtime)). A pool thread running a child of
instance X sets its `gs_gl` to X's globals for the task and, for a
stackless child, its `gs_stks` to X's block (the child never allocates on
it, but a captured resizable global's header is read through `gs_gl`, and
a captured resizable local's header and stack arrive as free variables
either way). Two thread-local stores per task; nothing else about
instances changes. A child program's `gs_stks` is its own kept block. The
read-only statics (`static const` globals, string literals) are shared
already.

### 2.6 Backends without threads run the loop

"Programs with threads need a real C compiler (`goose build`), since the
in-process TinyCC backend cannot place thread-local storage"
([reference §22](../reference.md#22-threads)). Neither `par` nor `simd`
has a meaning of its own, so under TinyCC and wasm a loop marked with
either runs as the plain loop, iteration by iteration on the calling
thread, and in any build `GOOSE_THREADS=1` does the same to every `par`;
the program's output is the same. This is also the debugging mode: a
marked loop that gives a different answer at `GOOSE_THREADS=1` or under
`goose run` has broken a rule the compiler should have caught, which is a
compiler bug, never a program bug. `goose run` keeps working on every
program; `goose build` parallelizes.

### 2.7 What the checker knows about effects today

What a body does to the world outside it is tracked in three places and
summarized in none: the checker's `FnSpec` summaries record shrinks,
grows, stores and return roots per specialization
([analysis_inventory.md §2](analysis_inventory.md#2-what-is-tracked-where));
codegen's `CollectSpecs` computes, to a fixpoint over the call graph, the
free variables and stack-owning globals a specialization reaches; the
optimizer records writes and address-taking per variable. What is missing
is the one-bit-per-kind summary that llm_frontier's `pure` proposes
([§3.4](llm_frontier.md#34-effects-pure-and-frames)): writes a global,
calls a queue or thread builtin, prints, calls an extern that is not
`pure`. A child body needs exactly that summary, over itself and its
callees, and it is the one piece of analysis both features share.

## 3. What others built

### 3.1 Thread level and lane level, side by side

| System | Thread level | Lane level | Who chooses the split | Nesting |
|---|---|---|---|---|
| ISPC ([ispc], [ispc-paper]) | `launch[n] f()` runs `n` tasks of a `task` function, `taskIndex`/`taskCount`; `sync` or the function's return joins them, "as in Cilk"; the task system is user-supplied C++ (pthreads, TBB, OpenMP, GCD, ConcRT backends in `tasksys.cpp`, [ispc-tasksys]) | `foreach (i = a ... b)` maps the domain onto the gang's lanes; `foreach_tiled`, `foreach_active`, `foreach_unique` for coherence and per-lane serialization; nested `foreach` is illegal | the programmer, both; the guide says to launch "many more tasks than there are processors ... but not so many that the overhead ... dominates" | lexical: `mandelbrot_tasks.ispc` computes a row span per `taskIndex` and runs `foreach` inside it ([ispc-mandel]); the span is padded "to avoid false-sharing" |
| Mojo ([mojo-matmul]) | `parallelize[calc_row](C.rows, num_workers)` over a runtime pool (`parallelism_level()`, [mojo-runtime]) | `vectorize[dot, nelts, size = C.cols]()` inside the row closure, `nelts` from `simdwidthof` ([mojo-vectorize]) | the programmer, both widths; `num_workers` is usually the core count | lexical, one closure inside the other |
| Halide ([halide], [halide-blur]) | `.parallel(y)` on a split dimension; the runtime's `halide_do_par_for` with a lazily started pool, the caller runs jobs too ([halide-pool]) | `.vectorize(x, 8)` on another split dimension | the programmer or the autotuner, in one schedule chain: `blur_y.split(y, y, yi, 32).parallel(y).vectorize(x, 16)` | one loop nest; "any dimension may be scheduled serial, parallel, or vectorized" ([halide-pldi13]) |
| OpenMP ([omp-parallel], [omp-for-simd], [omp-pfs]) | `#pragma omp parallel for` with `schedule(static[,chunk])`, `dynamic`, `guided`; the encountering thread is thread 0 of the team and runs iterations | `#pragma omp simd` with `safelen`, `simdlen`, `aligned`, `reduction` | the programmer; the combined `parallel for simd` "will first distribute the logical iterations ... across threads", then "each resulting chunk of iterations will then be converted to a SIMD loop" | one pragma for one loop, or `parallel for` outside and `simd` inside |
| C++17 ([cpp-policy], [cpp-draft]) | `std::execution::par` | `unseq` (C++20); `par_unseq` is both, and forbids "vectorization-unsafe" operations such as locking a mutex, which is undefined behaviour under it | the programmer, by policy tag; the library chooses the pool (libstdc++ needs TBB, [gcc9]; MSVC uses the Windows thread pool, [msvc-exec]) | none |
| TBB ([tbb-pfor], [tbb-part]) | `parallel_for(blocked_range, body, partitioner)` with `auto`, `simple`, `static` and `affinity` partitioners; work stealing; the calling thread participates | the C compiler's autovectorizer on the body | the programmer, with a grain rule: "grainsize iterations ... should take at least 100,000 clock cycles" ([tbb-grain]) | `parallel_for` inside `parallel_for` through stealing |
| Cilk ([opencilk-spec], [cilk5]) | `cilk_for`, divide-and-conquer over `cilk_spawn`/`cilk_scope`, `#pragma cilk grainsize`; a spawn costs "between 2 and 6 times the cost of a C function call" | Cilk Plus's `#pragma simd` and array notation, removed in GCC 8 and ICC 19.1 ([gcc8], [icc191]) | the programmer | lexical |
| Rust rayon ([rayon-join], [rayon-chunks]) | `par_iter_mut`, `par_chunks_mut`, `join`, `scope`; a global pool started lazily with one thread per logical CPU; `join` runs one closure on the caller and costs about 15 ns ([spice]) | the autovectorizer | the programmer | lexical |
| Chapel ([chapel-forall], [chapel-foreach]) | `forall`: the leader iterator runs a `coforall` over `dataParTasksPerLocale` tasks, one per core by default | `foreach`: "loop iterations are order-independent. This enables the compiler to vectorize"; the per-task inner loop of a `forall` is a `foreach` ([chapel-both]) | `forall` is one construct that gives both; `foreach` alone gives lanes; `coforall` is one task per iteration | automatic inside `forall` |
| Futhark ([futhark-perf], [futhark-msc], [futhark-ispc]) | `map`: the multicore backend chunks the outer parallel operation across threads, with two compiled versions of any nested parallelism chosen at run time | the ISPC backend turns the per-chunk sequential loop into a `foreach` | the compiler, both | automatic; the compiler flattens |
| Julia ([julia-threads], [julia-simd]) | `Threads.@threads` with `:static` (one task per thread) or `:dynamic` scheduling; a spawn costs "typically less than 10 microseconds" | `@simd` on the innermost loop, a promise that iterations "are independent and may be reordered" | the programmer, both | lexical |

Three shapes, then: two constructs placed by the programmer (ISPC, Mojo,
Julia, Cilk, rayon), one schedule naming both dimensions (Halide,
OpenMP's combined pragma), and one construct where the compiler or the
iterator decides (Chapel, Futhark). All three produce the same code for a
1-D kernel: contiguous chunks per thread, lanes within a chunk. They
differ in who writes the chunking. The measured gains from the two levels
together are the product of the two: ISPC's own suite on a 4-core AVX
machine goes from 4-8x with lanes alone to 15-33x with tasks as well
([ispc-perf]); Futhark's ISPC backend adds 1.7-9.5x on top of its thread
chunks for the kernels that vectorize and loses 20-40% on Barnes-Hut,
which does not ([futhark-ispc], [futhark-0-21-13]).

### 3.2 The safety rule: who may write what

Every system that lets children see a parent's stack blocks the parent in
a lexical construct ([2.1](#21-a-dormant-parent-freezes-every-lifetime)).
What they then do about writes splits three ways:

* **Capability types.** Rust's `Send`/`Sync` ([rust-sync]), Swift's
  `Sendable` ([se-0302]), Pony's `iso`/`val` ([pony-caps]), Verona's
  regions ([verona]): shared reads of immutable data are free, a write
  needs unique ownership of the whole value. None of them can express
  "iteration `i` writes `out[i]`". Rust gets it through the library,
  `split_at_mut` and `chunks_mut`, whose "chunks are mutable slices, and do
  not overlap" ([rust-chunks]), implemented once with `unsafe`; rayon's
  `for_each` takes an `Fn` closure, so a captured `&mut [T]` cannot be
  written through it at all, and `par_chunks_mut` or `par_iter_mut` is the
  answer ([rayon-foreach], [rayon-faq]). Swift considered allowing child
  tasks to mutate disjoint captured variables and declined: "Swift's
  `@Sendable` closure checking has to be conservative, unless we give it
  special knowledge" ([se-0304]); in Swift 6, disjoint writes to one array
  from child tasks are an error and the workaround is an unsafe buffer
  pointer ([swift-forum]).
* **Proof.** Deterministic Parallel Java proves it with region and effect
  annotations: "The value of `i` is different in each loop iteration, so
  the updates are all to disjoint regions" ([dpj-tutorial], [dpj]).
  ParaSail proves slice non-overlap at the call site ("Because the compiler
  can prove `Left > Right`, there is no overlap between the two slices")
  and forbids aliasing between any two parameters of which one is
  updatable ([parasail-qs], [parasail-embedded]). The compiler literature
  has the GCD and Banerjee tests and the Omega test for the general affine
  case ([gcd], [banerjee], [omega]); Goose's bounds-check prover is a
  domain of the same family, applied to a simpler question.
* **Assertion.** OpenMP, Chapel's `forall` ("the programmer asserts that
  the loop's iterations can execute safely in parallel", [chapel-forall-guide]),
  `#pragma GCC ivdep`, clang's `vectorize(assume_safety)`, Intel's `ivdep`:
  the programmer promises, and a wrong promise is undefined behaviour
  ([gcc-ivdep], [llvm-parallel]).

Goose has the second kind for free in its prover and the first kind in its
writability inference. The proposal uses both and never the third.

### 3.3 Pools

Every runtime surveyed starts its pool lazily, sizes it to the online
cores less one, and has the forking thread run a share of the work: ISPC's
pthreads backend launches "one fewer thread than there are cores, since the
main thread here will also grab jobs" ([ispc-tasksys]); libgomp's team
adds "current (master) thread to `threads[]`" and the outlined region is
called by the encountering thread itself ([libgomp-parallel],
[libgomp-team]); OpenMP's specification makes the encountering thread
thread 0 of the team ([omp-parallel]); Halide's `halide_do_par_for` has
the caller run jobs until its own is done ([halide-pool]); rayon's `join`
runs one closure on the caller ([rayon-join]); TBB's `execute` has "the
calling thread join[] the task_arena" ([tbb-arena]).

Between forks the workers spin and then park: libgomp spins 300,000 times
by default and 1,000 when oversubscribed ([gomp-spin], [omp-wait]); LLVM's
libomp spins for `KMP_BLOCKTIME`, 200 ms ([kmp-blocktime]); Halide's
condition variable "spins before blocking", which "helps reduce overhead
at the end of a parallel for loop" ([halide-pool]). The cost of not
spinning is the wake-up: a rerun of the EPCC microbenchmarks measures a
`parallel for` at 0.5-1 µs on 8 threads of a Ryzen 5950X with both
runtimes, and on an Apple M1, where libomp does not spin by default, 6.7
and 25.8 µs on 4 and 8 threads against 0.36 and 1.63 µs with spinning
forced on ([epcc-rerun]). The measurements of
[1.1](#11-what-share-nothing-costs-on-a-kernel) agree: a parked wake costs
1-2 µs per child, a spinning handoff a tenth of that.

Work stealing (Chase and Lev's deque, [chase-lev]) is what TBB, Cilk,
rayon and Julia use to balance uneven iterations and nested parallelism.
Halide does without: "The source does not implement classic work
stealing", one global mutex and a job stack ([halide-pool]), and Futhark's
multicore backend shares work statically at the top level and steals only
for nested or irregular parallelism ([futhark-msc]). For static kernels the
simpler pool is as fast, and it keeps the chunk-to-thread mapping fixed,
which [4.6](#46-results-reductions-determinism) wants.

### 3.4 Reductions

A float sum in parallel is order-dependent everywhere: OpenMP says "the
order in which values are combined are unspecified" and "there is no
guarantee that bitwise-identical results will be obtained" between runs
([omp-reduction]); rayon's `sum` is "not fully deterministic" for floats
([rayon-sum]); Julia's `reduce` has implementation-dependent associativity
([julia-reduce]). The fixes are a fixed combination order for a fixed
thread count (LLVM's `KMP_DETERMINISTIC_REDUCTION`, [kmp-blocktime]) and
Cilk's reducers, which keep "the left-to-right order" of the serial program
while leaving "the grouping (associativity) of the operations"
unspecified ([opencilk-spec], [opencilk-reducers]). Integer reductions are
exact in any order; that is the line Goose draws
([4.6](#46-results-reductions-determinism)).

### 3.5 What to take

* The parent blocks in the construct; the construct is a loop. Rust's
  closure-based `scope` and Halide's and OpenMP's outlined loops are the
  shape, not Rust's original guard or ISPC's `launch`/`sync` pair, which
  Goose can add later as a statement form ([6](#6-options-side-by-side)).
* Disjointness by construction, as rayon's `chunks_mut` and ParaSail's
  slice splits give it, before disjointness by proof. The structural form
  costs nothing and covers partitioned work; the proof covers stencils and
  strided writes.
* The combined form for one loop (threads over chunks, lanes within),
  which every system converges on for 1-D kernels, and lexical nesting
  for two loops. The programmer names the level, `simd`, `par` or both,
  on the loop where the parallelism is; the compiler does not guess at
  loop nests it was not told about.
* Lazy pool, cores less one, caller runs chunk 0, spin then park, no
  stealing in v1.
* Integer reductions only; floats go through per-iteration elements.

## 4. The design

### 4.1 One construct, two levels

The construct is a `for` loop whose iterations the programmer declares
independent, with one or two words in front that say where those
iterations may run:

```goose
simd for i in n { ... }             // lanes: this thread, iterations on SIMD lanes
par for i in n { ... }              // threads: contiguous chunks on the pool, a plain loop in each
par simd for i in n { ... }         // both: chunks on threads, lanes within each chunk
```

Each takes every range form `for` has:

```goose
par for i in a..b { ... }           // a range
par for x in arr { ... }            // element copies: the body reads arr
par for &x in arr { ... }           // element references: iteration i owns arr[i]
par for &x, i in arr { ... }        // with index
```

The meaning is the `for` loop's. Both words are permissions, not
instructions: the compiler may run the iterations as the word says once it
has checked that doing so is unobservable, and may always run them
sequentially instead. `simd` on a loop is `simd` on a function
([spec §7.12](../goose_spec.md#712-simd-fn-versions-per-instruction-set))
scaled down to one loop, with the same rule: no meaning of its own, every
lowering computes what the baseline computes. The rules a body must meet
are the same for all three spellings and are in
[4.3](#43-what-a-body-may-touch) to [4.7](#47-aborts). The body is a
block of the enclosing function, as a `for` body is: it sees the
function's locals and parameters and the program's globals, declares its
own locals, calls what it likes, and ends with the iteration.

What the construct does *not* have: a thread count, a chunk size, a
schedule, a lane width, or a reduction clause. The thread count is the
pool's ([4.9](#49-the-runtime)), the chunking is static and contiguous,
the lane width is the C compiler's or the enclosing `simd fn` version's,
and a reduction is a shape the compiler recognizes
([4.6](#46-results-reductions-determinism)). OpenMP's `schedule(dynamic)`
and TBB's partitioners answer uneven iterations; Goose's answer is in
[4.9](#49-the-runtime), without syntax.

### 4.2 Which level a loop gets

The keywords decide; nothing is inferred from nesting or from the body.
The four ways to write it:

**Lanes only: `simd for`.** The loop runs on the calling thread, and its
iterations may be spread over SIMD lanes. In v1 that means the loop is
emitted with the C compiler's "iterations are independent" assertion
(`#pragma clang loop vectorize(assume_safety)`, `#pragma GCC ivdep`) and
`restrict` on the arrays it writes, so the vectorizer no longer has to
prove what the checker proved; later it means the SPMD lowering of
llm_frontier §4.4, where the body is compiled per lane width with masks
for its `if`s. No thread is started, nothing is forked, and the loop
costs what a `for` costs to enter. This is the form for an inner loop
over pixels, for any loop that already runs inside a child, and for a
loop the programmer wants vectorized without threads:

```goose
fn scale(xs: f32[:], s: f32) {
    simd for &x in xs { x = x * s; }        // one thread; lanes; iteration i owns xs[i]
}
```

**Threads only: `par for`.** The range is cut into contiguous chunks, one
per pool thread plus the caller, and each thread runs its chunk as an
ordinary loop, which the C compiler may or may not vectorize as it would
any loop; no independence assertion is given, because `simd` was not
written. This is the form for an outer loop over rows or over `chunk`
indices, and the only form whose body may hold growable locals
([4.5](#45-stackless-and-stackful-bodies)):

```goose
fn count_distinct(rows: const i64[:], bits: i64, distinct: i64[:]) {
    par for k in P {                        // threads; one chunk of rows each
        var table: Slot[>..] = [];          // growable: a child program, which has no lane form
        ...                                 // the body is in section 5
        distinct[k] = n;
    }
}
```

**Both on one loop: `par simd for`.** Chunks on threads, and each
thread's chunk loop is a `simd for`. This is OpenMP's `parallel for simd`
([omp-pfs]) and what every system in
[3.1](#31-thread-level-and-lane-level-side-by-side) emits for a 1-D
kernel; it is the form for a flat loop over a large array:

```goose
fn saxpy(ys: f32[:], xs: const f32[:], a: f32) {
    assert(xs.len == ys.len);
    par simd for &y, i in ys { y = a * xs[i] + y; }
}
```

**Both on a nest: `par for` outside, `simd for` inside.** Rows on
threads, pixels on lanes; the blur in
[5](#5-the-query-engines-phases-written-out) is written this way. This is
ISPC's `launch` with `foreach` inside, Mojo's `parallelize` with
`vectorize` inside, Halide's `.parallel(y).vectorize(x)`. The outer
loop's chunks are bands of rows per thread, and the inner `simd for` runs
inside each child, on that child's thread.

What nesting does, and does not do:

* A `par for` that runs while its thread is already inside a child,
  whether written in the outer body or reached through a call, does not
  fork: it runs sequentially, as OpenMP's nested regions do by default. A
  function containing a `par for` may therefore be called from anywhere;
  from inside a child it costs a loop. The runtime knows from a
  thread-local flag, which the parent's own thread sets while it runs
  chunk 0 ([4.9](#49-the-runtime)).
* A `simd for` whose body reaches a `par for` is not vectorized: a fork is
  a call the lane lowering cannot inline, so the loop runs as written and
  the fork happens inside it.
* A `simd for` inside a `simd for` body is a plain loop in each lane. ISPC
  makes a nested `foreach` an error ([ispc]); Goose has nothing to gain by
  refusing it.
* A plain `for` around a `par for` forks once per outer iteration. The
  compiler does not hoist a fork out of a loop it was not told about; if
  the forks are the cost, mark the outer loop instead.

A reader knows a loop's lowering from its first word, and the compiler
never promotes a loop that was not marked. What llm_frontier §4.3
sketched as `par for` is this note's `par simd for`, with threads and
lanes named separately so that each can be asked for alone; its §4.4
option 2, the SPMD lowering, is the second lowering of `simd for`.
Nothing else there changes except the thread pool, which is the one this
note specifies, and the admission rules, which are the ones below.

### 4.3 What a body may touch

Inside the body of any of the three spellings, with `i` the loop
variable:

* **R1. Captured places are read-only.** Every variable of the enclosing
  function and every global is `const` inside the body, as a by-value
  `for` binding is: no assignment, no `push`, no `resize`, no shrink, no
  write through a reference or slice read out of them. The diagnostics are
  the writability rule's.
* **R2. What this iteration owns is writable.** Three ways to own a place:
  * the element a `for &x` form binds (`x` is `arr[i]`, and no other
    iteration binds it);
  * a chunk a chunking builtin hands out from a captured array or slice
    with `i` as its index ([4.4](#44-disjoint-writes-structural-first-proven-second));
  * a place indexed by an expression of `i` for which the prover shows
    that no other iteration reads or writes it
    ([4.4](#44-disjoint-writes-structural-first-proven-second)); `out[i]`
    is the trivial case.
* **R3. No effects but writes.** The body and everything it calls may not
  `qput`, `qget`, `qpoll`, `thread_spawn`, `thread_wait`, `print`,
  `write_stderr`, `exit`, or call an `extern fn` that is not `pure`. An
  iteration's only effect on the world is its writes to the places R2
  gives it; otherwise the schedule would be observable.
* **R4. No exits across the boundary.** No `return`, no `return … from`
  a function outside the body, no `break` or `continue` of an enclosing
  loop. `continue` of the marked loop itself ends the iteration; `defer`
  inside the body runs at the iteration's end.
* **R5. The body's own locals are its own.** A local declared in the body
  exists per iteration, on the thread's native stack or, for a nonfixed
  one, on the child program's stacks ([4.5](#45-stackless-and-stackful-bodies)).

R1 and R2 are one rule: a write is accepted if its place is owned by the
iteration. R3 is the effect summary of
[2.7](#27-what-the-checker-knows-about-effects-today). R4 is what `return
from` already tracks per specialization (`FnSpec::needs`), with a new
target that is never allowed. The reader of a `par for` learns from the
keyword alone that nothing in the body touches anything another
iteration can see, which is what the issue's point 1 to 3 asked the
compiler to check.

A body that violates R1 is the common first attempt, and the diagnostic
should say what to do: "`total += x` in a `par for` writes a variable of
the enclosing function; make `total` a reduction (an integer accumulated by
one operation, [4.6](#46-results-reductions-determinism)) or write a
per-iteration element `partials[i]`".

### 4.4 Disjoint writes: structural first, proven second

Two sources of "no other iteration touches this place".

**Structural.** Three builtins, on any array-family value or slice, whose
disjointness the checker knows as a fact about the builtin, not something
it proves:

| Builtin | Meaning | Disjointness |
|---|---|---|
| `a.chunk(k, n) -> T[:]` | the `k`-th of `n` contiguous runs of `a`, lengths differing by at most one, covering `a` | distinct `k` give disjoint runs |
| `a.chunk_at(bounds, k) -> T[:]` | `a[bounds[k]..bounds[k + 1]]` for a `bounds: const i64[:]` that is non-decreasing and within `a` | distinct `k` give disjoint runs; the bounds are checked once at the fork (monotone and in range), as an index type is checked once when made |
| `for &x in a` | the element `a[i]` | distinct `i` give distinct elements |

Inside a `par for k in n`, `out.chunk(k, n)` is a writable slice of `out`
that this iteration owns, and `src.chunk(k, n)` is a read-only view of
`src` (R1) of the same shape. Any number of arrays can be chunked the same
way, which is how a scan reads one column and writes another, and how a
probe reads keys, reads a shared table, and writes matches. The equal-parts
form is rayon's `par_chunks_mut` ([rayon-chunks]); the bounds form is what
a partitioned hash table or a CSR row structure needs, and its one check at
the fork is the price of taking a runtime array as the partition.

Outside a `par for` the same builtins are ordinary slicing. They are
useful on their own (`for k in n { let part = a.chunk(k, n); ... }`) and
cost nothing to add to the standard library today, ahead of the construct.

**Proven.** The two-iteration check of llm_frontier §4.3 rule 3 and
[analysis_inventory.md §5.6](analysis_inventory.md#6-loops-two-iteration-check-instead-of-name-scans-small-effort-independent):
for `i1 < i2`, every write place of iteration `i1` is disjoint from every
read or write place of `i2`, judged in the bounds-check prover's domain
(`Lin` bases and offsets). It admits `out[i]`, `out[i * 4 + b]` for `b in
0..4`, `dst[y * W + x]` for `x in 1..W - 1`, and a row slice
`dst[y * W..(y + 1) * W]` taken inside the body, since `y1 + 1 <= y2`
gives `(y1 + 1) * W <= y2 * W`. Arrays reached through two captured
references of the same type need `distinct` roots
([llm_frontier §4.1](llm_frontier.md#41-distinct-roots-as-restrict)) or
the check fails; writes to two different arrays need nothing. The
structural builtins are the cases this prover would find hardest
(division by `n`, a runtime `bounds` array), which is why they are facts
rather than theorems.

Rust's experience says the structural form is what most parallel code
uses, and Swift's says a language that offers only the capability form
leaves disjoint array writes to unsafe code
([3.2](#32-the-safety-rule-who-may-write-what)). DPJ shows the proof form
is sound and expressible ([dpj-tutorial]). Goose can have both, and in
that order.

### 4.5 Stackless and stackful bodies

**Stackless.** A body whose locals are all fixed-size, through every
function it calls, needs no data stack: it is the recursion rule's shape
([2.4](#24-two-body-shapes-the-stack-model-allows)). It runs on a pool
thread's native stack, reserves nothing, and may be a `simd for`. This is
every kernel in the issue's list (scan, filter into a chunk with a count,
partition into chunks of a table, probe, merge of per-worker states), the
blur and Sobel of `samples/22_image_filters.goose`, Mandelbrot rows, and
the particle update.

**Stackful.** A body with a nonfixed local (a scratch `u8[>..]`, a local
hash table, a `str()` result) is compiled as a **child program**: its own
stack assignment with N″ stacks, like a `thread_fn`'s N′
([spec §11.2](../goose_spec.md#112-concurrency)), with these differences:

* Its references and slices point into the parent, and it copies no
  globals and no arguments: it receives the parent's environment by
  pointer, as a stackless body does.
* Each pool thread reserves one block of N″ stacks the first time it runs
  a child program, where N″ is the largest any child program in the
  program needs, and keeps it for the program's life. Between tasks the
  block is rewound to its base, which keeps its pages committed
  ([spec §1.2](../goose_spec.md#12-memory-model)), so the second pass pays
  no page faults; `trim_memory()` from the parent would ask the pool to
  trim its blocks too. This is the issue's point 4.
* The chunk the parent's own thread runs needs no block: from the
  parent's point of view it is a call, and its nonfixed locals go on the
  parent's stacks above the parent's watermarks, where a callee's would.
* It runs on threads only: `par for`, never `simd for` or `par simd for`.
  A nonfixed local has no lane form.
* `hardware_threads()` already reports "no more workers than the
  implementation's address space for data stacks holds beside the main
  program" ([spec §11.2](../goose_spec.md#112-concurrency)); the pool's
  size for child programs is capped the same way, with N″ in the place of
  N′. A stackless pool has no such cap. The budget is 32 TB by default and
  a block is N″ times 2 GB, so the cap is in the thousands and never
  binds on a pool the size of the core count.

The programmer sees one rule: "a body with growable locals may be `par`,
not `simd`". The compiler sees two specializations of the
same body shape it already has (a function in the main program, a function
in a thread program), and one new one (a function in a child program,
whose stack indices are its own and whose globals are the parent's).

### 4.6 Results, reductions, determinism

How a value gets out of an iteration, in order of preference:

1. **An element.** `found[k] = n`, `partials[k] = acc`, `states[k] =
   state`: a fixed-size result per iteration into a parent array, owned
   by R2's trivial case. This is how per-worker counts, per-worker
   aggregation states and per-chunk float partial sums come back, and the
   parent combines them in whatever order it writes down, so a float sum
   is deterministic because the program chose its order.
2. **A chunk, partially filled.** A filter writes the survivors of its
   input chunk into the same-shaped output chunk and reports the count
   (example in [5](#5-the-query-engines-phases-written-out)); the parent
   compacts, or the next phase takes the counts as its bounds through
   `chunk_at`. Memory is the input's size, the output is written in place,
   and nothing is copied. This is the GPU and ISPC idiom for compaction.
3. **An integer reduction.** `total += f(i)`, `count++`, `bits |= m`,
   `best = max(best, v)`: a local integer of the enclosing function
   accumulated by one associative operation and not otherwise read in the
   body. The compiler keeps one accumulator per chunk and combines them in
   chunk order at the join. Integers are exact in any order, so the result
   is the sequential result. Floats are **not** admitted as reductions:
   they go through (1), where the order is the program's. This is the
   exactness rule of `simd` ([spec §7.12](../goose_spec.md#712-simd-fn-versions-per-instruction-set))
   applied to threads, and it is stricter than every system in
   [3.4](#34-reductions) because Goose's claim is that the schedule is
   unobservable, not merely that the answer is close.
4. **A growable result of a child program.** A body that builds a
   `T[>..]` and wants it kept has no place to put it: the parent's arrays
   cannot grow (R1), and the child's stacks are rewound at the iteration's
   end. The v1 answer is (2): size the output from the input, write into a
   chunk, report a count. A later `par for k in n into out { ... }` form,
   which appends each iteration's result image to `out` in iteration
   order at the join, is one copy per result on the parent's thread, the
   `qget` copy without the queue and without the input copies; it is
   listed in [8](#8-open-questions), not proposed.

Determinism follows from the rules: iterations have no effects but writes
to places only they own (R1 to R3), reductions are exact, and the chunk
count never changes a result. A program may therefore be run with
`GOOSE_THREADS=1`, under TinyCC, or on a machine with another core count,
and print the same bytes.

### 4.7 Aborts

llm_frontier §4.3 required the body to be `abortfree`, so that no
iteration could end the program at a schedule-dependent moment. For the
thread level this note relaxes it, because the code the issue wants to
run has bounds checks the prover cannot always remove, and the relaxation
costs exactly one thing:

* An abort in a child (a failed check, an `assert`, `abort(msg)`, a
  division by zero) ends the program as an abort on main's thread does.
  Whether the sequential loop would have aborted is a property of the
  program, not of the schedule: every iteration runs in every schedule,
  and the iterations before the aborting one have no observable effect but
  their writes, which a dead process does not show. What *is*
  schedule-dependent is **which** abort is reported when two or more
  iterations would abort, and the spec says so: the message is one of
  them, unspecified which.
* If the program instance is a contained worker
  ([reference §22](../reference.md#22-threads)), the abort must end that
  worker and not the process. The pool thread records the abort and
  finishes its chunk early; the parent's thread, at the join, raises it as
  its own, after which the existing contained-abort path runs unchanged.
  Other children finish their chunks, which are bounded.
* Stack overflow on a pool thread's native stack, `exit(code)` and runtime
  failures end the program, as they do for workers.

For `simd for` the stricter rule stays where it matters: an SPMD lowering
needs the body to have no kept check, or the lowering is the scalar loop.
With the v1 lane lowering being the C compiler's vectorizer plus an
assertion, a kept check simply means the loop does not vectorize, as
today.

### 4.8 In the spec

A new section beside §11.2, stating:

* A loop marked `simd`, `par` or `par simd` computes what `for` computes;
  an implementation may run its iterations in any interleaving on any
  number of threads of the same program instance (`par`), on SIMD lanes
  (`simd`), or sequentially; the program's output, aborts and exit status
  do not depend on which.
* Admission: R1 to R5, the chunking builtins and their facts, the core of
  accepted index shapes for the proven case (as §7.14 lists shapes for
  `checkfree`), the reduction shape, the abort rule.
* Nesting: a `par for` that runs inside a child is not a fork; it runs
  sequentially. A `simd for` inside a child is a lane loop on that child's
  thread.
* Inside a `thread_fn` worker, the rules are the same; the pool is the
  process's ([4.9](#49-the-runtime)).
* The data-race promise, restated: a program has no data race because
  threads share nothing (§11.2) and because the iterations of a marked
  loop write only places the compiler showed they alone own. Both are checked;
  neither is trusted.

Nothing about queues, workers or globals changes.

### 4.9 The runtime

A few hundred lines of C beside `runtime_threads.h`:

* **The pool.** `hardware_threads() - 1` native threads, created at the
  first fork of the process, never destroyed, parked between forks on one
  word (`WaitOnAddress` on Windows, [waitonaddress]; a futex on Linux,
  [futex]; `os_sync_wait_on_address` or a condition variable on macOS)
  after a bounded spin. Native stacks
  the size of main's: Windows gives a created thread the executable's
  reserve (64 MB since `goose build` sets it); POSIX needs
  `pthread_attr_setstacksize`, since a default pthread stack is 8 MB on
  glibc and 512 KB on macOS, and a child body may call anything.
* **A fork.** The parent writes a job (the outlined function, the
  environment pointer, the range, the chunk count), bumps the generation
  word, wakes all, runs chunk 0 itself, then waits on a done counter. A
  child adopts the parent's `gs_gl` and stack block
  ([2.5](#25-program-instances-and-what-a-pool-thread-must-adopt)), runs
  its chunk, increments the counter, and wakes the parent when it is the
  last. Fork is a release, join an acquire; nothing else in the program
  needs an atomic. The whole protocol is `bench/fork_cost/pool_cost.c`,
  which the numbers in [1.1](#11-what-share-nothing-costs-on-a-kernel)
  come from.
* **Chunks.** Static and contiguous: chunk `c` of `P` is
  `range.chunk(c, P)`. For uneven iterations (Mandelbrot rows), a second
  mode claims fixed-size blocks from an atomic counter; the mapping of
  blocks to threads then varies, but no result depends on it
  ([4.6](#46-results-reductions-determinism)), so the mode is a runtime
  choice (`GOOSE_PAR_BLOCK=n`) and never syntax. No stealing in v1.
* **Busy pool.** A fork claims the pool threads that are idle at that
  moment and runs the remaining chunks on its own thread. A pool thread
  never forks, nor does the parent's thread while it runs chunk 0
  ([4.2](#42-which-level-a-loop-gets)), so no thread waits on the pool
  from inside a task, and there is no deadlock to reason about. A
  `thread_fn` worker that forks uses the same pool; with several workers
  forking at once the pool is shared and the cores are oversubscribed at
  worst, as they would be under OpenMP's nested regions.
* **Stack blocks for child programs.** One per pool thread, reserved on
  first use, N″ regions, rewound after each task, trimmed on request
  ([4.5](#45-stackless-and-stackful-bodies)).
* **False sharing.** Two chunks that meet in one cache line cost a few
  bounces at the boundary and nothing else; ISPC pads its spans "to avoid
  false-sharing" ([ispc-mandel]). A `chunk` of a byte array could round
  its boundaries to 64 bytes; this is a tuning, not a rule.
* **Knobs.** `GOOSE_THREADS=n` (1 runs every `par for` sequentially),
  `GOOSE_SPIN=n` spins before parking, both read once at the first fork.
  No API: a program that wants fewer threads is asking for a deployment
  setting, as `OMP_NUM_THREADS` is.
* **Not provided.** Per-child state, thread ids visible to the program,
  barriers inside a body, atomics, and the GPU-style workgroup features of
  llm_frontier §4.5. None has a sequential meaning.

### 4.10 What the compiler needs

| Where | What | Exists | New |
|---|---|---|---|
| parser | `simd for`, `par for`, `par simd for` in `for`'s forms | `for`, `simd` | one keyword, one new position |
| checker | R1: captured places `const` inside the body | writability inference, by-value `for` bindings | a boundary that re-views every captured variable, ~100 lines |
| checker | R2 structural: `chunk`, `chunk_at`, `for &x`; the fact that these are owned | slicing, `for &x` | three builtins with a "disjoint per index" mark, and the ownership check at a write, ~200 lines |
| checker | R2 proven: two-iteration disjointness | the prover's domain, `HandedApart`, `DistinctTypes` | the judgement and its diagnostics, ~1,000 lines (llm_frontier's estimate) |
| checker | R3: effect summary over callees | reachable globals (codegen), writes (optimizer) | one summary per specialization, shared with `pure`, ~300 lines |
| checker | R4, R5, reductions, aborts, nesting | `needs` for `return from`, cycle rules | ~200 lines |
| codegen | outline the body with an environment struct; the chunk loop; the fork call | nested-function free variables, thread thunks, `simd` versions | ~400 lines |
| codegen | child programs: a third program kind | thread programs' per-program specialization and stack numbering | ~300 lines |
| codegen | `simd for`: `restrict` from `distinct` roots and an independence assertion on the loop | §4.1's `restrict`, `simd` versions | ~50 lines; SPMD later, per llm_frontier §4.4 |
| runtime | the pool, forks, stack blocks, knobs | `runtime_threads.h`'s threads and regions | ~300 lines of C, three platforms |
| spec, reference, tests | the section, the diagnostics, `test/errors_tc` fixtures per rule, a `--par-test` report | | |

The first row group that ships, structural R2 plus R1, R3 to R5, the
outlining and the pool, is about 1,500 lines of compiler and runtime and
no new analysis. It is enough for every phase in
[5](#5-the-query-engines-phases-written-out).

## 5. The query engine's phases, written out

The issue's workload: scan and filter columns in place into per-worker
outputs; partition build rows and build P disjoint partitions of a hash
table; probe the table as a shared read-only slice from all workers while
writing matches into disjoint output runs; merge per-worker aggregation
states. Each phase in the proposed syntax (the sequential stand-ins of
these, with `par` and `simd` dropped and `chunk` as a function, compile
and run on today's compiler).

**Scan and filter**, stackless, structural. Each iteration reads its chunk
of the column, writes survivors into the same chunk of the output, and
reports how many:

```goose
let P = 8;

fn filter(vals: const i64[:], keep: i64[:], counts: i64[:], threshold: i64) {
    assert(keep.len == vals.len && counts.len == P);
    par for k in P {
        let src = vals.chunk(k, P);             // a const view: R1
        let dst = keep.chunk(k, P);             // this iteration's run of keep: R2
        var n = 0;
        for v in src { if v > threshold { dst[n] = v; n++; } }
        counts[k] = n;                          // one element per iteration: R2
    }
}
```

`dst[n]` keeps its bounds check unless the prover relates `n` to
`src.len` (it is at most the index of `v`), which is a kept check inside
a child: allowed by [4.7](#47-aborts), and the usual `checkfree` work
removes it.

**Probe**, stackless, structural, with a shared table. The table is read
by every iteration at once and written by none:

```goose
struct Slot { key: i64, val: i64 }

fn probe(keys: const i64[:], table: const Slot[:], out: i64[:], found: i64[:]) {
    assert(out.len == keys.len && found.len == P && table.len > 0);
    par for k in P {
        let ks = keys.chunk(k, P);
        let os = out.chunk(k, P);
        var n = 0;
        for key in ks {
            var h = hash(key) as i64 % table.len;
            while table[h].key != 0 && table[h].key != key { h = (h + 1) % table.len; }
            if table[h].key == key { os[n] = table[h].val; n++; }
        }
        found[k] = n;
    }
}
```

**Partitioned build**, stackless, structural with runtime bounds. The
partitions of one table are disjoint runs given by an offsets array; each
iteration fills its own:

```goose
fn build(rows: const i64[:], table: Slot[:], bounds: const i64[:]) {
    par for k in bounds.len - 1 {
        let mine = table.chunk_at(bounds, k);   // bounds checked monotone and in range once, at the fork
        let cap = mine.len;
        for r in rows {
            if partition_of(r, bounds.len - 1) != k { continue; }
            var h = (hash(r) as i64) % cap;
            while mine[h].key != 0 { h = (h + 1) % cap; }
            mine[h] = Slot { key: r, val: r };
        }
    }
}
```

**A stackful build**, a child program: a local table per iteration, for a
group-by whose per-partition size is not known in advance. The body
declares a growable, so it runs on threads with a stack block each, and
reports a fixed-size summary per iteration:

```goose
fn count_distinct(rows: const i64[:], bits: i64, distinct: i64[:]) {
    par for k in P {
        let mine = rows.chunk(k, P);
        var table: Slot[>..] = [];              // a nonfixed local: this is a child program
        table.resize(1 << bits, Slot { key: 0, val: 0 });
        for r in mine {
            var h = (hash(r) as i64) & ((1 << bits) - 1);
            while table[h].key != 0 && table[h].key != r { h = (h + 1) & ((1 << bits) - 1); }
            table[h] = Slot { key: r, val: r };
        }
        var n = 0;
        for s in table { if s.key != 0 { n++; } }
        distinct[k] = n;
    }
}
```

**Merge**, a reduction and per-iteration partials. The count is an integer
reduction; the float sum goes through partials so that its order is the
program's:

```goose
fn totals(a: const f64[:], b: const f64[:], partial: f64[:]) -> i64 {
    assert(a.len == b.len && partial.len == P);
    var nonzero = 0;                            // an integer reduction: exact in any order
    par for k in P {
        let xs = a.chunk(k, P);
        let ys = b.chunk(k, P);
        var acc = 0.0;
        for i in xs.len { acc += xs[i] * ys[i]; if xs[i] != 0.0 { nonzero++; } }
        partial[k] = acc;                       // the float sum comes back per chunk
    }
    return nonzero;                             // sum(partial) is the caller's, in its order
}
```

**Two levels**, the blur of `samples/22_image_filters.goose`: rows on
threads, pixels on lanes. The row slice of `dst` is owned by the proven
rule (rows are disjoint for distinct `y`), the inner `simd for` is the
lane loop, and nothing else is written:

```goose
fn blur(src: const u8[:], dst: u8[:]) {
    assert(src.len == W * H && dst.len == W * H);
    par for y in 1..H - 1 {                     // threads: chunks of rows
        let a = src[(y - 1) * W..y * W];
        let b = src[y * W..(y + 1) * W];
        let c = src[(y + 1) * W..(y + 2) * W];
        let d = dst[y * W..(y + 1) * W];        // proven: rows of dst are disjoint per y
        assert(a.len == W && b.len == W && c.len == W && d.len == W);
        simd for x in 1..W - 1 {                // lanes, on the child's thread
            let s = a[x - 1] as u16 + a[x] + a[x + 1]
                  + b[x - 1] + b[x] + b[x + 1]
                  + c[x - 1] + c[x] + c[x + 1];
            d[x] = (s / 9) as! u8;
        }
    }
}
```

Without the proven rule, the same kernel is written structurally with
`dst.chunk(k, P)` over row bands and a sequential row loop inside, which
is the form the issue's prototype used; the inner `simd for` is unchanged.

## 6. Options side by side

| Option | What it is | Gain | Pain | Verdict |
|---|---|---|---|---|
| A. `par for` with structural ownership, stackless bodies, a static pool | [4.1](#41-one-construct-two-levels)-[4.4](#44-disjoint-writes-structural-first-proven-second) without the prover, [4.5](#45-stackless-and-stackful-bodies)'s first half, [4.9](#49-the-runtime) | the issue's every phase, no copies, 10 µs forks; the core count on partitioned work | ~1,500 lines; three builtins; one keyword | **v1** |
| B. Child programs (stackful bodies) | [4.5](#45-stackless-and-stackful-bodies)'s second half | growable scratch per iteration; the issue's point 4 | a third program kind in codegen; stack blocks per pool thread; the budget cap | **v2**, when A's "size the output from the input" idiom proves insufficient |
| C. Proven disjointness | the two-iteration judgement | stencils and strided writes without restructuring; the reviewer's "parallel because proven" | ~1,000 lines of prover and diagnostics; `distinct` from llm_frontier §4.1 | **v3**, after `requires`/`abortfree` per llm_frontier §7 |
| D. `simd for`, and `par simd for` as the two together | the independence assertion now, SPMD later | the MSVC blur gap at once; ISPC-class gains later | assertion: ~50 lines; SPMD: mask maintenance, per llm_frontier §4.4 | assertion in v1, SPMD when measured |
| E. Pooled `thread_fn` workers given borrowed `const` views, joined by `thread_wait` in the same scope (the issue's literal shape) | today's workers, with reference parameters allowed when every id is waited before the scope ends | no new construct | ids are runtime values flowing through arrays and loops; the checker cannot see the join; the scope would have to become a construct, at which point it is A with worse syntax | rejected; Rust's RFC 3151 made the same choice, "relying on closures rather than guards" ([rfc-3151]) |
| F. A statement form, `par { f(a); g(b); }`, each statement a child (ISPC's `launch`/`sync`, Cilk's `spawn`/`sync`, ParaSail's `\|\|`) | heterogeneous children | pipelines with unlike stages | same rules as A (each statement a body with R1-R5); a second syntax | later, as sugar over A's rules when a program needs it |
| G. Work-stealing tasks | TBB, Cilk, rayon | nested and irregular parallelism balanced automatically | a deque per thread, nondeterministic block-to-thread mapping, no gain on static kernels | not in v1; the atomic-counter block mode of [4.9](#49-the-runtime) covers uneven rows |
| H. Copy-free `qput` of views (a "borrow" message type) | a queue message holding a `const T[:]` with a runtime count of outstanding views | keeps the share-nothing model's surface | a runtime lifetime (refcount) in a language with none; the sender cannot shrink until the count drops, which no rule today expresses | rejected |

## 7. Recommendation and order

1. **The chunking builtins** (`chunk`, `chunk_at`) in the standard
   library, with tests, as ordinary slicing. Useful today; they are the
   vocabulary of everything after.
2. **The effect summary** of [2.7](#27-what-the-checker-knows-about-effects-today),
   shared with llm_frontier's `pure`: one pass over the call graph that
   records global writes, queue and thread builtins, output, non-`pure`
   externs. Measurable by itself through `goose claims` or a flag.
3. **`par for`, option A**: the keyword, R1 to R5 with structural R2, the
   outlining, the pool, `GOOSE_THREADS`, the sequential fallback for
   TinyCC and wasm, the spec section. Fixtures per rule in
   `test/errors_tc`, and a differential run of every `par for` program at
   `GOOSE_THREADS=1` and at the core count, which must print the same
   bytes. Measure the issue's two workloads (reconstructed in `bench/`),
   `samples/22_image_filters.goose`, `samples/23_mandelbrot_threads.goose`
   rewritten without queues, and the `blur`, `particles` and `scene` rows.
4. **`simd for`** with the independence assertion (option D's first half)
   and `restrict` from distinct roots, measured on the MSVC `blur` row;
   `par simd for` is then the two lowerings on one loop.
5. **Child programs** (option B) when a program needs per-iteration
   growables: the third program kind, the kept stack blocks, the cap.
6. **Proven disjointness** (option C) after `requires` and `abortfree`
   exist, so that its diagnostics can name the fact that was missing; a
   `--par-test` report over the benchmark corpus first, to see which loops
   would qualify, as llm_frontier §7 step 6 proposes.
7. **SPMD lanes and the GPU** as llm_frontier §4.4 and §4.5 lay out, as
   backends for the body shape the checker admits by then.

Step 3 is where the issue's workload runs at memory bandwidth across
cores; steps 1 and 2 are small and independent and can land at any time.

## 8. Open questions

* **An unasked assertion on `par for`.** A `par for` body that would be
  admitted as `simd for` could get the independence assertion on its chunk
  loop without `simd` being written; the claim is the same. Left off so
  that a loop's lowering reads from its keywords
  ([4.2](#42-which-level-a-loop-gets)); the first measurement where the
  autovectorizer gives up on a `par for` chunk loop decides.
* **`print` in a body.** Excluded by R3 because the order of lines would
  depend on the schedule (lines never interleave, so the content would
  not). Allowing it under a documented "order unspecified" would help
  debugging at the cost of the "no meaning of its own" claim; the
  alternative is `GOOSE_THREADS=1`, where any `print` outside the body
  tells the story.
* **Growable results** of child programs: the `into out` form of
  [4.6](#46-results-reductions-determinism), one copy per result at the
  join, or nothing.
* **A pool per program instance** rather than per process, when several
  `thread_fn` workers fork at once. Per process oversubscribes; per
  instance multiplies threads. OpenMP disables nested parallelism by
  default; Goose's workers are not nested but sibling programs, which is
  the harder case, and a measurement on a thread-per-core server that also
  forks is the way to decide.
* **A pool under TinyCC.** The runtime object could own the pool and the
  thread-local state the JIT cannot place, so that `goose run` also
  parallelizes. Sequential is correct and simpler; speed in the JIT is not
  what it is for.
* **Float reductions** with a fixed tree over a fixed chunk count, as
  llm_frontier §8 asks: deterministic on one machine, different from the
  sequential sum and between machines with different core counts. The
  answer here is no; partials are one line more.
* **`chunk` boundaries and cache lines**: round to 64 bytes for byte
  arrays, or leave it to the program.
* **A statement form** (option F) and whether it is worth a second syntax
  before a program asks for it.

## References

Spec and implementation sections referred to above:
[§1.2 memory model](../goose_spec.md#12-memory-model),
[§1.3 the stack invariant](../goose_spec.md#13-the-stack-invariant),
[§6.5 `for`](../goose_spec.md#65-for),
[§7.6 static function values](../goose_spec.md#76-static-function-values),
[§7.8 recursion](../goose_spec.md#78-recursion),
[§7.12 `simd fn`](../goose_spec.md#712-simd-fn-versions-per-instruction-set),
[§9.1 the outlives rule](../goose_spec.md#91-the-outlives-rule),
[§9.2 roots](../goose_spec.md#92-roots-and-the-depth-check),
[§9.5 writability](../goose_spec.md#95-writability),
[§10.3 stack assignment](../goose_spec.md#103-stack-assignment-deterministic-recipe),
[§10.4 runtime environment](../goose_spec.md#104-runtime-environment),
[§11.2 concurrency](../goose_spec.md#112-concurrency);
[implementation.md §6.3](../implementation.md#63-globals-and-program-instances),
[§6.4](../implementation.md#64-the-calling-convention-c3),
[§6.8](../implementation.md#68-threads-and-queues),
[§6.13](../implementation.md#613-simd-functions),
[§7](../implementation.md#7-the-runtime);
[reference §22 threads](../reference.md#22-threads);
[llm_frontier.md §3.4](llm_frontier.md#34-effects-pure-and-frames),
[§4.1](llm_frontier.md#41-distinct-roots-as-restrict),
[§4.3](llm_frontier.md#43-par-for-shared-memory-iteration-proven-race-free),
[§4.4](llm_frontier.md#44-explicit-simd);
[coroutines.md §2.2](coroutines.md#22-the-recursion-rule-is-the-coroutine-rule);
[analysis_inventory.md §2](analysis_inventory.md#2-what-is-tracked-where),
[§5.6](analysis_inventory.md#6-loops-two-iteration-check-instead-of-name-scans-small-effort-independent).
The issue: [goose#234](https://github.com/aardappel/goose/issues/234).
The measurements: `bench/fork_cost/spawn_cost.goose`,
`bench/fork_cost/pool_cost.c`.

[ispc]: https://ispc.github.io/ispc.html "ISPC user's guide: tasking model, foreach, uniform and varying"
[ispc-mandel]: https://raw.githubusercontent.com/ispc/ispc/main/examples/cpu/mandelbrot_tasks/mandelbrot_tasks.ispc "ISPC: Mandelbrot with launch and foreach"
[ispc-tasksys]: https://raw.githubusercontent.com/ispc/ispc/main/examples/common/tasksys.cpp "ISPC: the example task systems"
[ispc-paper]: https://www.pharr.org/matt/assets/ispc.pdf "Pharr and Mark: ispc, a SPMD compiler for high-performance CPU programming (InPar 2012)"
[ispc-perf]: https://ispc.github.io/perf.html "ISPC performance: one core and four cores"
[mojo-matmul]: https://github.com/modular/modular/blob/83284b5b3cfc163bcccd44f1fbca15bcb52d7ec8/examples/mojo/matmul.mojo "Mojo: the matmul example, parallelize over rows with vectorize inside (last revision before its removal, May 2025)"
[mojo-vectorize]: https://mojolang.org/docs/std/algorithm/backend/vectorize/vectorize/ "Mojo: vectorize"
[mojo-runtime]: https://mojolang.org/docs/std/runtime/parallelism_level/ "Mojo: parallelism_level, the runtime's worker threads"
[halide]: https://halide-lang.org/ "Halide: the algorithm and the schedule"
[halide-blur]: https://raw.githubusercontent.com/halide/Halide/main/apps/blur/halide_blur_generator.cpp "Halide: the blur generator's CPU schedule"
[halide-pldi13]: https://people.csail.mit.edu/jrk/halide-pldi13.pdf "Ragan-Kelley et al.: Halide (PLDI 2013), parallel loops lowered to closures and a task queue"
[halide-pool]: https://raw.githubusercontent.com/halide/Halide/main/src/runtime/thread_pool_common.h "Halide runtime: the thread pool"
[omp-parallel]: https://www.openmp.org/spec-html/5.1/openmpse14.html "OpenMP 5.1 §2.6: the parallel construct; the encountering thread is thread 0"
[omp-for-simd]: https://www.openmp.org/spec-html/5.1/openmpsu49.html "OpenMP 5.1 §2.11.5: simd and for simd"
[omp-pfs]: https://www.openmp.org/spec-html/5.1/openmpsu77.html "OpenMP 5.1 §2.16.5: parallel for simd"
[omp-reduction]: https://www.openmp.org/spec-html/5.0/openmpsu107.html "OpenMP 5.0 §2.19.5: reduction clauses, order unspecified"
[omp-wait]: https://gcc.gnu.org/onlinedocs/libgomp/OMP_005fWAIT_005fPOLICY.html "libgomp: OMP_WAIT_POLICY"
[gomp-spin]: https://gcc.gnu.org/onlinedocs/libgomp/GOMP_005fSPINCOUNT.html "libgomp: GOMP_SPINCOUNT"
[libgomp-parallel]: https://gcc.gnu.org/onlinedocs/libgomp/Implementing-PARALLEL-construct.html "libgomp: how PARALLEL is implemented"
[libgomp-team]: https://raw.githubusercontent.com/gcc-mirror/gcc/master/libgomp/team.c "libgomp team.c: thread reuse and the master thread in the team"
[kmp-blocktime]: https://openmp.llvm.org/design/Runtimes.html "LLVM OpenMP runtime: KMP_BLOCKTIME, KMP_DETERMINISTIC_REDUCTION"
[epcc-rerun]: https://github.com/vchuravy/ThreadTeams.jl/issues/46 "EPCC syncbench rerun: parallel for overheads on a Ryzen 5950X and an Apple M1"
[cpp-policy]: https://en.cppreference.com/w/cpp/algorithm/execution_policy_tag_t "C++ execution policies: seq, par, unseq, par_unseq"
[cpp-draft]: https://eel.is/c++draft/algorithms.parallel "C++ standard: vectorization-unsafe functions"
[gcc9]: https://gcc.gnu.org/gcc-9/changes.html "GCC 9: parallel algorithms require TBB"
[msvc-exec]: https://learn.microsoft.com/en-us/cpp/standard-library/execution?view=msvc-170 "MSVC: parallel algorithms on the Windows thread pool"
[tbb-pfor]: https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/algorithms/functions/parallel_for_func "oneTBB: parallel_for"
[tbb-part]: https://uxlfoundation.github.io/oneTBB/main/tbb_userguide/Partitioner_Summary.html "oneTBB: partitioners"
[tbb-grain]: https://www.intel.com/content/www/us/en/docs/onetbb/developer-guide-api-reference/2023-1/controlling-chunking.html "oneTBB: controlling chunking, the 100,000-cycle rule"
[tbb-arena]: https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/task_arena/task_arena_cls "oneTBB: task_arena"
[opencilk-spec]: https://cilk.mit.edu/docs/OpenCilkLanguageExtensionSpecification.htm "OpenCilk language extension specification: cilk_for, grainsize, reducers"
[opencilk-reducers]: https://www.opencilk.org/doc/reference/reducers/ "OpenCilk: reducers"
[cilk5]: https://www.fftw.org/~athena/abstracts/abstract17.html "Frigo, Leiserson, Randall: the implementation of the Cilk-5 multithreaded language (PLDI 1998)"
[gcc8]: https://gcc.gnu.org/gcc-8/changes.html "GCC 8: Cilk Plus removed"
[icc191]: https://www.intel.com/content/www/us/en/developer/articles/release-notes/c-compiler-legacy/19-1-macos-for-psxe2020.html "Intel C++ 19.1: Cilk Plus removed"
[rust-scope]: https://doc.rust-lang.org/std/thread/fn.scope.html "Rust: std::thread::scope"
[rust-sync]: https://doc.rust-lang.org/std/marker/trait.Sync.html "Rust: Sync"
[rust-chunks]: https://doc.rust-lang.org/std/primitive.slice.html#method.chunks_mut "Rust: chunks_mut"
[rust-24292]: https://github.com/rust-lang/rust/issues/24292 "Rust issue 24292: JoinGuard is unsound (the leakpocalypse)"
[rfc-1066]: https://rust-lang.github.io/rfcs/1066-safe-mem-forget.html "Rust RFC 1066: safe mem::forget"
[rfc-3151]: https://rust-lang.github.io/rfcs/3151-scoped-threads.html "Rust RFC 3151: scoped threads"
[rayon-join]: https://docs.rs/rayon/latest/rayon/fn.join.html "rayon: join"
[rayon-chunks]: https://docs.rs/rayon/latest/rayon/slice/trait.ParallelSliceMut.html#method.par_chunks_mut "rayon: par_chunks_mut"
[rayon-foreach]: https://docs.rs/rayon/latest/rayon/iter/trait.ParallelIterator.html#method.for_each "rayon: for_each takes Fn + Sync + Send"
[rayon-faq]: https://github.com/rayon-rs/rayon/blob/main/FAQ.md "rayon FAQ: two closures cannot mutate one slice"
[rayon-sum]: https://docs.rs/rayon/latest/rayon/iter/trait.ParallelIterator.html#method.sum "rayon: sum is not deterministic for floats"
[spice]: https://github.com/judofyr/spice "Spice: measured rayon::join overhead"
[se-0302]: https://github.com/apple/swift-evolution/blob/main/proposals/0302-concurrent-value-and-concurrent-closures.md "Swift SE-0302: Sendable"
[se-0304]: https://github.com/apple/swift-evolution/blob/main/proposals/0304-structured-concurrency.md "Swift SE-0304: structured concurrency; disjoint captured writes declined"
[swift-forum]: https://forums.swift.org/t/concurrent-work-on-array/79276 "Swift forums: concurrent work on an array under Swift 6"
[jep-453]: https://openjdk.org/jeps/453 "JEP 453: structured concurrency"
[kotlin-scope]: https://kotlinlang.org/docs/coroutines-basics.html "Kotlin: a parent waits for its children"
[njs]: https://vorpus.org/blog/notes-on-structured-concurrency-or-go-statement-considered-harmful/ "Smith: notes on structured concurrency (2018)"
[chapel-forall]: https://chapel-lang.org/docs/language/spec/data-parallelism.html "Chapel: forall and dataParTasksPerLocale"
[chapel-forall-guide]: https://chapel-lang.org/docs/users-guide/datapar/forall.html "Chapel users guide: forall is the programmer's assertion"
[chapel-foreach]: https://chapel-lang.org/docs/technotes/foreach.html "Chapel: the foreach loop"
[chapel-both]: https://chapel.discourse.group/t/vectorized-and-parallell-loops-can-i-have-both/13082 "Chapel discourse: forall's per-task loop is a foreach"
[futhark-perf]: https://futhark.readthedocs.io/en/latest/performance.html "Futhark: nested parallelism per backend"
[futhark-msc]: https://futhark-lang.org/student-projects/duc-msc-thesis.pdf "Tran: a multicore backend for Futhark (2020)"
[futhark-ispc]: https://www.futhark-lang.org/student-projects/ispc-bsc-thesis.pdf "Malling et al.: an ISPC backend for Futhark (2022)"
[futhark-0-21-13]: https://futhark-lang.org/blog/2022-07-07-futhark-0.21.13-released.html "Futhark 0.21.13: ISPC backend numbers"
[julia-threads]: https://docs.julialang.org/en/v1/base/multi-threading/ "Julia: Threads.@threads, :static and :dynamic"
[julia-simd]: https://docs.julialang.org/en/v1/manual/performance-tips/ "Julia: @simd"
[julia-reduce]: https://docs.julialang.org/en/v1/base/collections/#Base.reduce-Tuple{Any,%20Any} "Julia: reduce's associativity is implementation dependent"
[pony-caps]: https://tutorial.ponylang.io/reference-capabilities/reference-capabilities.html "Pony: reference capabilities"
[verona]: https://arxiv.org/abs/2309.02983 "Arvidsson et al.: Reference capabilities for flexible memory management (Verona, OOPSLA 2023)"
[dpj]: https://dl.acm.org/doi/10.1145/1640089.1640097 "Bocchino et al.: a type and effect system for Deterministic Parallel Java (OOPSLA 2009)"
[dpj-tutorial]: https://dpj.cs.illinois.edu/DPJ/Download_files/DPJTutorial.html "DPJ tutorial: disjoint array updates"
[parasail-qs]: https://parasail-programming-language.blogspot.com/2010/12/parallel-quicksort-in-parasail-and.html "Taft: parallel quicksort in ParaSail, slices proven disjoint"
[parasail-embedded]: https://www.embedded.com/?p=4375616 "Taft: ParaSail, less is more with multicore"
[gcd]: https://en.wikipedia.org/wiki/GCD_test "The GCD dependence test"
[banerjee]: https://en.wikipedia.org/wiki/Banerjee_test "The Banerjee dependence test"
[omega]: https://dl.acm.org/doi/10.1145/125826.125848 "Pugh: the Omega test (Supercomputing 1991)"
[gcc-ivdep]: https://gcc.gnu.org/onlinedocs/gcc/Loop-Specific-Pragmas.html "GCC: #pragma GCC ivdep"
[llvm-parallel]: https://llvm.org/docs/LangRef.html#llvm-loop-parallel-accesses "LLVM: llvm.loop.parallel_accesses"
[chase-lev]: https://www.dre.vanderbilt.edu/~schmidt/PDF/work-stealing-dequeue.pdf "Chase and Lev: dynamic circular work-stealing deque (SPAA 2005)"
[futex]: https://man7.org/linux/man-pages/man2/futex.2.html "futex(2)"
[waitonaddress]: https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitonaddress "Windows: WaitOnAddress"
