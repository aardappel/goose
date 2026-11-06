# Reusable thread pools and `pool.apply(chunks(...), fn, args...)`

Status: implemented on `feat/parallel-scope`.

<details>
<summary>Table of contents</summary>

- [1. Motivation](#1-motivation)
- [2. Prior art and what is new in Goose](#2-prior-art-and-what-is-new-in-goose)
- [3. Syntax and constructs](#3-syntax-and-constructs)
- [4. Compile-time safety and data-race freedom](#4-compile-time-safety-and-data-race-freedom)
- [5. Runtime and Data Stack reuse](#5-runtime-and-data-stack-reuse)
- [6. Worked examples (including parallel Hash Join)](#6-worked-examples-including-parallel-hash-join)

</details>

## 1. Motivation

Goose's existing concurrency model (§11.2, `thread_fn` + `thread_spawn` / `thread_wait` + typed queues `qput` / `qget`) is designed for **unstructured, isolated actor threads**:
- A `thread_fn` spawned with `thread_spawn` may outlive the scope that spawned it and runs concurrently with the parent thread.
- Because its lifetime is unbounded relative to the caller's scopes, a `thread_fn` may only accept **flat, by-value copies** of its arguments and globals (`memcpy`), cannot be called as an ordinary function, and can only return results by copying flat byte images into a process-wide `qput<T>` queue.
- Each `thread_spawn` creates a fresh OS thread (`pthread_create` / `_beginthreadex`), reserves a fresh block of 2 GB virtual-memory Data Stacks (`mmap` / `VirtualAlloc`), and tears both down (`munmap` / `VirtualFree`) when the worker exits.

For structured data-parallel workloads (such as image rendering, matrix/tensor kernels, simulation steps, or database joins and filters across consecutive passes), unstructured `thread_spawn` has three costs:
1. **Thread & `mmap` churn**: Spinning up OS threads and mapping/unmapping multi-gigabyte Data Stack regions on every parallel phase costs puluhan of microseconds per phase.
2. **Input copying**: Large lookup tables, hash tables, or scene graphs cannot be read in place by workers; they must be serialized into each worker's spawn packet.
3. **Output copying**: Workers cannot write directly into disjoint slices of an output array; they must build local arrays, `qput` them into a queue, and have the main thread `qget` and copy them into the destination array (as in `samples/23_mandelbrot_threads.goose`). Where the size of a worker's output is known only once it runs, as a filter's is, there is not even a slice to cut out for it ahead of time.

**`thread_pool` + `pool.apply(chunks(...), fn, args...)`** solves all three while leaving `thread_fn` 100% untouched for isolated actor threads.

---

## 2. Prior art and what is new in Goose

### Prior art in scope-bounded parallelism

| System / Language | Construct | How it handles memory & safety |
|---|---|---|
| **Cilk** (1994) / **OpenMP** (1997) | `cilk_for` / `#pragma omp parallel for` | Tasks can point into the parent's C stack frame because the parallel loop joins before continuing. No static check against data races. |
| **Rust** (`std::thread::scope` + Rayon `par_chunks_mut`) | `slice.par_chunks_mut(k).for_each(...)` | Statically proves tasks cannot outlive the call and uses `&T` (shared read-only) + `chunks_mut` (disjoint `&mut [T]`) for data-race freedom. |

### What is genuinely new in Goose's design

1. **One-line synchronous barrier (`pool.apply(chunks(...), fn, args...)`) with zero lifetime annotations**:
   Because `pool.apply` is a single synchronous call that dispatches all chunks of the target array and waits for them to complete before returning:
   - The calling thread is blocked inside `pool.apply`, so it **cannot** mutate or shrink the target array or any shared `const` arguments while workers are running.
   - Every variable in the caller's scope automatically outlives all worker tasks.
   - The compiler only needs to check one aliasing invariant at the call site: **no shared `const` argument (or `offsets` slice) may share a root with the partitioned output array**.
2. **Ordinary `fn`s (leaving `thread_fn` untouched)**:
   Workers dispatched by `pool.apply` are regular top-level `fn`s—not `thread_fn`s. The exact same `fn` can be called directly on a single thread (for debugging, tests, or small inputs) or dispatched across worker threads via `pool.apply`.
3. **Both uniform (`chunks(arr, size)`) and prefix-sum (`chunks(arr, offsets)`) disjoint partitioning**:
   Supporting a monotonically increasing `offsets: const i64[:]` slice alongside a uniform `size: i64` allows variable-output parallel algorithms (such as a two-pass database **Hash Join** or parallel `filter`) to write directly into exact disjoint slices `arr[offsets[idx] .. offsets[idx + 1]]` of a single contiguous output array.
   Where counting first would mean reading and filtering the input twice, a worker that takes a `T[>..]&` of its own grows its output on its own worker's Data Stacks instead, and the apply appends every task's output to one array, in task order (`chunks(arr, num_tasks)`, §3.4).
4. **Pre-reserved virtual-memory Data Stacks per pool worker with $O(1)$ rewind**:
   Each worker in a `thread_pool` maps its 2 GB virtual-memory Data Stacks **once**, the applying thread's in a registry the pool keeps for it (§5). When a worker's `fn` allocates temporary growable arrays (`u8[>..]`, `i64[>..]`) and returns, the worker resets `gs_nregions = 0` so the next chunk reuses the exact same `mmap` base pointers (`gs_regions[i].base`) in $O(1)$—**zero heap allocations and zero `mmap`/`munmap` syscalls across consecutive `pool.apply` calls**.

---

## 3. Syntax and constructs

### 3.1 Declaring a reusable thread pool: `thread_pool`

```goose
var pool = thread_pool(4);                  // explicit worker count
var default_pool = thread_pool();           // defaults to hardware_threads()
```

- `thread_pool` is a built-in resource type (`TY_POOL`) representing a pool of workers, each owning a reusable block of Data Stacks. A pool of `n` workers is `n - 1` persistent OS threads of its own and the thread that applies it, which runs chunks while it waits for them (§5): `thread_pool(1)` starts no thread, and `thread_pool()` one fewer than `hardware_threads()`.
- A `thread_pool` variable may be declared as a local or a global, and may be borrowed by reference (`p: thread_pool&`) to pass into helper functions.
- A `thread_pool` cannot be copied by value, reassigned, serialized, or sent through a queue. That covers `copy(pool)`, an un-annotated binding of a reference to a pool (one of several results too, unless it binds by reference with `.=`), an `if`, `match` or block whose value is a pool variable, a function value whose block names one, and an array of pools. No function returns a pool by value, declared or inferred. So each pool is in one variable, which `close` leaves closed, and no second handle can outlive the close.
- The program ends a pool itself, with `pool.close()` (or `close(pool)`), usually under `defer` right after making it:

  ```goose
  var pool = thread_pool(4);
  defer pool.close();
  ```

  `close` wakes the pool's worker threads, joins them, and releases their Data Stack regions. The variable then holds a closed pool: closing it again does nothing, and `pool.apply(...)` on it aborts with `cannot apply on a closed thread_pool`, as does an apply whose own arguments close the pool before it hands it a chunk. `close` takes the pool's variable (`let` or `var`, local or global) or a reference to it through which it can be written; a `const thread_pool&` applies a pool but cannot close it.
- Nothing ends a pool at its scope's exit, since no code runs at a scope exit that the programmer did not write ([ownership.md](ownership.md)). A pool the program leaves open lives until the process exits, which takes its threads and their mappings with it at once. That is cheaper than destroying the pool: all its workers unmap their Data Stacks at the same moment, which the kernel serializes (1 to 2 ms per worker that touched about 14 MB, measured on 16 threads on aarch64 Linux). A library's `goose_exit` destroys the pools its program leaves open, so that a host can start the program again.

### 3.2 Applying a function across disjoint mutable chunks: `pool.apply`

```goose
// Uniform chunk size:
pool.apply(chunks(data, chunk_size), worker_fn, extra_args...);

// Variable chunk sizes from a prefix-sum offsets array/slice:
pool.apply(chunks(data, offsets), worker_fn, extra_args...);
```

- `data` must be a writable indexable array (`T[k]`, `T[..k]`, `T[..]`, `T[>..]`, `T[>..<]`) or writable slice (`T[:]`) whose element type `T` is fixed-size (`SC_FIXED`) and `flat` (`IsFlat(T)`), with a single known exact root.
- The second argument to `chunks(data, ...)` is either:
  - **`chunk_size: i64`**: splits `data` into $\lceil \text{data.len} / \text{chunk\_size} \rceil$ contiguous slices of up to `chunk_size` elements (aborting if `chunk_size <= 0`).
  - **`offsets: const i64[:]`**: splits `data` into `offsets.len - 1` contiguous slices `data[offsets[idx] .. offsets[idx + 1]]`, verifying at runtime that `0 <= offsets[0] <= offsets[1] <= ... <= data.len` (aborting with `GS_E_CHUNKBOUNDS` before spawning if any pair is out of bounds or not monotonically non-decreasing).
- `worker_fn` is a regular top-level `fn` with no return value whose signature is either:
  - `fn worker_fn(chunk: T[:], extra_0, ..., extra_{K-1})` (without chunk index), or
  - `fn worker_fn(chunk: T[:], idx: i64, extra_0, ..., extra_{K-1})` (with 0-based chunk index `idx: i64`).
- A `worker_fn` whose first parameter is a `T[>..]&` rather than a `T[:]` grows an output of its own instead of writing a chunk, and its `chunks(data, num_tasks)` takes a task count (§3.4).
- Each extra parameter `extra_j` of `worker_fn` may be:
  1. **A flat by-value type (`IsFlat(U)`)**: scalars, flat structs, flat arrays, etc.
  2. **A read-only shared slice or reference (`const U[:]` or `const U&`)** where `U` is fixed-size and `flat` (`IsFlat(U)`), passed by pointer/slice header with **zero copying**.

### 3.3 Containing a task's abort: `pool.apply_contained`

```goose
var why: u8[>..] = [];
if !pool.apply_contained(chunks(rows, 1024), why, check_rows, limits) {
    log(why);                           // "index 7 out of bounds (length 3) (rows.goose:41)"
    why.clear();
}
let ok = pool.apply_contained(chunks(rows, 1024), check_rows, limits);   // no message
```

- `pool.apply_contained` takes what `pool.apply` takes, and optionally a message buffer between the `chunks(...)` and the worker's name: a growable `u8` array (`u8[>..]`, `u8[>..<]`, `u8[..k]`) or a writable reference to one, as `thread_wait(id, msg)` takes (§11.2), which is not the array `chunks()` partitions. An argument there that names a top-level `fn` is the worker. The buffer is appended to once the later arguments have run, as a member operation writes its receiver, so none of them may shrink an array the buffer is in (§5.2), but for a limited one, whose slots stay storage (§5.3): the message goes where the buffer was.
- Its tasks are contained, as a worker `thread_spawn_contained` starts is (§11.2): a failed check or `abort(msg)` in one ends that task alone, which releases what a return would, and its message goes to the apply rather than to stderr. Once a task has aborted, the apply's tasks that no worker has started are skipped, and those running finish. The apply returns once every task has finished or been skipped: `true` if every task returned, `false` if one aborted. The first abort's message, what it would print after `goose runtime error: ` including its location, is appended to the buffer; a later abort's is dropped, and an apply whose tasks all return appends nothing. Each apply keeps to its own tasks' aborts, also where one of its arguments applies the same pool again between its tasks.
- The pool runs on as before: the next apply on it, plain or contained, has the same workers, and the thread applying it gets its own storage back after an abort in a task it ran, as after a return.
- Only the tasks are contained. The apply's own checks (a chunk size of zero or less, offsets out of bounds, a closed pool) abort where they are, as any check of the applying thread's does, and a task's own plain `pool.apply` is not contained either. Guard-page hits, native stack exhaustion and `exit(code)` in a task still end the program. While a thread program has tasks out, on any pool it applies, it is not contained (§5): an abort of its own then ends the program, in a contained task or in a worker `thread_spawn_contained` started too.

### 3.4 Output whose size only the task knows: `chunks(out, num_tasks)`

```goose
fn keep_rows(out: Row[>..]&, idx: i64, rows: const Row[:], step: i64) {
    let lo = idx * step;
    let hi = min(lo + step, rows.len);
    for i in lo..hi {
        if rows[i].keep { out.push(rows[i]); }    // onto the task's own array
    }
}

var kept: Row[>..] = [];
pool.apply(chunks(kept, 16), keep_rows, rows, step);   // 16 tasks, each one's rows appended
```

- A `worker_fn` whose first parameter is a writable reference to a grow-only array, `out: T[>..]&`, rather than a chunk `T[:]`, grows output whose size only its run knows: a filter keeps a share of its morsel's rows that depends on the data, and a worker draining a queue of morsels finds out which ones it takes only as it takes them. Its `chunks(out, num_tasks)` takes the number of tasks to hand out, an `i64` of one or more; zero or less aborts with `chunk size of zero or less` (`GS_E_CHUNKSIZE`), as a chunk size does, and an offsets slice there is a compile-time error.
- `out` is a writable grow-only or grow-shrink array (`T[>..]`, `T[>..<]`), or a writable reference to one, whose element type is fixed-size and `flat`, with a single known root, as `data` is for a chunk; an element type that holds an index type is rejected, as it is through a queue (§3.11).
- Each task `idx` (`0 .. num_tasks - 1`) runs `worker_fn` with an empty `T[>..]` of its own, on a Data Stack of the worker that runs it, which it can `push`, `append` and `resize` into, or shrink, as any other. No task touches `out`. Once every task has returned, the apply appends to `out` the elements each task left in its own array, in task order, `0` first, whatever order the tasks ran in, after what `out` held. That growth is the apply's, once its later arguments have run, as a member operation grows its receiver (§1.3(4)): it may not land inside a value being built in `out` meanwhile, directly or through a callee that applies, as a push may not.
- `pool.apply_contained` appends the tasks' output only if every task returned. Where one aborted, `out` is left as it was, and the first abort's message goes to the message buffer as before.
- `worker_fn` is an ordinary function still: `keep_rows(kept, 0, rows, step)` pushes onto `kept` itself.

---

## 4. Compile-time safety and data-race freedom

The typechecker (`src/typecheck_calls.h`, `src/typecheck_builtins.h`) enforces four invariants on `pool.apply(chunks(data, spec), worker_fn, extra_args...)`:

1. **Disjointness of mutable chunks**:
   - For `chunks(data, size)`, `off` steps by `size > 0`, so chunks `[0..size)`, `[size..2*size)`, ... are disjoint by construction.
   - For `chunks(data, offsets)`, the inductive runtime check `0 <= offsets[idx] <= offsets[idx + 1] <= data.len` guarantees all slices `[offsets[idx] .. offsets[idx + 1])` are disjoint and in bounds.
2. **No aliasing between `data` and shared `const` arguments (or `offsets`)**:
   - The compiler checks that `data` has a single exact root `data_root`, and rejects `pool.apply` at compile time if `offsets` or any reference/slice extra argument `av` can alias `data_root` (`av.Has(data_root)`).
3. **Deep immutability of shared `const` arguments**:
   - Extra reference/slice parameters of `worker_fn` must be `const U&` or `const U[:]` with `IsFlat(U)` (`ra.writable = false`). Any attempt inside `worker_fn` (or its callees) to mutate through a `const` parameter is rejected at compile time.
4. **No reference escape**:
   - Because `T` (the chunk element type) and `U` (any shared `const` pointee type) are `flat`, a worker cannot store a reference to worker-local Data Stack memory into `chunk`, and `EnsurePoolSpec` assigns parameter root classes local depth `pdepth >= 1` so parameter references cannot be stored into globals (`depth == 0`).

A growable output (§3.4) is the task's own: each task starts with an empty array on its worker's Data Stacks, which nothing else names, and nothing writes the caller's `out` until every task has returned and the apply appends what they grew. 2 and 4 hold for it as for a chunk, `out` standing for `data`. The apply grows `out` once its arguments have run, which the checker logs as a growth of `out` at the apply (`NoteGrow`), so that it is checked against what is being built in `out` meanwhile, in the apply's statement and in its callers', as a push's growth is.

---

## 5. Runtime and Data Stack reuse

In `src/runtime/runtime.h`, `src/runtime/runtime_impl.h`, and `src/runtime/runtime_threads.h`:

- **`gs_pool_create(nthreads, file, line)`**:
  A pool of `nthreads` workers is `nthreads - 1` persistent OS threads of its own (`pthread_create` on POSIX, `_beginthreadex` on Windows) and the thread that applies it, which runs tasks in `gs_pool_wait`, where it is blocked anyway. `thread_pool(1)` has no thread of its own, and every chunk runs on the applying thread. A pool with as many workers as cores so starts no thread more than there are cores: with a thread of its own for every worker, the applying thread still held a core as it woke the last one, which then waited behind another thread until the kernel balanced it at its next scheduler tick (3 to 4 ms at 250 Hz, measured with 16 workers on 16 aarch64 Linux cores). Each worker thread initializes its fault/region tracking once (`gs_regions_begin(gs_workerregions)`, `gs_native_stack_init()`). The applying thread's worker gets an id of its own (`caller_thread_id`) and a registry the pool keeps for it (`caller_regions`, `gs_workerregions` entries).
- **Idempotent Data Stack reuse (`gs_regions_mapped`)**:
  Each worker thread keeps `gs_regions_mapped` (the high-water mark of mapped 2 GB Data Stack regions in `gs_regions`). Before each task, `gs_pool_worker_main` resets `gs_nregions = 0`, so every `gs_reserve_region` call inside the task's thunk returns the already-mapped `gs_regions[i].base` in $O(1)$ without calling `mmap` or `munmap`. The applying thread cannot rewind its own registry, whose regions hold its program's live storage, so `gs_pool_run_caller` runs each of its tasks on the pool's `caller_regions` in its place: it saves the thread's `gs_regions`, `gs_nregions`, `gs_regions_mapped`, `gs_regions_cap` and `gs_current_thread_id`, installs the pool's registry rewound to zero and the worker's id, and restores the thread's own after the task, keeping the high-water mark in `caller_regions_mapped`. `gs_pool_run` (`runtime.h`) does the same for the thread's stack block and globals instance (`gs_stks`, `gs_nstks`, `gs_gl`): the task starts without them, as on a thread of its own, opens and releases its own, and the thread gets its own back.
- **`gs_pool_submit` and `gs_pool_wait`**:
  `EmitPoolApply` dispatches all chunks via `gs_pool_submit` and calls `gs_pool_wait(pool)` before returning. `gs_pool_wait` takes the tasks no pool thread has taken yet off the queue and runs them, then waits for those still running on the pool's threads. For `pool.apply_contained` it hands the chunks out with `gs_pool_spawn_contained(pool, &batch, ...)` and waits with `gs_pool_wait_contained(pool, batch, &msg, &len)`, whose result is the apply's, then appends `msg` to the message buffer and frees it, as `thread_wait(id, msg)` does (`AppendMessage`). A limited buffer too small for the message frees it as its capacity check aborts, which a contained thread program returns from, past the free after the append; the buffer's address is taken where it is resolved among the arguments, before any task is out, so that the checks on the way to it, such as an index into an array of buffers, are made once, there (`PinMessageBuffer`).
- **A plain apply's tasks are not contained**: an abort in a task of `pool.apply` ends the program, on whichever worker it runs, as an abort in a worker that a contained worker spawns does (§11.2). `gs_pool_run_caller` clears `gs_contain` for the task, so that a contained worker applying a pool does not catch its task's abort, which would leave the pool's registry installed and the pool's threads still writing into the worker's storage as it was released.
- **`pool.apply_contained`'s tasks are**: `gs_pool_spawn_contained` marks each task contained, and the task's `gs_pool_run` runs its entry through `gs_pool_guard`, whose `gs_setjmp` (`gs_run_contained`, which contained workers use too) an abort in the task returns to. The guard is the innermost frame around the task, below `gs_pool_run` and `gs_pool_run_caller`, so the abort unwinds the task's frames alone: `gs_pool_run` releases the task's stack block and globals and gives the thread its own back, and `gs_pool_run_caller` restores the applying thread's registry, worker id and `gs_contain`, as after a return. The task's regions stay mapped for the next task, as a return leaves them. Each apply has a batch of its own (`gs_pool_batch`), which its first task makes and its wait frees, and each of its tasks points at it: the batch keeps the first abort's message (`aborted`, `msg`, `len`, under the pool's lock) and frees later ones, and a worker that takes a contained task once its batch has aborted frees it unrun. An argument of an apply may apply the same pool again between the apply's tasks, and the inner apply's wait runs the outer one's tasks too, each recording into its own batch, so the inner apply's result and message are its own tasks'. `gs_pool_wait_contained` waits as `gs_pool_wait` does, then hands the batch's message over and frees the batch. The pool lists the batches no wait has freed, and `gs_pool_destroy` frees those left: one whose pool an argument of its apply closed, or whose wait a long-distance return out of such an argument left out.
- **Growable outputs (`gs_pool_outs_new`, `gs_pool_keep`, `gs_pool_outs_free`)**:
  For a worker that takes a `T[>..]&` of its own (§3.4, `Call::pool_growable`), `EmitPoolApply` resolves `out` where the chunks are, as a member operation's receiver (`RecvLoc`), and once the arguments evaluated ahead of the tasks have run, asks the pool for the apply's outputs: a slot for each task (`gs_pool_out { data, len, bytes }`), all empty, which the pool lists. Each task's packet holds the address of its slot where a chunk would be. Its thunk reserves a Data Stack of its own on the worker that runs it (`gs_stack_init`, one more region the thunk holds), hands `worker_fn` an empty header on it, and once `worker_fn` returns copies the elements it left there into the slot (`gs_pool_keep`, a `malloc`'d copy), before the worker rewinds its regions for its next task. A contained task that aborts never gets there and leaves its slot empty, as does one skipped. After `gs_pool_wait`, or once `gs_pool_wait_contained` has returned true, the apply appends the slots' elements to `out` in task order, at its stack top, as an append does, and frees them with the slots (`gs_pool_outs_free`), ahead of any message it appends. The outputs of an apply that never gets to free them, one of whose arguments closed the pool or a long-distance return out of one left, go with the pool: `gs_pool_destroy` frees those it lists once the tasks it has yet to run have kept theirs.
- **A contained thread program is not contained while it has tasks out**: from the first task of a batch (`gs_pool_submit`) to the end of the `gs_pool_wait` for it, the batch is counted in the contained thread program that handed it out (`gs_contained::handing`, which the pool's `handed_by` names), and `gs_fail` contains an abort only where that count is zero. The count is the thread program's, not the pool's, so that batches on several pools, handed out and waited for in any order, as where an argument of one apply applies another pool, keep it uncontained until the last of them ends. An abort of its own meanwhile, such as a chunk's offsets out of bounds once earlier chunks are out, ends the program: the end of a contained worker would release its storage, and the end of a contained task would give it to the thread's next task, while the tasks still running write into it.
- **`gs_pool_destroy` and `gs_pool_check_open`**:
  `pool.close()` emits `gs_pool_destroy(pool)`, which first runs and waits for any task the pool has yet to finish, as `gs_pool_wait` does, since a pool of one worker has no thread of its own that would. It then joins the pool's threads and unmaps their Data Stack regions and those in `caller_regions`, and then sets the variable to `NULL`, the closed pool, which `gs_pool_destroy` leaves alone. `pool.apply` takes the pool's variable, not its handle, as its arguments may close the pool and free what the handle points at: it reads the handle once they have run, and again in each round that builds an argument into a chunk's packet, and calls `gs_pool_check_open(pool, file, line)` on it before handing the pool anything, which aborts with `GS_E_POOLCLOSED` on a closed pool. No scope exit emits either; `gs_rt_stop` (a library's `goose_exit`) destroys the pools a program leaves open.

---

## 6. Worked examples (including parallel Hash Join)

### 6.1 Smallest example: two-pass parallel transform

```goose
fn scale_chunk(chunk: i64[:], weights: const i64[:], bias: i64) {
    for &x, i in chunk {
        x = x * weights[i % weights.len] + bias;
    }
}

fn main() {
    var pool = thread_pool(4);
    defer pool.close();
    let weights = [10, 20];
    var data: i64[>..] = [1, 2, 3, 4, 5, 6];

    pool.apply(chunks(data, 2), scale_chunk, weights, 1);
    print("pass 1: ", data);   // [11, 41, 31, 81, 51, 121]

    pool.apply(chunks(data, 3), scale_chunk, weights, 0);
    print("pass 2: ", data);   // [110, 820, 310, 810, 1020, 1210]
}
```

### 6.2 Parallel Database Hash Join (`chunks(counts, 1)` + `chunks(joined, offsets)`)

See `test/threads/parallel_scope.goose` for a complete runnable two-pass parallel Hash Join:

```goose
// Pass 1: Each worker counts matches for its probe morsel `[idx * step .. (idx + 1) * step]`
pool.apply(chunks(counts, 1), count_join_matches, ht, probe, step);

// Main thread computes prefix-sum offsets and resizes `joined` once:
var offsets: i64[>..] = [0];
for c in counts { offsets.push(offsets[offsets.len - 1] + c); }
var joined: JoinedRow[>..] = [];
joined.resize(offsets[offsets.len - 1], default<JoinedRow>());

// Pass 2: Each worker writes its matches directly into `joined[offsets[idx] .. offsets[idx + 1]]`!
pool.apply(chunks(joined, offsets), write_join_matches, ht, probe, step);
```

### 6.3 The probe in one pass, and a parallel filter (`chunks(out, num_tasks)`)

With a worker that grows an output of its own (§3.4), the probe needs no count first, which reads and filters the probe side a second time, and no offsets: each morsel's matches are pushed as they are found, and the apply appends them to `joined_once` in morsel order. The same file runs it beside the two-pass join, and a parallel filter whose tasks keep as many rows as their morsels hold:

```goose
// Each task probes its morsel `[idx * step .. (idx + 1) * step]` and pushes what matches:
fn probe_join_matches(out: JoinedRow[>..]&, idx: i64, ht: const HashSlot[:],
                      probe: const ProbeRow[:], step: i64, num_parts: i64, slots_per_part: i64) {
    // ... for each slot `s` of `ht` whose key matches `probe[i]`:
    out.push(JoinedRow { key: probe[i].key, user_id: s.user_id, cents: probe[i].cents });
}

var joined_once: JoinedRow[>..] = [];
pool.apply(chunks(joined_once, num_morsels), probe_join_matches, ht, probe, step, num_parts,
           slots_per_part);

fn filter_chunk(out: i64[>..]&, idx: i64, src: const i64[:], step: i64) {
    let lo = idx * step;
    let hi = min(lo + step, src.len);
    for i in lo..hi {
        if src[i] % 3 == 0 { out.push(src[i]); }
    }
}

var filtered: i64[>..] = [];
pool.apply(chunks(filtered, 4), filter_chunk, src, step);
```
