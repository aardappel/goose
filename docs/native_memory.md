# Native memory

Native memory is separate from Goose arrays, references and copied worker
globals. `SharedOwner`, `SharedView` and `SharedAccess` are opaque compiler types. Safe Goose cannot construct their
fields, convert integers to them, serialize them, or send them through queues
or C signatures. Copying a capability copies its authority; it does not create
a second allocation or a second right to free storage.

## Allocate, borrow and close

```goose
let owner, ok = shared_alloc(4096);
assert(ok);
let view = shared_view(owner, 0, 4096);
let access = shared_borrow(view);
memory_write<u32>(access, 1, 1234, false);
assert(memory_read<u32>(access, 1, false) == 1234);
shared_release(access);
assert(shared_close(owner));
```

`shared_alloc(bytes)` returns an owner and a success flag. Zero size, sizes
outside the native address space and OS allocation failures return false and
an invalid owner. POSIX uses anonymous `mmap`; Windows uses `VirtualAlloc`.
The runtime never fills or eagerly touches the allocated byte range. OS demand
paging supplies initial zero bytes; physical-memory exhaustion on later page
fault follows the operating system's policy.

`shared_view(owner, offset, bytes)` checks the complete subrange.
`shared_size(owner|view|access)` reports its byte length as `u64`. No operation
uses unchecked `offset + size` arithmetic. A zero-length view is valid at the
end of an allocation.

Every ordinary load and store requires a live `SharedAccess` returned by
`shared_borrow(view)`. A borrow authorizes the calling thread exclusively for
that byte range. Creating any overlapping borrow fails, including a second
borrow on the same thread. Disjoint ranges may be borrowed independently.
`shared_release(access)` ends the authorization and invalidates all copies of
that access capability. It is an error to read, write or release it afterward.
Release before waiting for another worker that needs an overlapping range.

`memory_read<T>(access, offset, big_endian)` and
`memory_write<T>(access, offset, value, big_endian)` support exactly
`i8/i16/i32/i64/u8/u16/u32/u64`. `false` selects little endian and `true` selects
big endian, independently of host byte order. The runtime copies scalar bits
with `memcpy`, so unaligned byte offsets are supported. Bounds and read-only
permissions are checked in every build.

Only the owner thread can close an allocation. `shared_close(owner)` returns
false if it is already closed, invalid, has live worker leases, or has active
borrows; a false result leaves a live allocation open. Successful close
unmaps once and invalidates every derived capability. A slot reused by a later
allocation has a new generation, so stale values never revive.

Worker arguments may explicitly include `SharedView`. Owners and active accesses cannot cross worker boundaries.
Ordinary references, arrays and worker globals retain their existing isolation
rules. Leases are acquired before native worker creation and retained through
all worker execution. Normal return and a contained abort both release
abandoned borrows, owned allocations and worker leases before completion is
published. A worker-created allocation passed to a child is reclaimed only
after the last child's lease ends. Main normal return performs the same
context cleanup. Process-fatal exits rely on OS reclamation.

Capability records occupy stable reusable metadata slabs. Mapping storage is
released on close; the slabs retain generation tombstones and grow only with
peak simultaneous descriptor usage. Routine scalar accesses allocate no heap
memory. Borrow creation, view creation and worker handoff are setup operations.

## Candidate scope and targets

This candidate adds anonymous storage and scalar access only. It has no file
mapping, page advice, raw pointers, atomics, byte-buffer operations or byte copy.
The new storage semantics require upstream maintainer agreement before a PR.
Linux x86-64 with GCC and Clang is execution-tested. Other native targets are
not execution-verified. Embedded TinyCC cannot run the required threaded TLS
runtime. Ordinary Goose values and worker globals keep their existing rules.
