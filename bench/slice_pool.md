# slice_pool versus reusable[]

`reusable` and `reusable[]` remain deprecated but available. These comparisons
keep the builtins as baselines; removal is a separate change. Application
measurements and migration decisions are in [pool_workloads.md](pool_workloads.md).

The library keeps its free spans in the builtin's own augmented B+ tree: the
runtime's span tree, reached through three externs on its `nodes` and `tree`
fields. Its copy of the tree functions differs from `reusable[]`'s only in
taking the header apart from the nodes and in checking what it reads from
them, which the program can write. Differential tests check placement as well
as contents.

## Current measurements

Measured 2026-10-28 on Windows, Ryzen 9 9950X3D, CPUs 16–31. Ratios are
library / builtin; smaller is better. Clang compiles the generated C at O2.
Each cell is the ratio of median process-cycle counts from seven interleaved
samples after two warmups, with identical checksums. The six-case means give
equal weight to each workload/size pair. These allocation-heavy loops have no
application work between operations.

| Workload | Bytes | O1 Clang | O1 TinyCC | O1 JIT total | O2 Clang | O2 TinyCC | O2 JIT total |
|---|---:|---:|---:|---:|---:|---:|---:|
| append | 8 | 0.934 | 1.033 | 1.100 | 0.960 | 1.017 | 1.069 |
| append | 128 | 1.007 | 1.027 | 1.017 | 1.009 | 1.036 | 1.009 |
| fragmented | 8 | 0.994 | 1.084 | 1.205 | 0.995 | 1.087 | 1.196 |
| fragmented | 128 | 0.999 | 1.080 | 1.140 | 1.006 | 1.077 | 1.140 |
| resize | 8 | 1.011 | 1.143 | 1.229 | 1.014 | 1.116 | 1.194 |
| resize | 128 | 0.995 | 1.157 | 1.253 | 1.003 | 1.112 | 1.140 |
| Geometric mean | | 0.990 | 1.086 | 1.154 | 0.998 | 1.073 | 1.123 |

The same run measured the previous library, whose tree was checked Goose code,
as a third side. The new library takes this fraction of its cycles:

| Workload | Bytes | O1 Clang | O1 TinyCC | O1 JIT total | O2 Clang | O2 TinyCC | O2 JIT total |
|---|---:|---:|---:|---:|---:|---:|---:|
| append | 8 | 1.016 | 1.007 | 0.879 | 1.019 | 1.005 | 0.849 |
| append | 128 | 1.002 | 1.010 | 0.967 | 1.004 | 1.011 | 0.975 |
| fragmented | 8 | 0.892 | 0.730 | 0.457 | 0.901 | 0.731 | 0.453 |
| fragmented | 128 | 0.925 | 0.754 | 0.507 | 0.923 | 0.751 | 0.506 |
| resize | 8 | 0.918 | 0.729 | 0.470 | 0.872 | 0.708 | 0.460 |
| resize | 128 | 0.882 | 0.793 | 0.615 | 0.910 | 0.762 | 0.541 |
| Geometric mean | | 0.938 | 0.829 | 0.620 | 0.937 | 0.819 | 0.601 |

Standalone TinyCC measures execution only. **JIT total** includes Goose
compilation, TinyCC compilation and execution at the stated optimization level;
it is not an allocation-only measurement. Process startup, initialization and
final checksumming are included on both sides. Wall times are also retained,
but were more sensitive to background activity than process cycles.

## Reproduce

```text
python bench/slice_pool.py --exe build/Release/goose.exe --levels 1 2 --calls natural --widths 1 16 --compilers clang tcc jit --reps 7 --warmups 2 --affinity 0xffff0000 --previous-library <old>/slice_pool.goose --out build/slice-pool-bench-final
```

`<old>` holds `stdlib/slice_pool.goose` and `stdlib/slice_pool_internal.goose`
from `06a6300f`. The harness saves Goose/C snapshots, logs, checksums,
compiler and library fingerprints, sizes and all samples under `--out`. The
checked-in [current results](slice_pool_results.json) contain this matrix.
`--levels 0` is also supported. `--calls natural` leaves the emitted C
unchanged; `boundary` adds `GS_NOINLINE` to the native `exercise` function,
while `inline` requests inlining. JIT always uses the compiler's unmodified
output. TinyCC is discovered beside the Goose build or on PATH. MSVC is
available via `--compilers msvc`; timings here use Clang, standalone TinyCC
and the JIT.

## Interpretation and remaining work

With the tree in C, Clang is at parity. Under the JIT the tree runs natively,
as `reusable[]`'s does, which is where the old library lost most (2.0–2.7×);
under standalone TinyCC both trees are TinyCC code. What remains under
TinyCC and the JIT is the Goose code around the tree calls, which TinyCC
compiles as written. At O2 the resize loop runs about 60 cycles per step
more than the builtin's under both. Of those, the checks the tree copy makes
take about 1.5% of the run under TinyCC. Three 16-byte slice copies that
inlining `realloc_slice` and `finish` binds as parameters take about 12
million of 58 million cycles. Snapshotting the `gs_rref` argument, writing
back cached tops around the call, and the library's own bounds and span
checks take about 1% each. Calling the tree through a wrapper made no
measurable difference. JIT totals also pay for compiling the inlined library
code, about 7–15 million cycles a program here, which is most of the append
rows' gap. Not inlining `alloc_slice` slowed append without helping reuse.

`reusable[]` keeps the unchecked tree functions. Sharing the checked copy
measured 3–5% slower for it on fragmented and resize under TinyCC and the
JIT. With its own copy it measures at master's speed: geometric means of
1.001 Clang, 0.991 TinyCC and 1.017 JIT over the same 12 workload/level
pairs.
