# Serialization of relative-reference structures: a verified loader

<details>
<summary>Table of contents</summary>

- [1. Serialization guarantees](#1-serialization-guarantees)
- [2. Why loading requires validation](#2-why-loading-requires-validation)
- [3. The image](#3-the-image)
- [4. What must be verified](#4-what-must-be-verified)
- [5. The builtins](#5-the-builtins)
- [6. Generating the verifier per type](#6-generating-the-verifier-per-type)
- [7. Open points](#7-open-points)

</details>

*Implemented. This document describes the serialization guarantees, the
loader's validation rules, and how the implementation preserves §9's safety
guarantees.*

## 1. Serialization guarantees

A structure whose links are all self-relative references (§3.9) is
position-independent: a tree of `Node { v: i32, l: Node&<u32>?, r:
Node&<u32>? }` in a `Node[>..]`, or a `Sexp..[>..]` pool, is a contiguous
sequence of bytes whose links remain valid when the sequence is relocated.
`bench/design.md` counts "Goose data is already its own serialized form"
among the design's advantages. Saving writes the element region, loading
reads it back, and no pointer fixup pass is needed because there are no
pointers.

Three builtins (§12) provide this. `samples/25_serialize.goose` is the
worked example, and `test/storage/serialize.goose` holds the tests.

## 2. Why loading requires validation

If `from_bytes<T[>..]>(bytes)` reinterpreted the bytes without checking them,
it would be the first operation in the language that can produce a reference
the checker did not prove valid. A corrupted or malicious offset could be an
address anywhere in memory, which would break the first priority of §9.1.
Every other kind of unsafety that Goose allows is type-safe reuse (§9.4), and
this one would not be. The loader must therefore validate the bytes before
accepting them as a value, as a FlatBuffers verifier does.

What is verified is *safety*, not integrity. An image whose data bytes were
edited still describes a well-formed structure, and it is accepted with the
wrong numbers in it. A checksum, not the verifier, is what shows that the
file is the one that was written. Nothing checks that the file came from
*this* program either, since the type is not stored in the image. Recording
the type is a container format's job, not the language's.

## 3. The image

```
uleb128(payload byte count)  payload = the array's element region
```

The prefix lets a reader of a stream or a long file know how much to read
before it has the whole image. It is the ULEB128 of §3.6, and `format_uleb`
/ `parse_uleb` in `stdlib/std.goose` implement the same encoding for a
program that wants to write or read the prefix itself. `from_bytes` requires
the prefix to account for *exactly* the rest of the slice, so truncation and
extension are both caught before the walk starts.

The payload must be little-endian, as specified by the layouts in C.2 and
supported by every compiler target. `to_bytes`, `bytes_of`, and `from_bytes`
test the host and abort with `GS_E_ENDIAN` on a big-endian host. The test folds
to a constant on every supported target. A big-endian port would need either to
prove that an image never leaves the program or to emit byte-swapping variants.

## 4. What must be verified

For a candidate payload of `T[...]`:

1. **Framing.** The payload parses as a sequence of `T` values. For fixed
   `T`, the byte length is a multiple of `sizeof(T)`. For variable `T`, a
   sequential walk of the elements (the same walk `print` and `==` use) ends
   exactly at the payload's end, every varint terminates, every tag is in
   range, and every nested length fits in what is left.
2. **Element starts.** The walk yields the set of element start offsets, and
   for variable-mode ADTs the tag at each start.
3. **Links.** For every self-relative reference field at absolute offset `f`
   with stored offset `o`: `o == 0` is null and legal only where the field is
   optional; otherwise `f + o` is an element start. A reference typed at a
   *variant* addresses that variant's payload, so for such a reference
   `f + o − sizeof(tag)` must be an element start, and the tag there must be
   that variant's tag. A reference whose pointee type is neither the element
   type nor a variant of it, such as an `i64&<u16>` into a field of an
   element, would need every valid address of that type to be enumerated.
   v1 rejects such element types for `from_bytes`: the error is reported at
   the call and names the type (see §7).
4. **Excluded types.** Plain references and slices (they are addresses) and
   `in pool` references (they mean an offset into a *named* global pool, not
   into the image) are rejected for all three builtins. Resizable fields
   cannot be array elements in the first place.

What §3.9 already guarantees for a self-relative structure built by the
program (both ends of a link in one root array) is exactly what step 3
checks for a structure arriving from outside. A verified image is then
indistinguishable from a constructed one, and the ordinary rules apply to it
from then on. The result is rooted at its own variable, it can grow, and
relative stores into it are checked like any other relative store.

A `reusable` pool (§5.4) writes and loads like any other array. Its freelist
is separate state that no image carries, so a loaded pool starts with an
empty freelist and all slots live, consistent with §5.4.

## 5. The builtins

```goose
fn to_bytes(a) -> u8[>..]                            // the image, as a fresh value
fn to_bytes(a, out: u8[>..]&)                        // the image, appended to a builder
fn bytes_of(a) -> u8[:]                              // the payload alone, as a view
fn from_bytes<T[>..]>(bytes: u8[:]) -> T[>..], bool  // a verified copy, or [] and false
```

All three are safe for any array or slice whose element type contains no
plain references, slices or `in pool` references (a static check). Per UFCS,
they can be written either way: `a.to_bytes()` or `to_bytes(a[1..])`.

`to_bytes` copies. The two-argument form appends to a growable `u8` array
that the caller owns (resizable or limited), so a message or file header can
go in front of the image without a second copy. The one-argument form builds
a fresh `u8[>..]` at its destination like any other result (§7.3).

`bytes_of` copies nothing. It returns a `u8[:]` over the element region,
rooted at the array exactly as a slice of the array would be. So §5.1 and
§5.2 keep the array from shrinking under the view, and §9 keeps the view from
outliving the array. The view is **never writable**, whatever the array's own
provenance, because writing through it could create relative references that
the checker had not validated. Its length is the value the framing prefix
would hold. A save that writes its own header (`format_uleb`) and then the
payload therefore produces a file that `from_bytes` reads back, without ever
building an image in memory.

`from_bytes` checks the prefix, runs the verifier over the payload, and then
copies the payload into the result's element region. The count the verifier
returns becomes the result's length. The result is a fresh array rooted at
its own variable, exactly as if the program had pushed the elements itself,
so it needs no new rule in §9. `from_bytes` builds any array kind whose
contents are an element run plus a count: `T[>..]`, `T[>..<]`, and the `T[]`
family, whose count goes into the destination's own length prefix. A fixed
or limited array would also need the count to match a capacity that the
image does not carry, so those are rejected.

The `bool` is the verifier's verdict; on failure the array is empty. It may be
dropped (`var a = from_bytes<T>(b);`), when the caller accepts an empty
array as the result of invalid input.

Both are ordinary builtins with custom typechecking (like `default<T>()`);
`from_bytes` needs its explicit type argument (§7.7).

The pair round-trips: `from_bytes<T[>..]>(a.to_bytes())` is `a`, for every
element type `from_bytes` accepts.

## 6. Generating the verifier per type

The verifier is generated per element type, like the existing size and equality
functions (`gs_size_T`, `EmitEqWalk`). Its signature is `gs_verify_T(const
uint8_t *p, int64_t n, uint8_t *bm) -> int64_t`; it returns the element count or
−1. `EmitVerifyWalk` in `codegen_types.h` generates it using a case for each
type kind and the layout rules (`Layout`, C.2). The generated code recurses into
struct fields and variant payloads, checking each relative-reference field. The
implementation adds about 250 lines to codegen.

* **Fixed elements.** Element starts are implicit, so a single walk does all
  the checks: it range-checks tags, bounds limited-array lengths, and
  validates a link with `0 <= f + o < n && (f + o) % sizeof(T) == 0`. No
  scratch memory is needed, and `bm` is passed as null.
* **Variable elements.** These need two walks of the same shape. The first
  walk frames the payload and sets a bit per element start in `bm`, a bitmap
  of one bit per payload byte on a scratch data stack. The second walk goes
  over the payload again and looks up each link in the bitmap. The scratch is
  `(n + 7) / 8` bytes on a statement-scoped stack, so it is released at the
  end of the statement.
* **Bounds.** Every read is bounded before it happens, and the varint decoders
  are the bounded `gs_uleb_check` / `gs_zig_check` (runtime.h) rather than the
  ordinary ones, which loop until a terminator the image may not contain.
* **Element types with no relative references** skip the link pass. **Types
  that need no checks** (fixed, with no tag to range-check, no limited-array
  length to bound and no link) are skipped over as opaque bytes rather than
  walked field by field.
* Cost: verification takes one or two linear passes over the payload, with
  no allocation for fixed elements and one bitmap for variable ones. It is
  O(n) and branch-light, the same order as the copy that follows it, which
  matches the FlatBuffers experience. `samples/25_serialize.goose` reports
  the measured time for its own index on stderr.

Limited arrays are a special case. Slots past the stored length were never
written (§5.3), so the walk checks the links in the live slots only, and then
moves the cursor past the whole capacity.

**The bitmap ceiling.** The bitmap uses one bit per payload byte, on one data
stack, so the largest verifiable image of *variable* elements is eight times
a stack's reservation. That is 16 GB at the default `GS_STACK_RESERVE` of
2 GB, and the ceiling changes with that constant. A larger payload is rejected
(`false`) rather than letting the bitmap grow into the guard region, which
would abort. Fixed-element images have no such limit, since they need no
scratch. The alternative, an array of element offsets with a binary search
per link, has no ceiling. However, it costs 8 bytes per element instead of
one bit per byte, which is worse for every element under 64 bytes, and it
turns an O(1) test into an O(log n) one.

## 7. Open points

* **References into payload fields** (`i64&<u16>` at a field of an element)
  are rejected for `from_bytes`. Supporting them is not conceptually harder:
  the framing pass would mark, per *pointee type*, every offset at which a
  value of that type is stored, and the link pass would consult the bitmap
  for the reference's own pointee type. This has three costs. The single
  bitmap becomes one bitmap per distinct pointee type. The fixed-element fast
  path (no scratch memory, and a modulo test for links) is lost as soon as
  such a reference exists. And "the valid offsets" stop being a static
  residue set as soon as a limited array's live length or an ADT's tag
  determines them. The first version therefore rejects the element type at
  the call and names it in the diagnostic.
* **`from_bytes` into a fixed or limited array** would have to reject an
  image whose count does not match a capacity that is not in the image. The
  result would also be a fixed-size C value rather than a stack destination,
  which is a different codegen path.
* **Big-endian hosts** abort. Either option for fixing this (proving that the
  bytes stay inside the program, or emitting byte-swapping variants keyed off
  the element layout) is real work that no current target needs.
* **Queues** already carry images: `qput(pool.to_bytes())` and
  `from_bytes<T[>..]>(qget<u8[>..]>())` work today, since a `u8[>..]` is
  flat. What is *not* wired up is `qput(pool)` on a non-flat type doing the
  to_bytes/verify implicitly. That would be a convenience, not a new
  capability (TODO 9).
* **Element types whose values can occupy zero bytes** (a struct of nothing)
  have no recoverable element count; their verifier accepts the empty payload
  and nothing else.
* **A container format** (magic, version, type identity, checksum) is
  deliberately outside this design. The length prefix is the only piece of
  metadata that is simple and useful enough to be part of the language.
