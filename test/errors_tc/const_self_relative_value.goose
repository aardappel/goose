// error: cannot write through this reference: non-writable provenance
// A self-relative reference points within the value holding it (§3.9), so
// out of a `const` value it is read-only (§9.5): `self` here is the const
// value itself, which a write through the link would change.
struct Ring { next: Ring&<u8>, v: i64 }
fn inc(p: i64&) { p = p + 1; }
fn main() {
    const r = Ring { next: self, v: 1 };
    inc(r.next.v);
    print(r.v);
}
