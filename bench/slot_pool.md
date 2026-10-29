# slot_pool versus reusable

`reusable` and `reusable[]` remain deprecated but available. These comparisons
keep the builtins as baselines; removal is a separate change. Application
measurements and migration decisions are in [pool_workloads.md](pool_workloads.md).

The library uses the builtin’s LIFO free-index policy. Both versions allocate
initialized slots, retain stable addresses, and check indices. The workload
uses two local owners, calls the same `exercise` helper with each, and checks
allocation order and final contents.

## Current measurements

Measured 2026-10-28 on Windows, Ryzen 9 9950X3D, CPUs 16–31. Ratios are
library / builtin; smaller is better. Clang compiles the generated C at O2.
Each cell is the ratio of median process-cycle counts from seven interleaved
samples after two warmups, with identical checksums. The six-case means give
equal weight to each workload/size pair. These allocation-heavy loops have no
application work between operations.

| Workload | Bytes | O1 Clang | O1 TinyCC | O1 JIT total | O2 Clang | O2 TinyCC | O2 JIT total |
|---|---:|---:|---:|---:|---:|---:|---:|
| append | 8 | 1.029 | 0.968 | 1.029 | 0.981 | 0.973 | 1.033 |
| append | 128 | 0.990 | 0.996 | 1.012 | 0.964 | 0.997 | 1.004 |
| lifo | 8 | 0.861 | 0.939 | 1.001 | 0.872 | 0.926 | 1.009 |
| lifo | 128 | 0.904 | 0.986 | 1.095 | 0.879 | 0.983 | 1.066 |
| scattered | 8 | 0.913 | 0.952 | 0.990 | 0.888 | 0.962 | 0.988 |
| scattered | 128 | 0.911 | 1.011 | 0.936 | 0.873 | 1.012 | 0.920 |
| Geometric mean | | 0.933 | 0.975 | 1.009 | 0.908 | 0.975 | 1.002 |

Standalone TinyCC measures execution only. **JIT total** includes Goose
compilation, TinyCC compilation and execution at the stated optimization level;
it is not an allocation-only measurement. O2 is now the default. Process startup,
initialization and final checksumming are included on both sides. Wall times
are also retained, but were more sensitive to background activity than process
cycles.

## Reproduce

```text
python bench/slot_pool.py --exe build-root/Release/goose.exe --levels 1 2 --calls natural --widths 1 16 --compilers clang tcc jit --reps 7 --warmups 2 --cpus 16-31 --out build-root/review-final-slot
```

The harness saves Goose/C snapshots, logs, checksums, compiler and library
fingerprints, sizes and all samples under `--out`. The checked-in
[current results](slot_pool_results.json) contain this matrix. `--levels 0`
is also supported. `--calls natural` leaves the emitted C unchanged;
`boundary` adds `GS_NOINLINE` to the native `exercise` function, while `inline`
requests inlining. JIT always uses the compiler's unmodified output. TinyCC is
discovered beside the Goose build or on PATH. MSVC is available via
`--compilers msvc`; timings here use Clang, standalone TinyCC and the JIT.

## Interpretation and remaining work

`alloc_ref` returns the append result directly. Index builders and reference
frees are available as `alloc_index_with` and `free_ref`; application results
using them are in [pool_workloads.md](pool_workloads.md). The library keeps its
index checks, and no occupancy tracking or private-field assumption was added.

Native Clang and standalone TinyCC both meet the general microbenchmark goal.
TinyCC keeps every C temporary in memory and copies a struct assignment with
string moves (`movsq`), so until 2026-10-28 the library's 8-byte reuse ran 1.14–1.20x the
builtin's under it (geometric mean 1.08–1.10). The compiler changes that closed
it are general: a pop the preceding length test makes safe drops its emptiness
check (bounds-check elimination), pops, pushes and element reads and stores keep
no temporaries they do not need, a small struct variable is stored as one
integer, and `i < 0 || i >= items.len` is one unsigned comparison. The index
check on a slot popped from the public freelist stays. JIT totals, which
include compiling each program, are at parity on average.
