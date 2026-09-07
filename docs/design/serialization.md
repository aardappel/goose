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

Three builtins (§12) express that. `samples/25_serialize.goose` is the
worked example; `test/serialize.goose` is the coverage.

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
the one that was written. Nor does anything say the file came from *this*
program: the type is not in the image, which is a container format's job and
not the language's.

## 3. The image

```
uleb128(payload byte count)  payload = the array's element region
```

The prefix is what lets a reader of a stream or a long file size its read
before it has the whole image; it is the ULEB128 of §3.6, and `format_uleb`
/ `parse_uleb` in `stdlib/std.goose` are the same encoding for a program
that wants to write or read it itself. `from_bytes` requires the prefix to
account for *exactly* the rest of the slice, so truncation and extension are
both caught before the walk starts.

The payload is little-endian by construction: the layouts of C.2 are, and
every target this compiles for is. That is now a stated requirement rather
than an accident — `to_bytes`, `bytes_of` and `from_bytes` each test the
host and abort (`GS_E_ENDIAN`) on a big-endian one, which is a folded
constant on every real target. A big-endian port would have to either prove
an image never leaves the program or emit byte-swapping variants.

## 4. What must be verified

For a candidate payload of `T[...]`:

1. **Framing.** The payload parses as a sequence of `T` values: for fixed `T`
   the byte length is a multiple of `sizeof(T)`; for variable `T` a
   sequential walk of the elements (the same walk `print` and `==` use) ends
   exactly at the payload's end, every varint terminates, every tag is in
   range, every nested length fits what is left.
2. **Element starts.** The walk yields the set of element start offsets, and
   for variable-mode ADTs the tag at each start.
3. **Links.** For every self-relative reference field at absolute offset `f`
   with stored offset `o`: `o == 0` is null and legal only where the field is
   optional; otherwise `f + o` is an element start. A reference typed at a
   *variant* addresses that variant's payload, so what must be an element
   start is `f + o − sizeof(tag)`, and the tag there must be that variant's.
   A reference whose pointee type is neither the element type nor a variant
   of it — an `i64&<u16>` into a field of an element — would need every
   valid address of that type enumerated; v1 rejects such element types for
   `from_bytes`, at the call, by name (see §7).
4. **Excluded types.** Plain references and slices (they are addresses) and
   `in pool` references (they mean an offset into a *named* global pool, not
   into the image) are rejected for all three builtins. Resizable fields
   cannot be array elements in the first place.

Everything §3.9 already guarantees about a self-relative structure built by
the program — both ends in one root array — is exactly what step 3 checks
for one arriving from outside. A verified image is then indistinguishable
from a constructed one, and the ordinary rules apply to it from then on: the
result is rooted at its own variable, it grows, and relative stores into it
are checked like any other.

A `reusable` pool (§5.4) writes and loads like any other array. Its freelist
is separate state that no image carries, so a loaded pool starts with an
empty one and every slot live — which is what §5.4 already says every slot
always is.

## 5. The builtins

```goose
fn to_bytes(a) -> u8[>..]                            // the image, as a fresh value
fn to_bytes(a, out: u8[>..]&)                        // the image, appended to a builder
fn bytes_of(a) -> u8[:]                              // the payload alone, as a view
fn from_bytes<T[>..]>(bytes: u8[:]) -> T[>..], bool  // a verified copy, or [] and false
```

All three are safe for any array or slice whose element type contains no
plain references, slices or `in pool` references (a static check), and are
spelled either way per UFCS: `a.to_bytes()` or `to_bytes(a[1..])`.

`to_bytes` copies. The two-argument form appends to a growable `u8` array the
caller owns — resizable or limited — so a message or file header can go in
front of the image without a second copy, and the one-argument form builds a
fresh `u8[>..]` at its destination like any other result (§7.3).

`bytes_of` copies nothing: it is a `u8[:]` over the element region, rooted at
the array exactly as a slice of it would be, so §5.1 and §5.2 keep the array
from shrinking under it and §9 keeps it from outliving it. It is **never
writable**, whatever the array's own provenance: bytes written through it
would be relative references the checker never proved, which is the one thing
the whole design is protecting. Its length is what the framing prefix would
have said, so a save that writes its own header (`format_uleb`) and then the
payload produces a file `from_bytes` reads back, with no image ever built in
memory.

`from_bytes` checks the prefix, runs the verifier over the payload, then
copies it into the result's element region; the count the verifier returns
becomes the result's length. Its result is a fresh array rooted at its own
variable, exactly as if the program had pushed the elements itself, so it
needs no new rule in §9. It builds any array kind whose contents are an
element run plus a count — `T[>..]`, `T[>..<]`, and the `T[]` family, whose
count goes into the destination's own length prefix. A fixed or limited array
would additionally need the count to match a capacity the image does not
carry, so those are rejected.

