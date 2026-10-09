# Thread spawn benchmarks: design

What does `thread_spawn` cost, and what makes it cost more? This suite
measures the price of starting and finishing a Goose worker (spec 11.2) along
every axis the implementation makes that price depend on, and puts each
number next to the floor the operating system sets and to what an idiomatic
C++ or Rust program pays for the same hand-off.

It is a separate harness from `bench/run_bench.py` on purpose. That suite
times whole processes running one algorithm at three sizes. A spawn is
10-20 µs, below the noise of process creation, and the interesting questions
here are slopes (ns per global, per byte, per region), not one headline time.
So this suite times in-process with `os.clock_ns`, sweeps one parameter at a
time across many generated programs, and fits lines through the results.

## What a spawn does today

Read from `src/codegen_builtins.h` (`EmitThreadSpawn`, `EnsureThreadThunk`),
`src/runtime/runtime.h` and `src/runtime/runtime_threads.h`. Each step is a
cost the suite must be able to isolate.

Spawning side, on the caller's thread:

1. **Pack the image** on a data stack: each argument constructed in place,
   resizable ones as `[size][count][elements]`, then **every global the
   worker's program statically reaches** (`ThreadGlobals`: a walk of the call
   graph from the `thread_fn`), each as `[size][bytes]`. `const` globals with
   compile-time initializers are static data and are not copied. Copy 1.
2. `gs_thread_start`: `malloc` a record, `malloc(argsize)` and `memcpy` the
   whole image. Copy 2.
3. Take the **one process-wide mutex**, push the record on a linked list,
   `pthread_create` (detached) / `CreateThread`, unlock.

Worker side, on the new thread:

4. `gs_regions_begin`: `calloc` the region registry (`GS_MAX_STACKS * 4`
   pointers, 32 KB). `gs_native_stack_init`: `malloc` a signal alternate
   stack and `sigaltstack`; query the native stack bounds.
5. `gs_thread_run`: `calloc` the stack block (`GS_MAX_STACKS` entries, 8 KB).
6. The thunk unpacks: scalar and small fixed arguments by value, large fixed
   (> 4096 bytes) by pointer into the packet, and **each resizable argument
   into a region of its own** (`mmap` + `mprotect` of
   `GS_STACK_RESERVE + GS_STACK_GAP`, 257 MB by default). Copy 3.
7. `calloc(sizeof(gs_globals_t))`: **the struct of every non-static global
   in the program**, used by the worker or not. Then each used global is
   copied in: fixed ones into the struct, and each variable-size or
   resizable one **into a fresh region** (a `reusable` pool takes two, one
   for the elements, one for the freelist). Copy 3 for globals.
8. Run the body. Data stacks the body's call depth reaches are reserved
   lazily, one region each; every page it touches is a first-touch fault.

Finish:

9. `free(gs_gl)`, free the stack block, free the packet, `munmap` every
   region, free the registry and the alternate stack.
10. Lock the global mutex, **walk the active list** to unlink the record,
    `broadcast` the one shared condition variable, unlock.
11. `thread_wait(id)`: lock the same mutex, **walk the active list** looking
    for `id`, sleep on the shared condition variable if found (woken by every
    worker's exit, not just this one's).

From this the predicted cost model is

    spawn ≈ C_os                                   pthread create + exit + join
          + C_rt                                   fixed runtime allocations (4, 5, 9)
          + R · (C_mmap + C_munmap)                R = regions reserved (6, 7, 8)
          + B · c_copy · 3                         B = image bytes (1, 2, 3)
          + P · c_fault                            P = fresh pages first touched
          + G · c_glob                             G = globals copied (per-entry overhead)
          + S_struct · c_zero                      calloc of the whole globals struct
          + A · c_list                             A = active workers at spawn/exit/wait

and every term gets at least one benchmark whose sweep moves only that term.

A two-minute pilot on an Apple M5 (10 cores) already shows the shape:

| probe | ns per spawn + wait |
|---|---|
| raw `pthread_create` + `pthread_join`, C | ~11,000 |
| empty Goose worker | ~13,000 |
| + one 8 KB resizable global (one region) | ~18,000 |
| raw pthread + 1 MB `malloc` + `memcpy`, C | ~26,000 |
| one 1 MB fixed global | ~65,000-78,000 |
| one 1 MB resizable argument | ~163,000 |
| one 1 MB resizable global | ~255,000 |

