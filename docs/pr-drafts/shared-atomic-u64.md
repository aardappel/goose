# Add capability-based u64 atomics in registered shared storage

Builds on the separately proposed shared capability substrate. This adds one
opaque `SharedAtomicU64` cell type with `shared_atomic_u64`,
`atomic_is_lock_free`, load, store, fetch-add and strong compare-exchange.
Atomic operations accept only that capability. Ordinary references and raw
integer addresses are rejected; no raw-address atomic interface is included.

Construction checks natural alignment, bounds and range exclusion. Atomic
bytes cannot overlap ordinary borrows or other registered cells. Worker leases
retain the allocation through the entire instruction; generation checks reject
closed/reused handles. Capability wrappers remain byte-packed while registry
metadata and atomic cells stay naturally aligned.

Orders are explicit compile-time strings. Loads, stores and both CAS orders
are validated, including the relative strength of failure order. Fetch-add
wraps modulo 2^64. Generated GCC/Clang C uses lock-free `__atomic_*` operations
and strong CAS, with explicit unsupported-backend/width diagnostics and no
mutex fallback. U32, indexed regions, exchange and other RMW operations are
deferred.

Validation: the full native suite passed; GCC 14/Clang 23 debug and release
passed the shared capability suite and 40 repeated exact-once claim runs.
All 40 load/store/RMW/CAS order cases passed. Generated C was checked at Goose
O0/O2 for explicit orders, strong CAS and lock-free gates; i486 was rejected
and x86-64 accepted. Clang ASan/UBSan and TSan fixtures passed. See the readiness
report for exact commands, baseline caveats and platform limits.

Prepared for owner/maintainer discussion only. Do not open this PR until the
shared-storage extension has maintainer agreement and its prerequisite is
accepted. This branch is stacked on `upstream/shared-capability-substrate`.
