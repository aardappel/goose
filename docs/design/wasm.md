# Running Goose on WebAssembly

<details>
<summary>Table of contents</summary>

- [1. The problem](#1-the-problem)
- [2. What every option needs: growth checks](#2-what-every-option-needs-growth-checks)
- [3. Fixed partitions](#3-fixed-partitions)
- [4. Relocating stacks](#4-relocating-stacks)
- [5. Options that do not work today](#5-options-that-do-not-work-today)
- [6. Where relocation's changes fall](#6-where-relocations-changes-fall)
- [7. Wasm costs outside the data stacks](#7-wasm-costs-outside-the-data-stacks)
- [Recommendation](#recommendation)

</details>

This is a proposal and is not implemented. Spec §10.4 says that platforms
without address-space reservation fall back to "index-based references +
bounds-checked growth", and leaves the details open (open issue 15). This
note works them out. Its cost figures are estimates, not measurements.

## 1. The problem

Each data stack is a region of reserved address space, committed as it is
touched ([spec §1.2](../goose_spec.md#12-memory-model),
[§10.4](../goose_spec.md#104-runtime-environment),
[implementation §7](../implementation.md#7-the-runtime)). Growth is a
pointer bump with no capacity check, and a guard gap past each region turns
an overrun into an abort. References into a stack are plain addresses and
stay valid across growth because a stack's base never moves, a property the
language is built around ([tutorial](../tutorial.md#nothing-moves-so-references-survive-growth)).

WebAssembly has none of this. A module's linear memory grows only at its end,
has no unmapped pages inside it, and in wasm32 holds at most 4 GB. A port that
gives each of a few stacks a small fixed size works, but only toy programs
fit.

## 2. What every option needs: growth checks

Linear memory has no guard pages, so a region that overruns writes into the
next one. Every approach therefore checks growth, `if (lim - top < n)`,
including a port that keeps fixed regions. The options differ in what a
failed check does: abort, or move the stack. Only moving forces a different
pointer representation.

The checks go wherever a stack's top moves up:

* pushes, appends and `resize` (`EmitPush`, `EmitAppend`);
* in-place construction: `str()`, `to_bytes()`, `print`'s rendering, a
  length prefix reserved and patched afterwards (`OpenRzDest`,
  `EmitReprefix`, `EmitSlidePrefix`, varint prefixes), frame objects, the
  `_er` twins, the generated walkers;
* the runtime: a slice pool's freelist, which grows on a stack of its own
  (`gs_spans_*`), `gs_bld_append` in `goose.h`, and the helpers that write at
  a top with no bound, such as `gs_fmt_i64(top, v)` (up to 20 bytes). Each of
  these needs a reservation first: a worst case for a scalar, a check per
  chunk when rendering a nested value.

The top caches ([implementation §6.10](../implementation.md#610-loop-invariant-views-and-stack-top-caching))
become (top, limit) pairs, synchronized at the same points. A loop with a
known trip count can reserve once before it.

## 3. Fixed partitions

Each stack gets a fixed slice of linear memory, and a failed check aborts
with the overrun message native builds give. This is mostly configuration of
the existing runtime: `GS_STACK_RESERVE` is the linear memory divided by the
region count, and `GS_STACK_BUDGET` is the linear memory, which the runtime
already uses to cap `hardware_threads()`.

* **RAM.** On 64-bit desktop engines, linear memory that is grown but never
  touched is generally not backed by RAM (Windows charges commit for it), so
  growing to the maximum at startup wastes address space, not memory.
  Browsers may refuse a large memory on small devices, and Wasm cannot give
  pages back after a peak (there is no `memory.discard`).
* **Capacity.** The real limit is each stack's size. Programs need few
  regions: `--stacks` reports 5 for `samples/14_bst`, 6 for
  `12_huffman`, 10 for `18_json` (8 stacks and 2 globals' stacks), and 6
  for `23_mandelbrot_threads` plus 2 per worker. 4 GB over 10 regions is
  400 MB per stack. Workers multiply the count.
* **Sizing from a profile.** The high-water mark of each stack index in a
  representative run could be used to give the partitions different sizes.
  On Windows the fault handler commits on demand and so can report each
  region's depth (its commit mark). macOS and Linux would need explicit
  instrumentation to track the highest top across stack rewinds.
  `GS_STACK_STATS` reports only counts today. Stack numbering is
  specific to one build, and a profile fits only the input it came from,
  while the inputs that overflow are the large ones.
* **The open end.** The partition at the end of linear memory can keep
  growing with `memory.grow`. If the stack most likely to hold the
  program's large resizable is placed there, the common case (one big
  growing array) is limited only by the memory's own maximum.
* **wasm64** removes the 4 GB address limit in principle, but browsers still cap
  memory64 (V8 at 16 GB), and each access needs an explicit bounds check.
  wasmtime can reserve much more and commit it lazily. Fixed partitions under
  wasm64 therefore work much like the native model and are a practical choice
  for WASI and server-side targets.

## 4. Relocating stacks

Each stack is a heap block that moves to a block twice its size when a
growth check fails. Since a stack can move, a reference cannot hold an
address: it holds a handle.

* **Encoding.** A handle is one `u64`: the stack's id in the high bits and the
  offset in the low bits. A handle with id 0 holds a plain address, for
  static data, C memory and clang's shadow stack (where address-taken fixed
  locals live). Its base table entry is 0. References stay 8 bytes, so every
  layout stays the same as on the 64-bit hosts the C backend targets
  (implementation §6.1). The base table belongs to the thread program, like
  `gs_stks`, and also covers the globals' dedicated stacks.
* **Arithmetic is unchanged.** `base + i * size`, field offsets, the
  cursors that walk variable-size fields (`FieldPtr`), differences within a
  stack and a pool's range test all work on the integer. Only loads, stores
  and the `mem*` calls decode a handle; in wasm32 the decode is
  `bases[h >> 32] + (uint32_t)h`.
* **Relative references and pools need no change.** They are offsets within
  one stack, and a stack moves as a whole.
* **Decodes can be hoisted.** A decoded pointer stays valid until its stack
  grows. A decode can be hoisted over an extent with no growth of that stack
  and no call that can reach it, and top caching already computes those
  extents (`SyncReach`, `PlanTopCaches`). Array loops through hoisted views
  (`AddView`) then run as they do natively. The cost falls on pointer chasing
  (a decode per hop through a `T&`) and on pushes, whose check fixed
  partitions need too. The estimated time is 1.1 to 1.5 times that under
  fixed partitions, worst on tree and list code. The doubling amortizes the
  copying on relocation. Counting the old and new blocks and fragmentation,
  peak memory is up to two to three times the live data.
* **Threads.** Each thread program has its own base table, thread-local. A
  spawn copies flat images, so no handle crosses threads.

## 5. Options that do not work today

* **Multi-memory.** A memory per stack would keep offsets stable across
  `memory.grow`, but a load names its memory as an immediate. Stacks are
  chosen at run time (`gs_sp + k`, a fat reference's `stk`), and C cannot
  target several memories.
* **The memory-control proposal** (`memory.discard`, protect, map) would bring
  back reserve, commit and guard pages, and with them the native model. It
  has not shipped.
* **GC arrays** cannot grow, so they relocate as heap blocks would, and C
  cannot reach them.
* **A Wasm dialect that forbids references across growth** breaks the
  property the language is built around.

## 6. Where relocation's changes fall

In order of risk, not size:

1. **In-place construction while the same stack grows** (the list in §2).
   Natively, a held pointer to a slot's start or to a prefix still to be
   patched survives growth. Under relocation it must be a handle, or be
   derived again after the growth.
2. **The assumption that the base never moves.** View hoisting relies on it
   (its safety argument is that growth writes only the length), and so do
   the top caches, reference aliases (`refalias`) and `let first .=
   v.push(...)`. Little code depends on this assumption, but that code holds
   most of the potential bugs. The rule that contains them is to use handles
   everywhere, and decoded pointers only over extents that the sync analysis
   proves.
3. **The C boundary**: the runtime, `extern fn`s, the blessed packages and
   `goose.h`. The C ABI can stay on plain pointers. Slices and references are
   decoded at the call, since a C callee can grow a Goose stack only through
   a builder. A builder parameter gets a staging stack whose contents are
   appended after the call returns. Packages then recompile unchanged, with
   `gs_bld_append` gaining its capacity check.
4. **The way codegen writes memory accesses**, which is mechanical work to
   change. A rough count is 35 `*(T *)` dereferences, 140 emitted `->`, 26
   `mem*` calls and 21 `->top`, mostly through `LoadLoc`, `IndexLoc`,
   `DerefLoc`, `PointeeLv` and `VarLoc`.
5. **Unchanged**: the parser, the typechecker, the optimizer, BCE apart from
   view hoisting, and the standard library, which is Goose source the same
   codegen compiles. The library's only contact with C is its externs: math on
   scalars, `find_any` and `find_pair` on slices, `os`'s builders, and
   `json`'s `frexp` with an `i32&`.

## 7. Wasm costs outside the data stacks

* **Native call stack overflow.** The runtime reports it as a runtime error
  (implementation §7). Wasm has no vectored handler or `sigaltstack`, so the
  engine traps instead. Clang's shadow stack (64 KB by default) overflows
  silently unless checked (`-z stack-size`, Emscripten's
  `STACK_OVERFLOW_CHECK`).
* **The OS layer** (`runtime_os.h`) maps onto WASI or Emscripten.
* **Threads** need shared memory, which browsers give only to
  cross-origin-isolated pages.

## Recommendation

1. Add the growth checks of §2 as a configuration of their own. Native
   builds can run with them, so the existing test suite covers them. Fixed
   partitions are then only a matter of configuration, and they make a
   working port for programs whose stacks fit.
2. If that is not enough, add relocation as a native configuration first:
   stacks in heap blocks that move on growth. Give it a stress setting that
   moves a stack on every growth, so the test suite exposes every stale
   decoded pointer, and use the benchmark suite to measure the cost. Only
   then bring in a Wasm toolchain.
