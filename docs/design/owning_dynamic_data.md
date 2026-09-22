# Independently owned dynamic data: an editor's open documents

Research example. The requirement is a runtime-sized set of independently
editable buffers, with memory reclaimed when each document closes. The
difficulty is independent growth and release in arbitrary order, rather than
simply representing a list of strings. `reusable[]` slice pools (spec 5.4) now
express this inside the language. This note shows that representation and what
it costs, which earlier workarounds remain useful, and two
possible future extensions for what slice pools leave out: bounded independent
dynamic stacks and opaque API resources. Neither extension is an implemented
language feature.

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

## Documents as runs in a slice pool

A document is a descriptor in a `reusable` pool of slots, and its text is one
run of bytes in a `reusable[]` pool. The open documents are a list of links
into the descriptor pool (§3.9), which is also what lets the operations take a
`Document&`:

```goose
struct Document { id: u64, text: u8[:] }   // the document's current run of bytes

reusable var docs: Document[>..] = [];
reusable[] var bytes: u8[>..] = [];
var open: Document&<u32 in docs>?[>..] = [];   // in tab order; pruned elsewhere

fn open_doc(id: u64, size: i64) -> Document& {
    let d .= docs.alloc_ref(Document { id: id, text: bytes.alloc_slice(size) });
    open.push(d);
    return d;
}

fn grow(d: Document&, extra: i64) {
    d.text = bytes.realloc_slice(d.text, d.text.len + extra);
}

fn close(d: Document&) {
    bytes.free_slice(d.text);
    docs.free(docs.index_of(d));
}
```

`alloc_slice` places a run at the first free span, in index order, that holds
it, or else at the end of the array. `realloc_slice` grows a run in place where
it ends the array or free bytes follow it, and otherwise moves it where a new
run of the new size would go, its own old place included. `free_slice` returns
a run, merged with the free spans beside it. Every byte a run gains is zero.

In the example, A and B are placed end to end. Appending 5 MB to A finds B
directly behind it and no free 6 MB run, so A's megabyte is copied to the end
of the array. Closing A leaves two free runs: A's first place and the 6 MB at
the end, which C then takes the front of. Growing B, now followed by C, copies
B's 20 MB to the first free run that holds the new size: B's own place merged
with the megabyte before it if the growth fits there, or else the end of the
array, starting in the 4 MB left free after C. Closing C merges its bytes with
whichever free bytes border them.

Views keep the `reusable` semantics (spec 9.4). A slice or byte reference
taken before a move or a close keeps its original range, and reads whatever
bytes a later allocation puts there; it never reaches outside the array.
Editing code therefore reads the document's `text` again after anything that can
grow or close it, and keeps cursors as offsets rather than references. A link
in the tab list outlives its document's close the same way: it reads a valid
`Document`, which may by then be another one.

`index_of` is what turns a `Document&` back into the slot `free` takes, and it
recognises a reference a caller passed in as one of `docs`' slots only because
a type names that pool (spec 3.9). A pool no `T&<w in pool>` type names has to
pass slot numbers around instead; `docs/implementation.md` §10 records what
would lift that.

The checker cannot always tell which array a slice read out of `docs` points
into: any other global able to hold a `u8`, a log buffer say, is as good a
candidate as `bytes` (spec 9.5). `realloc_slice` and `free_slice` do not need
it to. A slice they are not shown to be `bytes`' own is checked when the call
runs, and the call aborts unless the slice lies inside `bytes` on an element
boundary: a few compares beside the work the call does anyway. A slice type
naming its pool (`u8[: in bytes]`, like `T&<u32 in pool>`) would make that
knowledge static, if a program needs to avoid the runtime
check or detect an invalid pool assignment earlier.

## What the slice pool costs

* **One reservation for all documents.** The pool is one grow-only array on
  one data stack, so all documents together are capped by that stack's
  reservation (`GS_STACK_RESERVE`, 256 MB by default; spec 10.4 allows up to
  2^48 bytes). Exceeding it aborts at the guard region: there is no recoverable
  allocation failure.
* **The array never shrinks.** Closed documents' runs are reused, but the
  array's high-water length stays committed and is never returned to the OS.
* **A move copies.** Growth that cannot happen in place is O(current length),
  and a document grown in small steps while another follows it can be copied
  at each step. Growing by a factor is the caller's policy.
* **Fragmentation.** Placement is first fit in index order, with neighbors
  merged on release. Enough free bytes in total does not guarantee a free run
  of the needed size; the array then grows. First fit was measured against
  best fit: it was faster everywhere, best fit's array came out at most 12%
  smaller on buffers grown a step at a time, and first fit's came out smaller
  on mixed sizes.
* **Views do not follow a move.** Store positions as offsets into the current
  run so they remain usable after it moves, as described above.
* **Element types.** Elements are fixed-size, and `realloc_slice` rejects
  element types holding self-relative references, whose offsets a copy would
  leave measuring from the old place (spec 3.9).
* **Per-operation cost.** Placement scans the free spans in order; adding or
  removing a span shifts the spans above it (docs/implementation.md 9.7). A
  workload with thousands of simultaneously free runs pays for both. A slice
  handed back that the checker cannot place in the pool also pays a range test.

## The other Goose representations