The fixed overhead over the OS is small (~2 µs). Regions are not (~4.5 µs
each), and megabyte payloads cost 2.5-10x what the same bytes cost through a
plain `malloc` + `memcpy` — first-touch faults on fresh regions plus three
copies. Why a resizable global costs 1.5x a resizable argument of the same
size is exactly the kind of thing this suite exists to explain.

## Axes

Each family below is a sweep: one parameter varies, everything else is at
the baseline (no arguments, no globals, an empty body, serial spawn-then-wait).
IDs are what the harness and the report call them.

### A. Floor and phases

| id | measures | sweep |
|---|---|---|
| A1 `empty` | serial spawn + wait, nothing passed | — |
| A2 `phases` | split of A1: spawn call returns / worker's first instruction / worker's last instruction / `thread_wait` returns | — |
| A3 `burst` | spawn N, then wait all N: throughput, and the spawn-call share alone | N = 1, 4, 16, 64, 256, 1024 |

A2 needs timestamps from inside the worker, which can only leave it through
a queue. The worker `qput`s its entry and exit `clock_ns`; the queue's own
cost is measured separately (G1) and reported beside it, not subtracted
silently.

### B. Globals

Every global type the runtime treats differently, by count and by size.

| id | global shape | storage path | sweep |
|---|---|---|---|
| B1 `glob_scalar_count` | K used `var gK: i64` | struct member, one `[size][8 B]` entry each | K = 0, 1, 4, 16, 64, 256, 1024 |
| B2 `glob_fixed_size` | one used `var g: i64[n]` | struct member, one memcpy | 8 B … 8 MB, ×8 steps |
| B3 `glob_resz_count` | K used `var gK: i64[>..]`, 1 element each | one region each | K = 0, 1, 2, 4, 8, 16, 64 |
| B4 `glob_resz_size` | one used `i64[>..]` | one region, image copy | 0 B … 64 MB, ×8 steps |
| B5 `glob_var_size` | one used variable-size `u8[]` | one region | 8 B … 8 MB |
| B6 `glob_pool` | one used `reusable var` pool, F free entries | two regions + freelist copy | F = 0, 64, 4096 |
| B7 `glob_unused_count` | K `i64` globals the worker never names | struct size only | K = 0, 64, 1024, 16384 |
| B8 `glob_unused_size` | one fixed `i64[n]` the worker never names | `calloc` of struct only | 8 B … 8 MB |
| B9 `glob_unused_resz` | K resizable globals the worker never names, holding 1 MB each | none expected | K = 0, 1, 16 |
| B10 `glob_const` | K `const` globals with compile-time initializers, used | static data, never copied | K = 0, 16, 1024 (control: flat) |
| B11 `glob_cold` | 1 MB fixed global named only in a branch the worker never takes | copied: reachability is static | on/off |

B7-B9 separate "program has globals" from "worker uses globals". B8 should
show `calloc` of a large struct is cheap until it is not (large callocs come
from fresh `mmap`ed zero pages; the copy then faults them in, but an unused
member is never touched). B10 is a control that must be flat. B11 documents
that what counts is the static call graph, so a rarely called logging helper
that names a big global makes every spawn pay for it.

### C. Arguments

| id | argument shape | path | sweep |
|---|---|---|---|
| C1 `arg_scalar_count` | K `i64` arguments | by value from the packet | K = 0, 1, 2, 4, 8, 16, 32, 64 |
| C2 `arg_fixed_size` | one `i64[n]` | by value ≤ 4096 B, by pointer into the packet above | 8 B … 8 MB, with points at 4096 and 4104 |
| C3 `arg_resz_count` | K `i64[>..]` with 1 element | one region each | K = 0, 1, 2, 4, 8, 16 |
| C4 `arg_resz_size` | one `i64[>..]` | one region, image copy | 0 B … 64 MB |
| C5 `arg_var_size` | one variable-size `u8[]` | read in place from the packet | 8 B … 8 MB |
| C6 `arg_strings` | one `u8[][>..]` of W short strings, same total bytes as a C4 point | nested-array image | W = 1 … 1M at fixed 1 MB, and at fixed W |
| C7 `arg_frame_obj` | a struct with a fixed prefix and a resizable tail | frame-object prefix path | tail 0 B … 8 MB |

