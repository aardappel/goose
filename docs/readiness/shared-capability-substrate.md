# Shared capability substrate readiness

Prepared branch: `upstream/shared-capability-substrate`.
Base: upstream master `0357b1c2e261fa0680008e79d6879f9ccc7aedb9`.
Implementation checkpoint: `0b7a44fda35e9c9ad914d697a74b9622f5b6c2e1`.
The later readiness commit is the branch head; use `git rev-parse HEAD` on this
branch (a commit cannot include its own hash).

Implementation plus tests/docs: 40 files, +1268/-8 relative to upstream,
including +303 generated runtime mirror lines. Readiness and PR draft files
are additional; final counts are reported in the handoff.

Red first: `465269c4` adds the lifecycle test before shared storage exists;
compilation fails with unknown `gs_shared_cap`. `2e800dcd` extracts the initial
substrate, and `0b7a44fd` narrows/fixes it and adds language-boundary coverage.
The first extracted lifecycle run failed the packed capability ABI assertion
before packing the copied wrappers. Registry records remain naturally aligned.

Scope follows the Astra study at `fa619cc02`, with no raw-address atomics or
other fork-only memory extensions. No PR is authorized for this branch yet.

Validation commands (Clang's installed bin directory precedes its unconfigured
mise shim on PATH):

```
cmake -S . -B /tmp/goose-upstream-safe-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/goose-upstream-safe-build -j 4
python3 test/run_tests.py --exe build/goose --no-jit -j 8
python3 test/shared_capabilities.py --exe build/goose
```

Focused suite passed GCC 14.2 and Clang 23.1 debug (`O0`, `GS_DEBUG=1`) and
release (`O2`, `GS_DEBUG=0`), plus Clang ASan/UBSan lifecycle instrumentation.
Each compiler checked scalar behavior, packed arguments/aggregates and runtime
lifetime cleanup. All shared compile-rejection diagnostics passed.
Failed native thread creation is injected on POSIX; Windows execution is not
verified. Allocation failure, pending handoff abort, normal/aborted worker
cleanup and descendant orphan reclamation are counted for resource leaks.

A clean, archived upstream source baseline passed its entire native test suite
with GCC and Clang available. The four owner-reported upstream failures
(including dispatch_result_prefix and stdlib_regex at O2/debug) were not
reproduced in that run; no claim is made that this patch fixes them. Initial
runs with a relocated compiler and unconfigured Clang shim had environment
failures, corrected before the clean baseline run.

Full candidate native suite: all tests passed (76 seconds, 8 jobs); 691
generated-C runs, 204 targeted debug checks, 21 compiler/runtime/Clang checks
and 1921 compile-rejection checks. The subsequently added packed-language
fixture also passed the focused GCC/Clang matrix.

Linux x86-64 is the execution-verified platform. Other platforms remain
unverified; embedded TinyCC threaded execution is unsupported. No TSan result
is claimed. No general GC or ownership inference is introduced.
