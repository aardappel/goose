# Serialization of relative-reference structures: a verified loader

*Future work. What the language promises, what a loader has to check, and
the shape of an implementation that keeps the safety guarantees of §9.*

## 1. The promise

A structure whose links are all self-relative references (§3.9) is
position-independent: a tree of `Node { v: i32, l: Node&<u32>?, r:
Node&<u32>? }` in a `Node[>..]`, or a `Sexp..[>..]` pool, is a run of
bytes that means the same wherever it sits. `bench/design.md` counts "Goose
data is already its own serialized form" among the design's advantages:
saving is a write of the element region, loading is a read of it, and there
is no pointer fixup pass because there are no pointers.

Nothing in the language expresses either half today. There is no byte view
of an array (`T[>..]` to `u8[:]`), relative references make a type non-flat
so `extern fn` refuses it (§7.10), and constructing an array from bytes
would need a builtin that the type system trusts.

## 2. Why a trusting loader is not acceptable

`from_bytes<T[>..]>(bytes)` that reinterpreted the bytes would be the first
operation in the language that can produce a reference the checker did not
prove valid: a corrupted or malicious offset is an address anywhere in memory,
which breaks the first priority of §9.1 outright. Every other unsafety Goose
admits is type-safe reuse (§9.4); this one is not. So the loader has to
verify, the way a FlatBuffers verifier does, before the bytes are a value.

## 3. What must be verified

For a candidate image of `T[>..]` (or `T[]`, `T[varint]`):

1. **Framing.** The image parses as a sequence of `T` values: for fixed `T`
   the byte length is a multiple of `sizeof(T)`; for variable `T` a
   sequential walk of the elements (the same walk `print` and `==` use) ends
   exactly at the image's end, every varint terminates, every tag is in
   range, every nested length fits what is left.
2. **Element starts.** The walk yields the set of element start offsets, and
   for variable-mode ADTs the tag at each start.
3. **Links.** For every self-relative reference field at absolute offset `f`
   with stored offset `o`: `o == 0` only if the field is optional; otherwise
   `f + o` is an element start (or, for a reference typed at a variant,
   `Sym&<u32>`, an element start whose tag is that variant). A reference
   whose pointee type is not the element type itself -- a `i64&<u16>` into a
   field of an element -- would need every valid address of that type to be
   enumerated; v1 simply rejects such types for `from_bytes`.
4. **Excluded types.** Plain references and slices (they are addresses),
   `in pool` references (they mean an offset into a *named* global pool,
   not into the image), resizable fields (a resizable-tailed struct cannot be
   an element anyway), and reusable pools (their freelist is not in the
   image).

Everything §3.9 already guarantees about a self-relative structure built by
the program -- both ends in one root array -- is exactly what step 3 checks
for one arriving from outside. A verified image is then indistinguishable
from a constructed one, and the ordinary rules apply to it from then on.

## 4. The builtins

```goose
fn to_bytes(a: T[>..]) -> u8[>..]                 // the element region, or the whole value for T[]
fn from_bytes<T[>..]>(bytes: u8[:]) -> T[>..], bool  // a verified copy at the destination, or [] and false
```

`to_bytes` is safe for any array whose element type contains no plain
references, slices or `in pool` references (a static check); it copies the
element region (and, for `T[]`, the length prefix) into a fresh `u8[>..]`
built at the destination like any other result (§7.3). Copying rather than
viewing keeps the image independent of the array's later growth.

`from_bytes` runs the verifier over `bytes`, then copies them into the
result's element region. Its result is a fresh array rooted at its own
variable, exactly as if the program had pushed the elements itself, so it
needs no new rule in §9. The `bool` is the verifier's verdict; on failure
the array is empty. Both are ordinary builtins with custom typechecking
(like `default<T>()`): `to_bytes` rejects the excluded element types at the
call, `from_bytes` needs its explicit type argument (§7.7).

## 5. Generating the verifier per type

The verifier is generated per element type, like the equality and size
functions codegen already emits (`gs_size_T`, the `EmitEqWalk` walker):

* `gs_verify_T(const uint8_t *p, int64_t n) -> int` does the framing walk
  and collects element starts (fixed `T`: implicit, `i * sizeof(T)`;
  variable `T`: into a scratch array of offsets on a data stack, or a bitmap
  of one bit per byte for large images), then walks again checking links.
  For fixed elements with no variant types among the pointees, the second
  walk needs no scratch at all: a link is valid iff `(f + o) % sizeof(T) == 0`
  and it lies inside the region.
* The per-type walker is the same code shape as `EmitEqWalk`, driven by the
  layout (`Layout`, C.2), so it is a few hundred lines of codegen rather than
  a new subsystem: a case per type kind, recursion into struct fields and
  variant payloads, a check per relative-reference field.
* Cost: two linear passes over the image, no allocation for fixed elements,
  one scratch array for variable ones. Verification is O(n) and branch-light
  -- it is the same order as the copy that follows it, which is the FlatBuffers
  experience as well.

## 6. Open points

* **Queues** (TODO 9): the same verifier lets a self-contained relative
  structure cross a thread queue, since the receiving side can verify the
  image it unpacks; that relaxes the flat-only rule for exactly these types.
* **Variable-size `T` with references into payload fields**: rejected in v1
  (§3 step 3); an element-start-plus-field-offset enumeration would admit
  them later.
* **A view instead of a copy** for `to_bytes` (`u8[:]` of the element region)
  is cheaper for a save, but the slice would be rooted at the array and stay
  valid only while it lives, which the ordinary slice rules already express;
  it can be added as `bytes_of(a) -> u8[:]` without touching the verifier.
