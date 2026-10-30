# First u64 atomics readiness

Prepared branch: `upstream/shared-atomic-u64`.
Stack base: `upstream/shared-capability-substrate` at
`231dcbc79835273313c053de5a95bbb8ac784d01`.
Ultimate upstream base: `0357b1c2e261fa0680008e79d6879f9ccc7aedb9`.
Implementation checkpoint: `fced57b40371ec47a35b8aeba9be068631d53f09`.
The readiness commit is the later branch head (`git rev-parse HEAD`); its own
hash cannot be embedded in the same commit.

Implementation delta before readiness files: 20 files, +390/-9 relative to the
substrate branch, including +38/-2 generated runtime mirror lines.
Final branch delta including readiness files: 22 files, +505/-9.

The red-first checkpoint `b1045033` adds the u64 claims/order tests before the
implementation exists. `build/goose check test/memory/shared_atomics.goose`
then failed with unknown `shared_atomic_u64` / `SharedAtomicU64`. The subsequent
implementation adds only constructor, lock-free query, load, store, fetch-add
and strong CAS; no U32, indexed atomic cells, exchange, other RMWs, raw pointers
or raw addresses. The value uses the substrate's existing 16-byte packed
capability representation, without an unused array index.

## Validation evidence

Linux x86-64, GCC 14.2.0, Clang 23.1.3; Release Goose compiler build.
Use the installed Clang bin directory before the unconfigured mise shim.

```
cmake --build /tmp/goose-upstream-safe-build -j 4
python3 test/run_tests.py --exe build/goose --no-jit -j 8
python3 test/shared_capabilities.py --exe build/goose
python3 test/shared_atomic_u64.py --exe build/goose
```

- Full native suite: all tests passed (66 seconds, 8 jobs). 1450 parse/typecheck
  checks, 693 generated-C runs, 204 targeted debug checks, 21 compiler/runtime/
  Clang checks, and 1928 rejection checks passed, alongside the other sections.
- Focused shared suite: GCC and Clang, O0 + GS_DEBUG=1 and O2 + GS_DEBUG=0 all
  passed; Clang ASan/UBSan lifecycle instrumentation passed.
- Atomic suite: all 40 order cases passed (25 CAS order pairs, five each for
  load/store/fetch-add). O0/O2 generated-C inspection verified native load,
  store, fetch-add, strong CAS (`weak=0`), exact order constants and lock-free
  generation/width gates. i486 cross-target gate failed as required; x86-64
  passed without requiring a cross-libc installation.
- Forty repeated GCC/Clang debug/release runs each checked all 4000 job IDs
  individually for uniqueness and completeness. Each run also checked unsigned
  overflow wrap, CAS success/failure observations, packed capability fields/
  worker packets, bounds/alignment failures, stale handles after close and slot
  reuse, exclusion in both directions, and close while an atomic worker waits.
- Linked native test executables had no unresolved `__atomic_*` library calls.
- Clang ASan/UBSan execution of the generated atomic fixture passed.
- Clang TSan executions of both the generated atomic fixture and direct runtime
  lifecycle fixture passed, with no race report. Commands:

```
build/goose -O2 --standalone -o /tmp/goose-safe-atomic-tsan.c test/memory/shared_atomics.goose
clang -O1 -g -fsanitize=thread -pthread /tmp/goose-safe-atomic-tsan.c -lm -o /tmp/goose-safe-atomic-tsan
/tmp/goose-safe-atomic-tsan
clang -O1 -g -fsanitize=thread -pthread test/memory/runtime_shared_lifecycle.c -lm -o /tmp/goose-safe-lifecycle-tsan
/tmp/goose-safe-lifecycle-tsan
```

The direct lifecycle test deliberately injects a native create failure in a
child process; its expected stderr is `cannot create thread`, followed by the
successful parent test result. The test verifies rollback of worker leases
and packet allocations before the child's process-fatal exit.

The archived clean upstream baseline passed its complete native suite. The
four owner-reported known failures, including dispatch_result_prefix and
stdlib_regex at O2/debug, were not reproduced in that run. They are not claimed
as fixes. Earlier environment-only failures were corrected by locating the
stdlib beside the compiler and selecting the installed Clang binary.

The full suite explicitly skipped embedded JIT execution and unavailable
multimedia modules; atomic/thread execution through embedded TinyCC remains
unsupported. Native execution on other platforms and weak-memory hardware is
not verified. Sanitizer and repeated stress results support these tested paths;
they are not a general proof of race freedom or native-FFI lifetime safety.

## Handoff

No PR is opened for this branch. Its substrate is a substantive language
extension requiring maintainer agreement. The drafted description is
`docs/pr-drafts/shared-atomic-u64.md`. Owner review precedes any upstream opening.
