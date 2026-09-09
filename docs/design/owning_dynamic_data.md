# Independently owned dynamic data: an editor's open documents

Research example and three possible future extensions, not implemented language
features. The requirement is a runtime-sized set of independently editable
buffers, with memory reclaimed when each document closes. The difficulty is
independent growth and reclamation, rather than simply representing a list of
strings. The current-language alternatives come first; the proposals follow.

## The ordinary owning representation

In C++ the core could be:

```cpp
struct Document { std::vector<std::byte> text; };
std::unordered_map<DocumentId, std::unique_ptr<Document>> open;
```

The corresponding Rust shape is `HashMap<DocumentId, Document>` with
`Document { text: Vec<u8> }`; a `Box<Document>` is optional if the document
object itself needs a stable address. These examples do not promise that an
interior pointer survives a text-buffer reallocation. Edits can use offsets
or temporary borrows. Stable interior references are a separate requirement.

For example: open A (1 MB), open B (20 MB), append 5 MB to A, close A while B
stays open, open C (2 MB), grow B, close C. There is no useful stack ordering:
both allocation and release interleave. A document may later own undo history
or tokens too, but one buffer per document already exposes the issue.

## Goose representations and their costs

| Approach | What works | Cost under these requirements |
|---|---|---|
| `struct Document { text: u8[>..] }`, stored in a growable document array | One standalone resizable-tailed document is legal. | An array cannot contain resizable elements (spec 3.4); `[>..<]` and `reusable` additionally need fixed-size elements. A runtime-sized collection cannot give each element its own resizable tail. |
| A reference table pointing to separate local buffers | Each named local can grow independently and the table can retain references while the owners live. | The program needs a statically named set of owners or an enclosing activation for each owner. Arbitrary open/close order does not match their nested scope lifetimes. A reference table does not create ownership. |
| Inline limited arrays, `u8[..MAX]` | Fixed-size document slots can live in a reusable pool, and each buffer grows/shrinks within its capacity. | Every slot has a `MAX`-byte capacity/stride, though demand paging can leave untouched pages nonresident. A bound suitable for a huge document wastes address/layout space for small ones; exceeding it cannot transparently grow that buffer. This is a good solution when the bound is real and small. |
| Runtime-capacity limited arrays, `u8[..]` | Capacity can match each document at construction. | Capacity still cannot grow, variable-size document records cannot use the fixed-slot reusable pool, and replacing a record does not reclaim an arbitrary hole in an enclosing stack. |
| Append-only byte storage plus per-document offsets/slices | Compact descriptors and stable old data fit the current model. | Replacing/growing text leaves old storage behind until a whole-region reset is legal. Retention follows edit history, rather than the set of live documents. A moving compaction pass requires updating handles and proving no ordinary references survive it. |
| One big grow-shrink buffer containing all documents | Explicitly shifting later data can maintain a compact byte store. | Growing or closing one document moves other documents; descriptors must be fixed up, and outstanding references/slices constrain shrink. The application has taken responsibility for placement and relocation. |
| Reusable fixed-size chunks, linked by indices or relative references | Independent growth and release can be expressed, with storage recycled at chunk granularity. | Someone implements chunk allocation, addressing, cross-chunk iteration, fragmentation policy and any contiguous-view copying; a reusable library could hide this. A rope or piece table is an optional further choice. This may be the right editor representation, but requires more infrastructure than an ordinary owning byte vector. Stale-slot identity also needs an application policy. |
| A worker owning each document | The worker's local buffer has an independent lifetime and is reclaimed on worker exit. | It changes an in-process container into a message protocol, costs an OS thread per document, and requires copying/flattening data that crosses queues. This is attractive when document actors are wanted anyway. |
| Foreign owning storage | A C shim can manage documents behind integer handles. | The ownership implementation and operations move outside Goose's memory model; this does not demonstrate that Goose can express the container itself. |

Recursion does not supply a general escape: cycle functions cannot own new
nonfixed locals (spec 7.8), and ordinary nested activations still close in
stack order. Rebuilding all live documents into another region can bound retained
history if the application creates a quiescent phase and replaces its references,
but adds copying and lifetime coordination to every such phase.

Goose is already a natural fit for immutable document snapshots, a single scoped
document, bounded buffers, or a deliberate chunked editor. The research question
is narrower: can it support independently growing, movable owners with arbitrary
release order while retaining its cheap common case and static reference safety?
The following three extensions are candidates to evaluate against that example.
They do not prescribe a general-purpose heap or settle every part of their
surface syntax and resource-management policy.

