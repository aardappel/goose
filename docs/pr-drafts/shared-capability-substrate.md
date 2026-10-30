# Add bounded shared storage capabilities and worker leases

This proposal adds explicitly shared anonymous storage without changing Goose's
ordinary isolated-worker values. `shared_alloc` returns an opaque `SharedOwner`;
`shared_view` selects a bounded range; `shared_borrow` grants exclusive,
thread-local scalar access until `shared_release`. Disjoint ranges can be
borrowed concurrently. `memory_read<T>` and `memory_write<T>` check bounds in
all builds and support unaligned endian-aware integer access.

Only the allocating thread can close storage. Close refuses active borrows or
worker leases. Generation checks reject stale aliases, and normal/contained
worker cleanup releases abandoned accesses and leases, including descendants.
Owners and accesses cannot cross a worker boundary; direct views can. Safe
capabilities have no raw C ABI or serializable byte image.

This is a substantive language extension and is prepared for discussion only.
Maintainer agreement is required before opening this PR. File mapping, page
advice, unsafe pointers, atomics, indexed arrays and byte-copy APIs are deferred.

Validation: upstream baseline and complete candidate suite, focused GCC 14 and
Clang 23 debug/release checks, Clang ASan/UBSan lifecycle checks. Lifecycle
coverage includes stale aliases, range overlap/exclusion, disjoint borrows,
owner-only close, overflow bounds, packed capability alignment, live-worker
close, nested workers, failed native creation rollback, allocation failure,
and abort cleanup. Exact results and limits are in the readiness report.
