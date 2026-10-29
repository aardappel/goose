# Root structs: state with independent storage

Status: root structs v1 and the `slot_pool`/`slice_pool` modules are implemented.
`reusable` and `reusable[]` remain available but are deprecated in favor of the
library pools. Owners have independent local/global components, borrowed owner
references, per-component lifetime checks and known-global codegen. Whole-owner
copying, nesting, serialization and automatic worker transfer remain deferred.
The [migration guide](../stdlib.md#migrating-from-reusable) describes the remaining
compatibility differences; the [benchmark report](../../bench/pool_workloads.md)
records current measurements and performance follow-ups.

## Motivation

Structs bundle related state. An ordinary Goose struct is a single contiguous
piece of memory and can contain at most one resizable value, on its tail.
Root structs lift that storage limitation while restricting where the owner
may be declared. Several arrays can grow independently, and fixed fields can
live beside them, all accessed through ordinary field syntax. Libraries are an
important use: users can instantiate complete library state in one declaration
instead of supplying separate arrays and a borrowed context.

Root structs group independently stored values under one owning lifetime. They
do not put several growing arrays into one contiguous byte image, relocate
fields on growth, or introduce a heap allocator. Every simultaneously live
resizable component is still on top of its own data stack.

```goose
root struct ParserState {
    tokens: Token[>..] = [],
    work: i64[>..<] = [],
    position: i64 = 0
}

var parser = ParserState { .. };
parse(parser, input);

fn parse(state: ParserState&, input: u8[:]) {
    // Ordinary field access and UFCS, with independent array storage.
}
```

The illustration assumes a separately declared `Token` type. Root structs are
useful for parser state, parallel arrays, lookup structures with separate
metadata, and pools. They remain worthwhile if reusable pools stay built in.

## Syntax and explicit contract

Use `root struct`, retaining struct fields, type arguments, defaults, namespaces,
and function-based methods. Store the distinction on the existing struct
declaration/instantiation rather than creating a separate family of field-access
AST nodes.

Treat `root` as a contextual modifier immediately before `struct` at a type
declaration. Existing variables, fields, parameters, and functions named `root`
remain legal. Do not reserve the word throughout the language merely to parse
this modifier.

The modifier means that owning instances are storage roots. It does not mean
that the declaration or instance must be at file scope. An explicitly declared
root struct keeps its restrictions even when it has zero or one resizable
field, including in generic specializations. Ordinary structs keep their
current placement and layout rules. Adding a second resizable field to an
ordinary struct remains an error, and the error suggests `root struct`.

If the root modifier were inferred instead, editing a field or specializing a
generic would change a type's placement, copying, and representation contract.
A keyword that replaced `struct` would obscure how much of the existing struct
behavior remains. The name `local` would misdescribe globals, and `stack`
would distinguish little in Goose.

For comparison, Rust infers the restrictions on a dynamically sized struct from
an unsized last field, and such a struct still describes one object. C#
explicitly marks its restricted `ref struct` category. Neither has the same
rules as this proposal.
References: [Rust dynamically sized types](https://doc.rust-lang.org/reference/dynamically-sized-types.html),
[C# ref structs](https://learn.microsoft.com/en-us/dotnet/csharp/language-reference/builtin-types/ref-struct).

## V1 ownership and placement

| Use | V1 rule |
|---|---|
| Local or global `var`/`let` | Allowed; construct directly at its declaration using a literal and field defaults. |
| Fields | Ordinary fields and multiple independently stored resizable fields, in any order. Their own existing element, construction, and lifetime restrictions still apply. |
| Root owner inside another struct | Forbidden by value, including inside another root struct. |
| Root owner as an array/slice element or ADT payload | Forbidden, including fixed-length arrays. |
| Reference to an owner | Allowed, including optional references and references stored in fields/arrays when ordinary lifetime rules permit. |
| Function argument | By reference. Untyped parameters bind owning lvalues by reference as for nonfixed values. |
| Function result | References to existing owners may be returned under normal lifetime rules; owning results are deferred. Never return a reference to a local owner. |
| Unannotated binding from an existing owner | Binds by reference, consistent with nonfixed lvalues. Explicit `.=` remains available. |
| `copy`, whole-owner assignment, by-value parameters | Forbidden. No implicit descriptor copying or ownership transfer. |
| Temporary owners | Forbidden; owning construction requires a named local/global declaration. |
| Field assignment and array operations | Use the field's usual rules, including shrink and construction checks. |
| Whole-owner raw serialization or C ABI | Unsupported; explicitly supported component operations remain available. |
| Transfer into a worker | Unsupported, including implicit capture of a root global. See the threading rules below. |

The prohibition on ownership nesting does not prohibit ordinary fields whose
types are existing resizable-tailed structs. Such a field is an independently
stored component with its existing internal layout. A root struct itself is
never such a by-value component in v1. Variable-size ordinary fields likewise
retain their construction rules; the enclosing root has no contiguous image.

```goose
struct Context { parser: ParserState& }   // Borrowing is allowed.
struct Object { parser: ParserState }     // Owning containment is an error.
```

Diagnose the prohibited use with the declaration and field/element/argument
path, and suggest a reference or a local/global owner. For example:

```text
ParserState is a root struct and cannot be stored by value in Object.parser.
Declare it as a local/global variable, or store ParserState&.
```

Enforce the rules through aliases, generic instantiation, inferred types,
defaults, inferred results, and unreachable/standalone checking as applicable.
An apparently fixed-size control record must never escape the owning rules.
`const` and writability follow existing struct and reference rules, including
their existing treatment of reference-valued fields; add no blanket transitive
immutability rule.

## Storage and construction

An instance has a stable control record for ordinary fields and component
metadata. Each resizable component has its own stable element region, length
header, and access to its data stack. Use existing representations for ordinary
components where possible. The control record is an implementation detail, not
a fixed-size Goose value that can be copied or serialized.

The number of component stacks is statically determined by the instantiated
type and owning declarations. Lifetime-disjoint owners can reuse stack indices
as existing locals do. Globals use dedicated component storage. Restore all
owned stack watermarks on scope exit, including `break`, `continue`, ordinary
returns, and `return from`. Partial construction must also restore acquired
storage without publishing an incomplete owner.

Evaluate field initializers once, in declaration order. Preserve checks against
observing under-construction storage. A completed owner can subsequently be
initialized further by functions taking its reference. Do not introduce
destructors, automatic resource cleanup, runtime owner counts, or moves.

A root owner holding data stacks is subject to the existing recursive-cycle
restrictions even though its metadata has constant size. It cannot be live
across a recursive-cycle call that would require another set of stacks for each
activation. Passing an owner declared outside the cycle by reference remains
the intended pattern. Capturing such a reference in a nested function or a
compile-time function value follows existing capture and escape rules.

## Component identity and lifetime analysis

An enclosing variable's lifetime is not sufficient to identify its storage.
Represent independently stored components with identities equivalent to an
owning declaration plus a field path:

```text
(state, .tokens)
(state, .work)
```

These share an enclosing lifetime but are different regions. Preserve this
distinction through field projection, direct and optional owner references,
branches, rebinding, calls, returned references, globals, captures, and stored
references. An unknown reference carries a conservative set of possible
regions; never promote a lifetime bound to an exact component identity.

Implementation: a synthetic variable represents each component. Class roots
can carry the same projection without claiming a concrete owner. A separate
`projected` proof records that a view stays inside that field's region, allowing
the checker to exclude sibling fields even after an owner reference was loaded
from a holder. Ordinary references read out of the field lose this proof;
self-relative links retain it. Joins intersect the proof, weakening drops it,
and return/effect summaries preserve it. This is distinct from exact identity,
which operations such as `index_of` still require.

In particular:

* Growing one field does not interfere with construction in another field.
* Shrinking one field does not invalidate references into another field,
  including when their element types are identical.
* Shrinking a field still rejects any live view or holder that can refer into
  that field, across calls and reference aliases.
* `index_of` must establish the particular array, not merely the owner. Two
  arrays of the same element type in one root remain distinguishable.
* Whole-array references name their headers; element references name the
  element region. A shrink must preserve the existing distinction.
* Self-relative links remain within their permitted contiguous region. A
  common enclosing owner does not permit links between separate stacks or from
  its control record into an array. Use ordinary references for those links.
* Read-back, holder/store records, effect summaries, construction conflicts,
  and recursive specialization must propagate component identities consistently.

Relevant code includes `RootAlt`/`Roots` in `src/ast.h`,
`RootedAtReceiver`/`OneArrayOf` in `src/typecheck_types.h`, and
`SoleResizableIn`, growth logs and shrink summaries in
`src/typecheck_builtins.h`. Root-set joins merge entries by variable identity,
with separate synthetic identities for independent components; domain tests
cover the additional proof's join/equality laws. Existing packed structs still
share one region: a field path is not automatically a new region everywhere.

During investigation, small typecheck probes against the available compiler
showed that a scalar allocator with separate data and freelist arguments accepts
a retained reference across reuse. When those arguments were grouped in a
struct of references instead, the checker conservatively assumed that the
result could point into the freelist and rejected the next allocation. This is
evidence of a current precision limit, not a test of root structs. Include this
pattern, and two same-typed owned arrays, in the implementation's acceptance
cases.

## Code generation and performance

Array access must remain efficient. Grouping arrays in a root struct must not
disable existing array optimizations or add an unexplained load on every
iteration of a hot loop.

The current backend already has relevant machinery:

* `ParamFields` in `src/codegen_frames.h` passes slices, fat references and pool
  references as separate C parameters. This avoids platform ABI costs and
  alias-related reloads of aggregate argument copies.
* Stack tops are cached in C locals, with synchronization around calls and exits
  (`RefTopsOk`, `HandedStacks`, `SyncReach`, `Top`/`TopW`).
* BCE's `hoistrefs` and `hoistfields` let codegen cache array bases and lengths
  when a loop cannot invalidate or rebind them.
* Existing frame objects keep fixed prefixes and a resizable tail's header
  outside the element region.

A stable control-record reference is a reasonable v1 representation, but field
projections must expose their normal header/stack information to those passes.
Track every stack a root argument or captured root can reach. Flush and reload
cached metadata when a call may mutate it, without assuming two parameters are
distinct owners. Rebinding, globals, callbacks and nonlocal returns require the
same care as existing references. Preserve observable left-to-right sampling.

Inspect generated C as well as timings. Compare direct array arguments, an
ordinary one-tail struct, a root with two arrays, a borrowed context, and the
existing reusable pool. Cover reads, growth, freelist churn, same-typed fields,
out-of-line calls, aliasing parameters, callbacks, and branches selecting owners.
Check that base/length reads are hoisted where valid and that unrelated component
mutations do not force unnecessary reloads or lose bounds-check elimination.
Exercise O0/O2 and TinyCC/native C; include Windows x64 and other CI targets for
ABI-sensitive changes. Record reservation count and code size as well as runtime.

Do not promise zero overhead based only on an inlined benchmark. Keep existing
`reusable` until a migration has demonstrated acceptable behavior and performance.
For initial root support, report material regressions explicitly and fix basic
integration with existing optimizations before calling the feature complete.

Two call lowerings improve on passing the descriptor's address:

1. **Known-owner specialization.** Where all incoming calls prove exactly
   one global owner, accesses use that owner's fields and component caches.
   See [the implemented scope](#known-root-identity-across-function-calls).
   Preserving local-owner identity across emitted call boundaries remains future work.
   A unique type is not proof of a unique live instance: repeated declarations,
   recursion, worker instances, aliases and reentrancy must be considered.
2. **Field-by-field lowering.** Pass the headers, stack handles and component
   addresses a descriptor holds as separate arguments, which is the pool
   calling convention. Writes still go through those addresses, so owner
   identity is preserved. A parameter whose reference is stored, returned or
   captured keeps the descriptor. See
   [field-by-field root parameters](#field-by-field-root-parameters).

Keep the v1 language contract independent of these ABI strategies so that
further optimizations do not require user code changes.

### Initial native measurement

`python bench/root_structs.py --exe <goose.exe> --n 32000000 --reps 9`
compares `push_ref.goose` with `push_root.goose`: the same RNG, item writes,
retained references, checksums, and two owner instances. The harness marks both
C `fill` helpers `GS_NOINLINE`, preserving the call boundary at Goose O0 and O2
while compiling the C with native optimization. It saves timings, generated C,
executable sizes, and stack reports. Times include process startup.

Windows x64, 2026-10-16, median of nine runs after one warmup:

| Native compiler | Goose level | Separate array arguments | Root argument |
|---|---|---:|---:|
| MSVC 19.51 | O0 | 88.14 ms | 88.88 ms |
| clang-cl 22.1 | O0 | 85.57 ms | 85.64 ms |
| MSVC 19.51 | O2 | 86.78 ms | 87.18 ms |
| clang-cl 22.1 | O2 | 86.48 ms | 85.37 ms |

Both use five data-stack regions (including rendering) and produce identical
checksums and executable sizes per compiler: 148,480 bytes with MSVC and 155,648
with clang-cl. The root version adds 536 bytes of generated standalone C. This
probe shows no material regression for this workload; it is not a claim about
all containers or platforms. Freelist-policy comparisons remain part of the
separate reusable migration experiment.

`root_owner_caches.goose` and the baseline runner also check generated C: top
and length caches for both independent fields, base/length hoisting for indexed
reads, and runtime correctness when owner/owner, owner/array, or owner/global
arguments alias. Such aliases intentionally keep the shared memory top.

## Serialization, workers, and other boundaries

V1 has no root-level `to_bytes`, `bytes_of`, `from_bytes`, raw equality/copying,
or public foreign value layout. A root owner has no single contiguous image,
even when every component is individually flat. Do not accidentally accept it
through existing flatness or fixed-size predicates.

Serializing or viewing an eligible component, such as `pool.items`, remains an
ordinary operation under the existing image-safety and lifetime rules. Such an
image contains the items, not the root's other fields or freelist. Loading it
into an existing component obeys ordinary assignment/shrink rules. Libraries
that need a coordinated snapshot can define an explicit data format later.

There are existing reusable-to-worker cases:
`test/threads/thread_globals.goose` copies both a reusable slot pool and a
reusable slice pool from the spawning instance, preserving freelist state, and
tests a worker spawning another worker. Current codegen explicitly copies the
freelist beside the data. Preserve that behavior for existing `reusable`.

Root v1 rejects passing an owning root to `thread_spawn` or a queue, and
sending a reference to an owning root. It also rejects any direct or
transitive use of a root global by a worker, since that use would require
implicitly copying the owner. Diagnose the reachable global/call path. A root
local constructed within a worker needs no transfer and is allowed. An
unrelated root global unused by the worker must not reject the program.
Existing rules still govern passing eligible components as copied data; never
transfer descriptor pointers or infer transferability from flat contents.

Worker snapshot support can follow later by reconstructing independent storage
for every component, copying the relevant metadata, and handling only reference
forms for which transfer is already legal. This is an explicit compatibility
gap to resolve before claiming that stdlib pools replace every reusable use.

Formatting and reflection must distinguish inspecting a reference from copying
or serializing its owner. Retain field access and UFCS; support inspection by
reference where it does not request a forbidden value operation. Do not add
array operator overloading merely to disguise a container's `items` field.

## Separate follow-up: reusable pools in the stdlib

`import slot_pool;` provides a fixed-size slot allocator:

```goose
root struct slot_pool<T> {
    items: T[>..] = [],
    free_slots: i64[>..<] = []
}

var pool = slot_pool<i64> { .. };
let r .= pool.alloc_ref(42);
pool.free(pool.items.index_of(r));
```

The implementation uses the same LIFO free-index policy as scalar `reusable`,
with allocation, reference-returning allocation and checked free. Tests compare
allocation traces, retained references and slot contents against the builtin.
The [API reference](../stdlib.md#slot_pool) describes the supported element types
and current boundaries; the [benchmark report](../../bench/slot_pool.md) compares
equivalent native workloads. The existing reusable implementation remains available unchanged.

`stdlib/slice_pool.goose` packages its `items`, node array and tree state in one
root owner. Its three tree operations are externs (`stdlib/slice_pool_internal.goose`)
that hand `nodes` and `tree` to a copy of the builtin's runtime span tree:
first-fit placement, coalescing and tail reuse, with overlap-safe relocation in
Goose. The copy checks what it reads from those fields, which the program can
write, and aborts on a node number or count that does not fit. Differential tests
compare positions, retained views and every backing element under allocation,
free and resize traces. See its [API](../stdlib.md#slice_pool) and
[benchmark report](../../bench/slice_pool.md). Both pool types require fixed-size
elements. Worker snapshots remain a compatibility gap; global component paths
and destination-aware slot construction are implemented.

Preserve the existing safety contract: all published elements remain valid
typed values, a free merely marks storage reusable, and a stale reference may
observe a replacement value. Double frees are logical errors unless a library
adds detection. Freeing does not promise decommit, reservation release, or
destruction. Slice relocation does not retarget existing views; fragmentation,
high-water storage and copying costs remain.

Expose `items` explicitly for indexing, slicing and ordinary array algorithms.
Its length is the high-water length, not a live-slot count. Live-only iteration
requires additional metadata or work. Coordinated reset must update both data
and metadata and satisfy the data array's normal shrink rules. Goose currently
lacks field privacy: internal-field conventions and checked indexing preserve
memory safety, but arbitrary user edits may violate pool invariants. Privacy is
a separate possible improvement, not a prerequisite for root structs.

The migration requires the following capabilities; completed ones are noted:

### Global component paths

Implemented for mutable direct array fields of global `var` root owners:
`Node&<u32 in nodes.items>`, `u32 in nodes.items`, and `u32 upto nodes.items`.
Owner names may be namespace qualified. The target must be a grow-only array;
aliases, reference crossings, indices and nested field paths are rejected.
`in nodes` still means exactly that global, with no implicit `.items` selection.

The component identity is reserved before types are instantiated, so recursive
declarations such as `Node` linking into `slot_pool<Node>.items` keep the same
identity throughout checking. The owner's field resolves to that existing
component when its type becomes known. Owner-reference specializations retain
named component identity through helper calls. Width checks, exact-root rules,
index/bound type identity and the index array's no-shrink rule remain in force.
Bounds analysis identifies a named component with the corresponding field
expression; codegen uses its existing global header and cached base directly.

Types can use array bases and lengths without an identifier expression. These
dependencies participate in call-cache synchronization and worker checks. An
implicit dependency on a root component is rejected in workers, just like an
explicit use of its owner. Image verification flushes cached counts before
checking serialized indices against global array lengths.

Validation includes namespace/alias round-trips, cyclic node/pool declarations,
global field initializers, owner-reference forwarding, compact-width overflow,
index bounds and image validation, cross-field/owner rejections, no-shrink
checks through owner and array parameters, and implicit worker dependencies.
The full native baseline passes at O0/O2 with TinyCC and runtime-debug coverage;
24 focused fixtures also pass with compiler assertions enabled. Optional native
modules and sanitizer configurations were not run for this extension.

### Destination-aware library construction

`alloc_with<T, F>(pool: slot_pool<T>&) -> T&` is implemented using ordinary
library control flow: remove a free index and assign `F()` into that slot,
or return `pool.items.push(F())`. It calls the zero-argument builder once,
after selecting the destination, and accepts fresh fixed-size aggregates
with self-relative links or pool-relative `self`. Use `items.index_of` on
the returned reference when an index is needed.

The general compiler prerequisites landed on master separately; see
[result construction](result_construction.md). Fresh results are distinct
from copies, receiving roots flow through calls independently of argument
slots, and fixed relative aggregates construct through destination pointers
at every optimization level. Named functions, trailing blocks, forwarding,
recursion and nonlocal returns share these rules. A block still infers its
own result type; an explicit reference-result function can supply a
reference-valued element. `alloc_ref(value)` and `alloc_index(value)` retain
their ordinary argument evaluation and copy requirements.

No default element or uninitialized published slot is needed. Appending
publishes the new length only after construction finishes. A nonlocal exit
leaves that length unchanged. Reuse removes the free index before running
the builder and does not roll back that removal or initializer side effects
on an early exit. A slot freed by the builder stays available for the next
allocation. Invalid recycled indices fail bounds checks before the builder.
Existing construction, lifetime, root and copy checks remain in force.

The builder fixtures cover fresh and recycled slots, neighbour links,
`self` in both relative forms, forwarding, explicit reference results,
non-defaultable elements, nonlocal exits, and freelist side effects. The
publication fixture mirrors `test/storage/alloc_half_built.goose` and
produces the same output. Rejection fixtures cover copies, wrong roots on
cached calls, short-lived local targets and conflicting item growth.

### Known root identity across function calls

Codegen preserves a root parameter's identity when every incoming call names
the same global owner. This is independent of `in pool.items` types: ordinary
root structs and pools of ordinary values qualify too. After Goose optimization,
an analysis follows direct global references, immutable uncaptured aliases and
parameter forwarding. Recursive edges participate in the same fixed point;
no body is emitted until all its callers agree. Conflicting or unknown inputs
retain the generic descriptor path. The analysis neither changes typechecking
nor creates a specialization per owner.

In a qualifying helper, the parameter aliases the global's existing component
locations. Multiple parameters naming that owner share its top/length caches,
including with direct global accesses in the body. The global effects propagate
through callers so nested calls and builder callbacks flush/reload the relevant
caches. A separately supplied array reference can alias an owner component and
still takes the conservative uncached path. Return-destination caching also
continues to account for the helper's access to global storage.

The call interface and argument evaluation stay intact. All index, lifetime,
construction and shrink checks remain in force. In particular, knowing the
owner does not make the contents of its public freelist trustworthy. No
singleton assumption about a root type is needed: several instances are valid,
and a helper shared by different owners retains its descriptors.

This extension specializes accesses to proven **global** owners. Local owners
still benefit from existing Goose inlining/alias projection, and from the
field-by-field interface below; preserving their identity across an emitted
function boundary would need additional specialization. Mutable, captured,
stored, optional and otherwise opaque root references are conservative, as are
implicit formatting and tag-dispatch entry points. The restriction on Goose
inlining into recursive functions remains unchanged.

### Field-by-field root parameters

A root reference parameter that its function only projects, aliases or
forwards to another such parameter is passed as its descriptor's members: each
resizable component's header and stack, as a fat reference parameter is
(`ParamFields`), and each other component's address. The body rebuilds the
descriptor in a local that its projections read. An owner's descriptor never
changes once the owner is made, so the copy stands for it while the call
runs. A parameter that its function returns, stores, captures, hands to a
builtin or to an entry point with a fixed interface (exports, workers,
formatting and tag dispatch), or forwards to a parameter that keeps its
descriptor, keeps the descriptor's address (`FindSplitRootParams` in
`src/codegen_frames.h`). A parameter proven to name one global owner keeps the
one pointer, which its body does not read. `test/codegen/root_split_params.goose`
covers both kinds.

Against the descriptor-pointer interface, on the slot and slice pool
microbenchmarks at Goose O1/O2 (October 2026), this is a few percent faster on
`slot_pool` under TinyCC and within run-to-run noise elsewhere. The remaining
distance to `reusable` was not the calling interface. Under TinyCC, the slot
pool paid for a pop's check right after its own length test, for temporaries
and for struct copies, which general codegen changes have since removed
([slot_pool.md](../../bench/slot_pool.md)); the index into `items` from the
public freelist keeps its check. The slice pool's span
tree is checked Goose code, where `reusable[]` calls the runtime's C
(`gs_spans_*`). The JIT compiles that runtime C natively, so the gap there is
about 2.5x on the fragmented and resize workloads.

Validation covers recursive forwarding, a recursive helper used by multiple
globals, independent local pools, aliasing owner parameters, separately passed
components, nested-call cache synchronization, self-relative builders, freelist
mutation inside a builder and corrupt indices failing before builder effects.
The full Windows baseline passes with native O0/O2, TinyCC, runtime-debug,
compiler assertions in focused fixtures, bootstrap and C-shape checks. See the
[application measurements](../../bench/pool_workloads.md) and
[microbenchmarks](../../bench/slot_pool.md) for performance and its limits.

### Checked slice position

Builtin `free_slice`/`realloc_slice` can check alignment and complete containment
at runtime when provenance cannot prove their input slice belongs to the pool.
`index_of` currently requires stronger static provenance. Expose a general
checked slice-to-array position operation, now implemented as `range_index`:
elide checks when proven, otherwise check alignment and the full range at runtime.
Every empty slice, including a foreign or default slice, returns zero. Nonempty
foreign storage aborts. No library pointer arithmetic or unchecked index
fabrication is needed.

Preserve all-build checks for invalid frees, negative/overflowing allocation
lengths and element bounds, with validation before metadata changes where the
current contract requires it. Slice allocations initialize every added element;
relocating slices retain the restriction on self-relative element types. Expose
any needed length validation as a general array capability rather than copying
runtime constants into the library. This is implemented as `check_length`, which
validates a proposed total element count without changing storage or promising
that a later allocation will succeed. `slice_pool` validates requested counts and
input slices before changing tree metadata. Allocation and free support
defaultable self-relative elements; resize still requires copying, as it does
for the builtin.

### Policy and compatibility

Library variants may add occupancy checks, generations, different span policies,
or logical freelist counts over grow-only metadata arrays. Generational handles
are an optional API and cannot protect escaped raw references. See
[slotmap's versioned keys](https://docs.rs/slotmap/latest/slotmap/) for a useful
comparison, not a replacement for Goose's existing reuse semantics.

Deprecate `reusable` and `reusable[]` in documentation and recommend the library
types for new code. Keep the syntax, diagnostics and runtime behavior available
for existing code. Global component index/relative types, destination-aware slot
construction and worker-local library pools are implemented. Removal is a later
change, after worker transfer has a reviewed migration path. Continue
measuring application workloads as well as allocation-heavy microbenchmarks;
small regressions alone do not require retaining a language-level allocator.

## V2 candidates

* **Nested root owners.** Permit a root field inside another root, recursively
  flattening its finite region requirements. Define initialization, projection,
  lifetime identities and diagnostics before relaxing placement. Ordinary
  packed structs still cannot silently acquire this layout.
* **Small constant arrays of root owners.** Explore `slot_pool<T>[N]` for a
  compile-time constant N. The stack requirement is statically multiplied by N;
  runtime indexing selects an owner descriptor/region set. Define permitted
  enclosing positions, whole-array references, dynamic-index alias joins,
  initialization, iteration, N=0, and practical reservation limits. This does
  not imply resizable or runtime-length arrays of owners. Do not choose an
  arbitrary small-N language limit before evaluating costs.
* **Local-owner specialization.** Extend the implemented global-owner
  optimization while preserving addressability and alias semantics and
  minimizing indirections. Field-by-field call lowering is implemented (see
  [field-by-field root parameters](#field-by-field-root-parameters)).
* **Owning factory results.** Construct directly into several caller-provided
  destinations, without copying a descriptor and presenting it as a move.
  General copying, reassignment and moving need separate semantics.
* **Worker snapshots and coordinated serialization.** Reconstruct independent
  components with an explicit format/lifecycle contract; never memcpy an owner.

## Implementation sequence and acceptance

1. **Frontend foundation.** Parse and round-trip the contextual modifier; carry
   it on struct metadata through generic instantiation and AST cloning. Add
   syntax coverage and precise errors. During incremental development, reject
   semantic uses explicitly until their storage support exists: do not allow a
   root to fall through ordinary fixed/resizable codegen.
2. **Placement, storage classification and provenance.** Introduce an explicit
   owning-layout distinction rather than treating the fixed metadata as a fixed
   Goose value. Audit size/flatness/copy/containment queries. Implement component
   identities and their join, specialization and effect-summary behavior.
3. **Ownership and codegen.** Allocate and restore all component stacks; support
   literals/defaults, locals/globals, references, field projection, aliases,
   captures and calls. Integrate top caches, call reachability and view hoisting.
   Reject unsupported transfers, copies, nested owners and temporaries before
   codegen. Update spec, reference, implementation notes and editor support when
   the feature is usable, without advertising scaffolding as working support.
4. **Library-state acceptance.** Demonstrate one-declaration multi-array state
   used by generic functions, with references retained across unrelated field
   operations. Test actual runtime behavior and generated code, not just parsing.
   This milestone does not depend on a reusable replacement.
5. **Pool migration experiment.** Port copyable-element slot allocation, compare
   code and timings, then address the separate construction/path/slice features
   above and evaluate slice pools and existing worker behavior.

Required focused tests include:

* independent same-typed fields;
* shrink rejection for the correct component and acceptance for unrelated
  components;
* stored, optional, returned and branch-selected owner references;
* const/writability;
* generic placement failures;
* recursion and nonlocal exits;
* partial initialization;
* component serialization;
* forbidden root serialization/FFI/thread use;
* allowed worker-local owners;
* transitive worker-global rejection.

Exercise aliases that refer to the same owner as well as distinct owners.

Run the appropriate focused tests during each implementation slice, then the
repository's baseline compiler/runtime suite for the complete feature. Extend
`test/compiler_roots.cpp` when changing root-set laws. Keep performance probes
repeatable and compare equivalent algorithms; do not conflate a different
freelist policy with root-reference overhead.

### V1 validation record

After rebasing onto master `cce94b91`, the Windows x64 baseline passed on
2026-10-27 using an assertion-enabled Debug compiler, MSVC and the hosted TinyCC
JIT: 1,406 parse/round-trip/typecheck checks, 671 native program checks, 191
runtime-debug checks and 826 JIT runs. The compiler root-domain tests,
generated-C cache/hoisting assertions, bootstrap, formatter, language server,
error fixtures, samples and runnable reference examples also passed.

Root alias, recursive capture, pool equivalence and allocator model fixtures
also run at O1. Root/defer integration runs at O0/O1/O2 and with runtime checks,
covering holder aliases, both pool types, early returns, loop exits, recursive
unwinding and nonlocal returns. Formatter coverage includes root declarations
and named component paths. All 40 VS Code extension tests passed with the
rebased compiler, with no skips.

The baseline includes native worker-local ownership and transitive worker-global
rejection; TinyCC does not run worker programs. Optional audio/gfx/physics/UI
modules were not built. Linux/macOS and sanitizer CI were not run locally.
The benchmark reports retain their dated 2026-10-25 measurements; this validation
run did not remeasure performance after the rebase.

Implementation steps 1–4 are complete. Step 5 has both pool implementations,
differential tests and repeatable native/TinyCC/JIT benchmarks. Current application
results and unresolved performance work are in the
[pool workload report](../../bench/pool_workloads.md). Explicit flat worker snapshots
are demonstrated; automatic transfer remains a v2 candidate.