C2 brackets the 4096-byte large-fixed threshold on both sides. C4 against C5
is the region-or-not comparison for the same bytes; C4 against B4 is
argument-or-global for the same bytes, where the pilot already disagrees.
C6 checks that "flat is cheap to copy" holds for a nested value whose
C++/Rust equivalent is W allocations.

### D. What the worker does with its own storage

| id | measures | sweep |
|---|---|---|
| D1 `worker_stacks` | data stacks the body's call depth reserves lazily | 0, 1, 4, 16, 64 regions |
| D2 `worker_touch` | bytes the body pushes onto one resizable local (commit + teardown) | 0, 64 KB, 1 MB, 16 MB |
| D3 `worker_native` | native stack the body touches (recursion depth) | 0, 64 KB, 256 KB |

D2 is in a spawn suite because `munmap` of committed pages is paid at the
worker's exit, inside the spawn+wait time a caller sees, and is part of why
large payloads cost what they do.

### E. Topology and contention

| id | measures | sweep |
|---|---|---|
| E1f / E1r `wait_order` | burst of N, waited oldest first / newest first | N = 16, 256, 1024 |
| E2 `live_workers` | serial spawn + wait while L idle workers block in `qget` | L = 0, 16, 256, 1024 |
| E3 `parallel_spawners` | P workers each spawn + wait M children concurrently | P = 1, 2, 4, 8, hardware_threads |
| E4 `tree` | binary fork-join tree of depth d, every node spawns two and waits | d = 4 … 12 |
| E5 `chain` | worker spawns worker spawns … depth d, each carrying a 1 KB global | d = 1 … 256 |
| E6s / E6p / E6i `spawn_vs_pool` | tasks of k xorshift rounds: a worker spawned per task (waves of 256), a pool of `hardware_threads` workers fed by `qput`/`qget`, and the work alone on main's thread | k = 0, 100, 1000, 10^4, 10^5 |

E1 and E2 target the linked list: `thread_wait` and worker exit both walk
every active worker under the global mutex, and every exit broadcasts to
every waiter, so these should be the superlinear rows if any are. The list
is newest first, so E1f (waiting for the oldest, at the tail) is the walk's
worst case and E1r its best. In E2 the worker being waited for is always the
newest, at the head, so E2 isolates what many live threads cost the kernel
and scheduler rather than the list. E3 and E4
hit the single mutex and the kernel's address-space lock (every region is an
`mmap`). E6 is the practical question a user has: at what task size does
spawning per task stop mattering? A Goose task has to return its result
through a queue; the C, C++ and Rust tasks add it to an atomic, which is
cheaper, and the rows say so.

### F. Build-time configuration

| id | measures | sweep |
|---|---|---|
| F1 `reserve` | `GS_STACK_RESERVE` per region, on A1 and B3 at K = 8 | 16 MB, 256 MB (default), 2 GB, 64 GB |

Reservation size should not matter on Linux and might on macOS or Windows.
If it does, that is a cheap knob for programs that spawn a lot.

### G. Reference costs

| id | measures |
|---|---|
| G1 `queue` | one `qput` + `qget` round trip of an `i64` between two threads, for A2 and E6 |
| G2 `mmap` | one region reserve + release in C, through the Goose runtime functions directly |

### Baselines

Each row of A1, A3, C1, C4, C6, E2, E3 and E6 also runs in:

* **C, raw pthread** (`CreateThread` on Windows): the OS floor. For payload
  rows, `malloc` + `memcpy` once into the thread, which is the minimum a
  no-shared-memory hand-off can do.
* **C++, `std::thread`**: a lambda capturing the payload by value
  (`std::vector<int64_t>`, `std::vector<std::string>` for C6), joined.
* **Rust, `std::thread::spawn`**: a `move` closure owning a cloned `Vec`
  (`Vec<String>` for C6), joined. Plus `std::thread::scope` for E4.