## Future option A: bounded independent dynamic stacks

Extend the dynamic stacks idea in spec 11.3 to a runtime-sized collection whose
elements each own an independently growing stack: a resizable array of resizable
arrays. This is a new owning container, not something today's placement rules
already permit. Its descriptor table can grow or reuse slots while the element
storage stays in separately reserved address regions.

For a supported 64-bit target, define a virtual-address reservation budget rather
than assuming that a 64-bit pointer provides 2^64 usable bytes. A runtime profile
could guarantee a budget by reserving it before the collection accepts work;
failure to obtain that reservation is reported at initialization. With usable
capacity `R` per stack, guard space `G`, descriptor space `H` and reserved budget
`B`, the configured total stack count must satisfy `N * (R + G) + H <= B`.
This is a bound on all stacks using that budget, not a separate allowance for
every container. Additional stacks for undo history or tokens count too.

An editor could choose a maximum of 256 open documents, each with a 1 GiB text
ceiling. That requires about 256 GiB of reserved virtual address space plus
guards and metadata, not 256 GiB of resident RAM. Such a configuration is usable
only on a target where that reservation can be guaranteed. It is a reasonable
trade to investigate for a bounded number of substantial buffers, rather than
one independently reserved stack per small string.

* **Opening and growing:** opening document 257 returns a recoverable limit
  error that the editor can explain to the user. Growth beyond a document's
  byte ceiling likewise fails before changing its contents. The configured
  bounds permit explicit checks rather than relying on an overflow guard to
  abort the process. Address reservation does not guarantee physical memory;
  the commit policy must separately define allocation-failure behavior.
* **Stable data, movable owners:** growth within an inner stack's reservation
  preserves its element addresses. Moving an owning descriptor or growing the
  outer table need not move that data. An outer table index is not a permanent
  document identity if slots can be removed and reused; use a stable handle or
  another explicit identity policy.
* **Reference safety:** closing one document must be forbidden while references
  or slices into it can still be used. The checker needs to track an inner
  owner's lifetime, not merely the outer table's lifetime. One conservative
  first design permits access through scoped borrows of a document handle;
  borrowing the whole collection is simpler but restricts unrelated closes.
  Precise per-document borrowing is further design work. A generation check on
  a handle prevents stale-handle reuse, but does not protect an escaped raw slice.
* **Release:** closing a document decommits its pages and recycles its region
  slot independently of other documents. Keeping the address reservation allows
  the runtime to guarantee reuse of its budget; the reservation itself is
  released when its owning collection/runtime budget ends. No document's live
  data has to be copied or compacted to release another's storage.

The remaining questions include per-stack versus pooled size budgets, the owning
descriptor's transfer rules, and how much per-document lifetime precision the
checker can provide without annotation-heavy APIs. Fixed size/count limits are
part of this option's contract, not an accidental failure mode to hide.

## Future option B: variable-size reusable spans in a boxed array

Generalize the reusable-pool allocation unit from one slot to a contiguous run
of `T` elements. An allocator lives inside an explicitly owned, stable backing
array; "boxed array" here describes that separate owner, not an existing Goose
keyword. It could be a scoped backing arena or one dynamic stack from option A.
Different allocations have different lengths and can be released in any order.
Start with fixed-size elements such as `u8`; this already covers text buffers.

The owner tracks each live span's offset, length and capacity, with a freelist
of available runs. Allocation can split a free run, release can coalesce adjacent
runs, and growth can use adjacent free capacity when available. When it cannot,
append allocates another run, copies the existing elements, appends the new ones
and returns the replacement slice. The caller uses that returned slice to see
the appended elements. Existing slices keep their original ranges, including
when append stays in place. Freeing the old run happens only after a successful
replacement; a failed append leaves the original allocation intact.

* **Use after free or relocation is legal:** this option deliberately adopts
  the existing `reusable` semantics. A slice, subslice or element reference into
  a freed or relocated span remains usable within its original range and the
  backing owner's lifetime. Reads and writes access whatever initialized `T`
  values remain at those addresses or are subsequently placed there by reuse.
  An old view does not follow the allocation to its new location, and a write
  through it can affect a later allocation that reuses those cells. These are
  the intended semantics, with no exclusive-borrow or automatic-invalidation
  requirement. Normal writability, bounds and owner-lifetime rules still apply.