The `bool` is the verifier's verdict; on failure the array is empty. It may be
dropped (`var a = from_bytes<T>(b);`), which is deliberate: where corruption
is unlikely and an empty array is a benign outcome, the check is noise.

Both are ordinary builtins with custom typechecking (like `default<T>()`);
`from_bytes` needs its explicit type argument (§7.7).

The pair round-trips: `from_bytes<T[>..]>(a.to_bytes())` is `a`, for every
element type `from_bytes` accepts.

## 6. Generating the verifier per type

The verifier is generated per element type, like the equality and size
functions codegen already emits (`gs_size_T`, `EmitEqWalk`):
`gs_verify_T(const uint8_t *p, int64_t n, uint8_t *bm) -> int64_t` returns
the element count, or −1. It is `EmitVerifyWalk` in `codegen_types.h`, a case
per type kind driven by the layout (`Layout`, C.2), recursing into struct
fields and variant payloads, with a check per relative-reference field —
about 250 lines of codegen rather than a new subsystem.

* **Fixed elements.** Element starts are implicit, so one walk does
  everything: it range-checks tags, bounds limited-array lengths, and
  validates a link with `0 <= f + o < n && (f + o) % sizeof(T) == 0`. No
  scratch at all, and `bm` is passed as null.
* **Variable elements.** Two walks of the same shape. The first frames the
  payload and sets a bit per element start in `bm`, a bitmap of one bit per
  payload byte on a scratch data stack; the second re-walks and asks the
  bitmap about each link. The scratch is `(n + 7) / 8` bytes on a
  statement-scoped stack, so it is gone at the end of the statement.
* **Bounds.** Every read is bounded before it happens, and the varint decoders
  are the bounded `gs_uleb_check` / `gs_zig_check` (runtime.h) rather than the
  ordinary ones, which loop until a terminator the image may not contain.
* **Element types with no relative reference at all** skip the link pass
  entirely; **types the walk has nothing to say about** (fixed, no tag to
  range-check, no limited-array length to bound, no link) are skipped over as
  opaque bytes rather than walked field by field.
* Cost: one or two linear passes over the payload, no allocation for fixed
  elements, one bitmap for variable ones. Verification is O(n) and
  branch-light — the same order as the copy that follows it, which is the
  FlatBuffers experience as well. `samples/25_serialize.goose` reports the
  measured time for its own index on stderr.

The limited-array case is the one worth calling out: slots past the stored
length were never written (§5.3), so the walk checks the links in the live
slots only, and then skips the cursor over the whole capacity.

**The bitmap ceiling.** One bit per payload byte, on one data stack, so the
largest verifiable image of *variable* elements is eight times a stack's
reservation — 2 GB at the default `GS_STACK_RESERVE` of 256 MB, and it moves
with that constant. A payload past it is rejected (`false`) rather than
growing into the guard region, which would be an abort. Fixed-element images
have no such limit, since they need no scratch. The alternative — an array of
element offsets and a binary search per link — has no ceiling but costs 8
bytes per element instead of one bit per byte, which is worse for every
element under 64 bytes and turns an O(1) test into an O(log n) one.

## 7. Open points

* **References into payload fields** (`i64&<u16>` at a field of an element)
  are rejected for `from_bytes`. What they need is not conceptually harder —
  the framing pass would mark, per *pointee type*, every offset at which a
  value of that type lives, and the link pass would consult the bitmap for
  the reference's own pointee type. The costs are that one bitmap becomes one
  per distinct pointee type, that the fixed-element fast path (no scratch at
  all, a modulo test for links) is lost the moment such a reference exists,
  and that "the valid offsets" stop being a static residue set as soon as a
  limited array's live length or an ADT's tag decides them. Rejecting the
  whole element type at the call, by name, was the cheap and honest v1.
* **`from_bytes` into a fixed or limited array** would have to reject an
  image whose count does not match a capacity that is not in the image; the
  result is also a fixed-size C value rather than a stack destination, which
  is a different codegen path.
* **Big-endian hosts** abort. Either half of the fix (prove the bytes stay
  inside the program, or emit byte-swapping variants keyed off the element
  layout) is real work that no current target needs.
* **Queues** already carry images: `qput(pool.to_bytes())` and
  `from_bytes<T[>..]>(qget<u8[>..]>())` work today, since a `u8[>..]` is
  flat. What is *not* wired up is `qput(pool)` on a non-flat type doing the
  to_bytes/verify implicitly — a convenience, not a capability (TODO 9).
* **Element types whose values can occupy zero bytes** (a struct of nothing)
  have no recoverable element count; their verifier accepts the empty payload
  and nothing else.
* **A container format** — magic, version, type identity, checksum — is
  deliberately outside this. The length prefix is the one piece of metadata
  simple and useful enough to be the language's.