The placement rules are unchanged: an array still cannot contain resizable
elements (spec 3.4), so a growable array of growable documents remains
inexpressible, and cycle functions still cannot keep nonfixed locals across
a call into their cycle (spec 7.8). Slice pools are the in-language
substitute. Several earlier workarounds are superseded by them: append-only
byte storage with per-document offsets (retention followed edit history
until a whole-region reset), one big
grow-shrink buffer with application-managed shifting (the pool now does the
placement and moving), runtime-capacity limited arrays `u8[..]` (which could
neither grow nor live in a slot pool), and a table of references to separately
scoped locals (which never owned anything). The following approaches remain useful:

| Approach | Where it still fits |
|---|---|
| Inline limited arrays, `u8[..MAX]`, as fixed-size slots in a `reusable` pool | A real and small bound. Nothing is ever copied or fragmented, and each document is one fixed slot. Every slot pays the full capacity, and exceeding it cannot grow that buffer. |
| Reusable fixed-size chunks linked by indices or relative references, possibly as a rope or piece table | Very large documents, or frequent inserts in the middle, which a contiguous run copies on every move. Chunks never move, so references into them stay current, growth copies at most a chunk, and fragmentation is bounded by the chunk size. It needs chunk addressing, cross-chunk iteration and a stale-slot policy, which a library could hide. |
| A worker owning each document | Document actors wanted anyway. Reclamation happens at worker exit, at the cost of a message protocol, an OS thread per document and copying whatever crosses a queue. |
| Foreign owning storage | Memory the program does not own. Option C below gives it a typed representation. |

## Future option A: bounded independent dynamic stacks

What a slice pool cannot give: element addresses that stay put while a
document grows, growth that never copies, release of a closed document's memory
to the OS, isolation of one document's fragmentation from the others, and more
total storage than one reservation. This option extends the dynamic stacks idea
in spec 11.3 to a runtime-sized collection whose elements each own an
independently growing stack: a resizable array of resizable arrays. This is a
new owning container, not something today's placement rules already permit.
Its descriptor table can grow or reuse slots while the element storage stays in
separately reserved address regions.

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
trade for a bounded number of substantial buffers; many small buffers belong
in a slice pool instead.

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
* **Reference safety:** unlike a slice pool, closing a document releases its
  storage, so it must be forbidden while references or slices into it can still
  be used. The checker needs to track an inner owner's lifetime, not merely the
  outer table's lifetime. One conservative first design permits access through
  scoped borrows of a document handle; borrowing the whole collection is
  simpler but restricts unrelated closes. Precise per-document borrowing is
  further design work. A generation check on a handle prevents stale-handle
  reuse, but does not protect an escaped raw slice.
* **Release:** closing a document decommits its pages and recycles its region
  slot independently of other documents. Keeping the address reservation allows
  the runtime to guarantee reuse of its budget; the reservation itself is
  released when its owning collection or runtime budget ends. No document's live
  data is copied to release another's storage.

The two compose: a stack from this option can back a slice pool of its own,
for example one per class of document, combining stable large buffers with
pooled small ones. The remaining questions include per-stack versus pooled size
budgets, the owning descriptor's transfer rules, and how much per-document
lifetime precision the checker can provide without annotation-heavy APIs. Fixed
size and count limits are part of this option's contract, not an accidental
failure mode to hide. This option is only worth that checker work where
measurements of slice pools show copies, retained memory or a reservation limit
that matters.

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
`resource<ByteBuffer>`. With slice pools in the language, this is no longer the
way to get independently growing Goose buffers; it is the way to hold memory and
objects that belong to someone else, accessed through functions and copies.
Slice pools and option A remain preferable when the program wants direct array
indexing, slices and ordinary Goose element references.

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

**Cleanup remains unresolved.** A binding might optionally supply cleanup to run when a
local resource leaves scope; whether or how that extends to resources inside
aggregates is also open. Which copies would trigger cleanup, what happens to
aliases after an explicit release, how repeated cleanup or double-free is
prevented, and what aggregate copying means are unresolved. The proposal does
not yet promise RAII, destructors or automatic ownership transfer. These
questions must be settled before a cleanup hook could be given reliable
language semantics.

## Extending slice pools

Smaller steps than either option, each addressing one of the costs above:

* **A slice type naming its pool**, as described above, turning the run-time
  check on a slice handed back into a static one.
* **Recoverable allocation:** a form of `alloc_slice` and `realloc_slice` that
  reports a run the reservation cannot hold instead of aborting, leaving the
  original run intact.
* **Returning free memory:** whole pages inside free spans could be decommitted
  and recommitted zeroed on the next touch. Stale views can still read those
  pages, so this is sound only for element types whose all-zero bytes are a
  valid value, such as `u8`; a type holding a non-optional reference has none.
* **Per-pool reservations:** a pool on a stack of its own size, or on a stack
  from option A.
* **Relocating self-relative references:** tracking the region a relative
  reference ranges over (spec TODO 16) would let a run of linked elements move
  whole.

## Evaluating what remains

Run the open/edit/close sequence against slice pools first, now that they
exist. Measure live bytes, the array's high-water and committed bytes, copied
bytes and latency separately, including repeated fragmenting growth and
release, a document that outgrows the reservation, and old views read after a
move, a close and reuse. That establishes where option A's stable, releasable
storage justifies the additional lifetime analysis: its evaluation adds the 257th document, a
document exceeding its size ceiling, and references held across a document
close. For option C, evaluate API buffer transfers and file-mapping access
while keeping the unresolved cleanup policy explicit.