* **Bounds and initialization:** a newly returned view covers the initialized
  elements of its allocation. Validate span ranges and size arithmetic before
  construction or copying. Every cell reachable by an old view must remain
  addressable and initialized as `T`, even when the allocator considers it free
  or has split that range between new spans. Keep allocator metadata separate
  from these cells. General `T` also needs defined relocation semantics;
  self-relative links require additional region tracking or must initially be
  excluded from relocating spans.
* **Release and fragmentation:** free makes a run available for another span;
  it need not immediately return all of that allocation's pages to the OS.
  A free run may still have usable slices into it: free alone is not proof that
  its pages are unused. Do not decommit or shrink backing storage that an old
  view can still reach. The backing owner releases its region at the end of its
  lifetime under the normal reference-lifetime rules. Total free space does not
  guarantee a sufficiently large contiguous run. Define a recoverable allocation
  failure, and evaluate fit policies, coalescing and growth slack against real
  document-size distributions. Any optional compaction must preserve old views'
  ranges under the same reuse semantics; moving allocations alone does not make
  their former backing pages safe to release.

This option trades the stable-growth property for denser sharing of a bounded
backing region: a relocating append is O(current length), and capacity slack
and external fragmentation are real costs. The allocator is explicit and local
to this owner; it need not replace Goose's ordinary bump-allocation path.

## Future option C: opaque API resources

Introduce a fixed-size `resource<U>` value, internally a `u64`, for arbitrary
API pointers and handles such as GPU buffers, native byte buffers and file
mappings. `U` is a binding-defined tag identifying the resource kind. The binding
creates and interprets the opaque representation, whether it holds a native
pointer, a native handle or an entry in a binding-owned table.

A resource can be stored wherever a fixed-size value is allowed: locals,
globals, aggregate fields, array elements, arguments and return values. Its
operations in Goose are limited to copying/storing it and passing it to APIs
accepting the compatible resource type. Code cannot dereference it, perform
arithmetic or comparisons on it, or convert it to or from a general integer.
Copying the value copies the opaque handle; it does not itself duplicate the
native allocation. The tag prevents mixing resource kinds, but native resource
lifetimes and API validity still need binding-defined rules.

For example, a GPU binding could return a `resource<GpuBuffer>` and accept it
in upload, draw and release operations. A byte-buffer binding could provide
allocate, resize, release and bulk read/write or copy operations through a
`resource<ByteBuffer>`. This supplies a third route to independently allocated
dynamic memory, accessed through functions and copies. Options A and B remain
preferable when the program wants direct array indexing, slices and ordinary
Goose element references.

Existing FFI already passes scalar handles and Goose buffers to C, including
builders that a C API can append copied bytes into. The proposed tagged resource
type adds an opaque representation to that interface; it is not an implemented
way to obtain a native pointer as a Goose reference.

A file-mapping binding is another example: a map operation could return a
`resource<FileMapping>`, with API functions to query it, copy a range into a
Goose buffer and unmap it. Ordinary growable Goose arrays use the runtime's own
reserved address regions; the current runtime does not map a file into such an
array. Holding a mapping resource therefore would not automatically make the
mapped bytes a Goose array or slice. Direct mapped views would need a separate
design connecting their bounds and lifetime to the resource.

**Cleanup is VERY TBD.** A binding might optionally supply cleanup to run when a
local resource leaves scope; whether or how that extends to resources inside
aggregates is also open. Which copies would trigger cleanup, what happens to
aliases after an explicit release, how repeated cleanup or double-free is
prevented, and what aggregate copying means are unresolved. The proposal does
not yet promise RAII, destructors or automatic ownership transfer. These
questions must be settled before a cleanup hook could be given reliable
language semantics.

## Evaluate the three options together

Independent stacks suit a capped set of large buffers whose addresses should
stay stable while they grow. Reusable spans suit many differently sized buffers;
append returns the current allocation's view while older views legally continue
to access their original cells. Opaque resources suit externally managed memory
and other API objects, accessed through compatible binding functions. The
options can compose: bounded independent stacks can back span allocators, and
API operations can copy between resource-managed buffers and Goose arrays.

Evaluate all three with the open/edit/close sequence above, including the 257th
document, a document exceeding its size ceiling, allocation failure during
append and repeated fragmenting growth/release. For A, check references during
document close; for B, verify legal reads and writes through old slices after
free, relocation and reuse. For C, evaluate API buffer transfers and file-mapping
access while keeping the unresolved cleanup policy explicit. Measure live bytes,
committed pages, reserved address space, copied bytes and latency separately.
That establishes which forms of independent storage are convenient and
predictable for the application.
