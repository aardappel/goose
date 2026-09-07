# Serialization of relative-reference structures: a verified loader

*Implemented. What the language promises, what a loader has to check, and
the shape of the implementation that keeps the safety guarantees of §9.*

## 1. The promise

A structure whose links are all self-relative references (§3.9) is
position-independent: a tree of `Node { v: i32, l: Node&<u32>?, r:
Node&<u32>? }` in a `Node[>..]`, or a `Sexp..[>..]` pool, is a run of
bytes that means the same wherever it sits. `bench/design.md` counts "Goose
data is already its own serialized form" among the design's advantages:
saving is a write of the element region, loading is a read of it, and there
is no pointer fixup pass because there are no pointers.

Two builtins (§12) express the two halves. `samples/25_serialize.goose` is
the worked example; `test/serialize.goose` is the coverage.

## 2. Why a trusting loader is not acceptable

`from_bytes<T[>..]>(bytes)` that reinterpreted the bytes would be the first
operation in the language that can produce a reference the checker did not
prove valid: a corrupted or malicious offset is an address anywhere in memory,
which breaks the first priority of §9.1 outright. Every other unsafety Goose
admits is type-safe reuse (§9.4); this one is not. So the loader has to
verify, the way a FlatBuffers verifier does, before the bytes are a value.

What is verified is *safety*, not integrity. An image whose data bytes were
edited still describes a well-formed structure and is accepted with the
wrong numbers in it; a checksum, not the verifier, is what says the file is
the one that was written.

## 3. What must be verified

For a candidate image of `T[>..]`:

1. **Framing.** The image parses as a sequence of `T` values: for fixed `T`
   the byte length is a multiple of `sizeof(T)`; for variable `T` a
   sequential walk of the elements (the same walk `print` and `==` use) ends
   exactly at the image's end, every varint terminates, every tag is in
   range, every nested length fits what is left.
2. **Element starts.** The walk yields the set of element start offsets, and
   for variable-mode ADTs the tag at each start.
3. **Links.** For every self-relative reference field at absolute offset `f`
   with stored offset `o`: `o == 0` is null and legal only where the field is
   optional; otherwise `f + o` is an element start. A reference typed at a
   *variant* addresses that variant's payload, so what must be an element
   start is `f + o − sizeof(tag)`, and the tag there must be that variant's.
   A reference whose pointee type is neither the element type nor a variant
   of it -- an `i64&<u16>` into a field of an element -- would need every
   valid address of that type enumerated; v1 rejects such element types for
   `from_bytes`, at the call, by name.
4. **Excluded types.** Plain references and slices (they are addresses) and
   `in pool` references (they mean an offset into a *named* global pool, not
   into the image) are rejected for both builtins. Resizable fields cannot be
   array elements in the first place.

Everything §3.9 already guarantees about a self-relative structure built by
the program -- both ends in one root array -- is exactly what step 3 checks
for one arriving from outside. A verified image is then indistinguishable
from a constructed one, and the ordinary rules apply to it from then on: the
result is rooted at its own variable, it grows, and relative stores into it
are checked like any other.

A `reusable` pool (§5.4) writes and loads like any other array. Its freelist
is separate state that no image carries, so a loaded pool starts with an
empty one and every slot live -- which is what §5.4 already says every slot
always is.

## 4. The builtins

```goose
fn to_bytes(a) -> u8[>..]                            // any array kind, or a slice
fn from_bytes<T[>..]>(bytes: u8[:]) -> T[>..], bool  // a verified copy, or [] and false
```

`to_bytes` is safe for any array or slice whose element type contains no
plain references, slices or `in pool` references (a static check); it copies
the **element region only** -- no length prefix, whatever the receiver's
array kind -- into a fresh `u8[>..]` built at the destination like any other
result (§7.3). Copying rather than viewing keeps the image independent of the
array's later growth. Spelled either way per UFCS: `a.to_bytes()` or
`to_bytes(a[1..])`.