Goose globals have no direct counterpart: a C++ or Rust thread shares
globals instead of copying them. The closest honest comparison for B rows is
a C++/Rust worker given a copy of a struct of the same size, and those rows
are labelled as such, not as "the same thing".

## Method

**One program per point.** Global counts, global sizes and argument shapes
are compile-time facts, so a Python generator writes one Goose source per
(benchmark, parameter) and builds it with `goose -O2 --standalone` and the
C toolchain at `-O2`, as `run_bench.py` does. About 150 programs in all;
building is parallel and cached by source hash.

**In-process timing.** Each program runs its loop of spawn + wait under
`os.clock_ns`, after a warm-up of up to 50 spawns or a quarter of a batch
(first `mmap`s, page cache, the allocator's thread caches). The loop count
adapts: double until one batch takes at least 100 ms, then run 15 batches.
Reported per spawn: median and p10/p90 over batches, and min. Each batch
prints one line, which the harness parses. Every run's result feeds a
printed sink, or a side-effect-free run (E6i) would be deleted from the
calibration loop. `CLOCK_MONOTONIC` is µs-granular on macOS; at 100 ms
batches that is irrelevant, and A2's per-phase sums over thousands of
spawns average the quantization out.

**Peak memory.** The harness records the process peak working set
(`tc.run_measured`) for each point, and for the burst rows reports it per
live worker. Address space reserved by regions is not counted (bench
design, 10.4).

**Correctness.** Every program is also built with `CHECK = true`, where the
worker `qput`s a checksum of every argument and global it received and main
compares it with the same fold computed by a plain function, `expect`, given
the same arguments and reading main's globals. A point whose check fails is
reported, not timed: a spawn that copies the wrong bytes is not fast.

**Keeping the work real.** The worker must name what it is given, or
`ThreadGlobals` drops it. Each worker reads the first and last element of
each argument and global and folds them into a value compared against a
sentinel no checksum equals; on a match it would `qput`. The C compiler
cannot prove the branch dead, so the reads stay; they cost a few loads. The
copies themselves are runtime code the compiler cannot remove either way.

**Fitting.** For each count or size sweep the report fits ns = a + b·x by
least squares on the medians and prints b with its unit (ns per global, ns
per region, ns per KB) and R². A poor R² is itself a result: it means the
cost is not linear in that axis (E1, B2 across the page-fault boundary).
Those rows get a log-log table instead.

**Noise.** Machines are not quiet. The harness records CPU, core count, OS
and compiler, refuses to run if load average is above 1 per core unless
`--force`, and re-measures any point whose p90/p10 exceeds 1.5 once.
Linux runs may pin with `taskset`; macOS cannot pin and the report says so.

## Output

```
bench/spawn/
  design.md         this file
  spawn_bench.py    generator, builder, runner, report
  baselines/        spawn.c, spawn.cpp, spawn.rs: one program, a mode argument per row
  results.md        written by the harness: tables, fitted slopes, notes
  results.json      raw measurements, for --report-only re-renders
bench/gen/spawn/    generated sources and binaries (git-ignored)
```

```
python bench/spawn/spawn_bench.py                     # everything
python bench/spawn/spawn_bench.py --only B,C4         # families or single benchmarks
python bench/spawn/spawn_bench.py --quick             # 3 points per sweep, 5 batches
python bench/spawn/spawn_bench.py --no-baselines      # Goose only
python bench/spawn/spawn_bench.py --no-check          # skip the CHECK builds
python bench/spawn/spawn_bench.py --report-only       # re-render from results.json
python bench/spawn/spawn_bench.py --list              # the plan: 37 benchmarks, 183 points
```

A `--only` run merges into `results.json`, so a subset can be re-measured
without losing the rest.

`results.md` leads with the cost model above filled in with measured
coefficients, then one table per family, then the baselines side by side.

## Expected findings to confirm or refute

Written down before running so the report can say which held.

1. The fixed overhead over the OS is a few µs, mostly the three `calloc`s
   and the signal stack (A1).
2. Each region costs a constant ~4-5 µs on macOS, smaller on Linux; this is
   the largest per-item cost (B3, C3, D1). Pre-reserving or caching regions
   across workers would be the obvious fix.
3. Per-byte cost is ~3x `memcpy` plus first-touch faults, so large payloads
   are 3-10x a raw `malloc` + `memcpy` hand-off (B2, B4, C4).
4. Resizable globals cost more than resizable arguments of the same size
   (B4 vs C4). Unexplained yet.
5. Unused globals cost little until the struct is large enough to change
   `calloc`'s path (B7, B8), and unused resizable globals cost nothing (B9).
6. Waiting is O(active) under one mutex with a broadcast per exit, so E1 and
   E2 grow superlinearly once hundreds of workers are live, and E3 stops
   scaling early.
7. Goose ≈ C++ ≈ Rust for empty and scalar-argument spawns; Goose wins on
   C6 (one flat image against W allocations) and loses on C4 at large sizes.

Each of 2, 3, 4 and 6 points at a runtime change; this suite is what would
show whether that change worked.

## What the first run found (Apple M5, macOS, clang 21, 2026-10-09)

The full numbers are in [results.md](results.md). Here is how each prediction above held up:

1. **Held.** The fixed overhead is small: an empty spawn + wait takes 12.3 µs,
   against 10.9 µs for raw pthread, 10.6 µs for C++ and 11.8 µs for Rust.
   Scalar arguments (C1), scalar globals (B1), const globals (B10) and
   unused globals (B7, B9) cost nothing measurable.
2. **Held, and worse than predicted.** Each data-stack region costs 9-12 µs
   (B3, C3, D1), about one empty spawn per region, and the cost grows with
   the reservation: 8 regions take 55 µs over A1 at a 16 MB reserve and
   880 µs at 64 GB (F1b). The raw `mmap` + `mprotect` + `munmap` floor is
   5.7-16 µs (G2). This is the biggest cost in the runtime that can be fixed.
3. **Held for resizables, partly for the rest.** Resizable payloads cost
   about 94 ns/KB (B4, C4, C7), roughly 6x the 15 ns/KB of a `malloc` +
   `memcpy` hand-off in any of the three baselines. Fixed and variable-size
   arguments, which skip the region, cost about 39 ns/KB (C2, C5), roughly
   2.5x the baseline. A used fixed global costs 64 ns/KB (B2).
4. **Partly held.** A resizable global costs more than a resizable argument
   of the same size at 1 MB (190 µs against 103 µs) and at 8 MB (897 µs
   against 797 µs). The two are equal at 64 MB.
5. **Held.** An unused global is free until the globals struct gets large:
   an unused 8 MB fixed global adds 53 µs to every spawn through the
   `calloc` (B8). A 1 MB global named only on a dead branch adds 54 µs
   (B11).
6. **Refuted.** The active list and the broadcast do not show up. E1f, E1r
   and E2 stay flat up to 1024 live workers. With 1024 idle threads, Goose
   holds at 12 µs while C, C++ and Rust slow to 30 µs. Concurrency is where
   Goose falls behind instead: P concurrent spawners reach 19 µs per spawn
   at P = 10, against 6 µs for the baselines (E3), and the fork-join tree
   takes 20-39 µs per node against 6-15 µs (E4). That points at the global
   mutex and the kernel's address-space lock, which every region `mmap`
   takes.
7. **Mostly refuted.** Scalar spawns are at parity. C6 does not show the
   expected win: a list of strings is a resizable, so it pays the region and
   the 94 ns/KB path. Goose is ahead only at 16384 strings (120 µs against
   282 µs). It is 3-4x behind at 1-1024 strings.

Other findings:

* E6p: a pool hand-off costs about 300 ns per task in Goose against 30 ns
  for a mutex + deque. Each `qput` mallocs a node and signals a condition
  variable. From about 1 µs of work per task the pools are level.
* E6s: spawning one worker per task stays within 1.5x of the baselines.
* A2: of the 12.4 µs, 4.8 µs is spent before `thread_spawn` returns, and
  the worker starts 7.9 µs after the call began.

Runtime changes these results suggest, in order of payoff:

* Cache released regions per process, or reserve smaller regions for
  workers (items 2 and 6).
* Pass resizable arguments and globals without the intermediate copies
  (item 3).
* Use a lock-free or per-type node pool for queues (E6p).