`from_bytes` runs the verifier over `bytes`, then copies them into the
result's element region; the count the verifier returns becomes the result's
length. Its result is a fresh array rooted at its own variable, exactly as if
the program had pushed the elements itself, so it needs no new rule in §9.
The `bool` is the verifier's verdict; on failure the array is empty. Both are
ordinary builtins with custom typechecking (like `default<T>()`): `to_bytes`
rejects the excluded element types at the call, `from_bytes` needs its
explicit type argument (§7.7).

The pair round-trips: `from_bytes<T[>..]>(a.to_bytes())` is `a`, for every
element type `from_bytes` accepts.

## 5. Generating the verifier per type

The verifier is generated per element type, like the equality and size
functions codegen already emits (`gs_size_T`, `EmitEqWalk`):
`gs_verify_T(const uint8_t *p, int64_t n, uint8_t *bm) -> int64_t` returns
the element count, or −1. It is `EmitVerifyWalk` in `codegen_types.h`, a case
per type kind driven by the layout (`Layout`, C.2), recursing into struct
fields and variant payloads, with a check per relative-reference field --
about 250 lines of codegen rather than a new subsystem.

* **Fixed elements.** Element starts are implicit, so one walk does
  everything: it range-checks tags, bounds limited-array lengths, and
  validates a link with `0 <= f + o < n && (f + o) % sizeof(T) == 0`. No
  scratch at all, and `bm` is passed as null.
* **Variable elements.** Two walks of the same shape. The first frames the
  image and sets a bit per element start in `bm`, a bitmap of one bit per
  image byte on a scratch data stack; the second re-walks and asks the bitmap
  about each link. The scratch is `(n + 7) / 8` bytes on a statement-scoped
  stack, so it is gone at the end of the statement.
* **Bounds.** Every read is bounded before it happens, and the varint decoders
  are the bounded `gs_uleb_check` / `gs_zig_check` (runtime.h) rather than the
  ordinary ones, which loop until a terminator the image may not contain.
* **Element types with no relative reference at all** skip the link pass
  entirely; **types the walk has nothing to say about** (fixed, no tag to
  range-check, no limited-array length to bound, no link) are skipped over as
  opaque bytes rather than walked field by field.
* Cost: one or two linear passes over the image, no allocation for fixed
  elements, one bitmap for variable ones. Verification is O(n) and
  branch-light -- the same order as the copy that follows it, which is the
  FlatBuffers experience as well. `samples/25_serialize.goose` reports the
  measured time for its own index on stderr.

The limited-array case is the one worth calling out: slots past the stored
length were never written (§5.3), so the walk checks the links in the live
slots only, and then skips the cursor over the whole capacity.

## 6. Open points

* **Queues** (TODO 9): the same verifier lets a self-contained relative
  structure cross a thread queue, since the receiving side can verify the
  image it unpacks; that would relax the flat-only rule for exactly these
  types. Not wired up: `qput`/`qget` still take flat values only.
* **References into payload fields** (`i64&<u16>` at a field of an element)
  are rejected for `from_bytes`; an element-start-plus-field-offset
  enumeration would admit them.
* **`from_bytes` into other array kinds.** Only `T[>..]` today. `T[>..<]`
  would be a one-line relaxation; a `T[]`/`T[varint]` form would have to
  decide whether the image carries its own length prefix, which is a format
  choice, not a verifier one.
* **A view instead of a copy** for `to_bytes` (`u8[:]` of the element region)
  is cheaper for a save, but the slice would be rooted at the array and stay
  valid only while it lives, which the ordinary slice rules already express;
  it can be added as `bytes_of(a) -> u8[:]` without touching the verifier.
* **The discarded verdict.** `var a = from_bytes<T>(b);` is legal and drops
  the bool, leaving an empty array behind on failure -- safe, but silent.
  Whether the second result should be mandatory is a language-wide question
  about trailing-bool returns (§4 of `samples/04_errors.goose`), not one
  about this builtin.
* **Element types whose values can occupy zero bytes** (a struct of nothing)
  have no recoverable element count; their verifier accepts the empty image
  and nothing else.
* **Cross-endian and cross-width images** are not portable: the layouts of
  C.2 are little-endian and the widths are what the declaration says, so an
  image moves between machines that agree on both. Nothing checks that a
  file came from the same program, either -- the type is not in the image.
