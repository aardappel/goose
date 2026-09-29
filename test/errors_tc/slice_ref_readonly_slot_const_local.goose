// error: cannot write through this reference: non-writable provenance
// A field of a `const` value is not written (§9.5): bound by reference, as
// `&s.f` of it is, it is a read-only reference.
struct S { f: u8[:] }
var buf: u8[>..] = [];
fn set(q: u8[:]&) { q = buf[1..]; }
fn main() {
    buf.append("xyz");
    const s = S { f: buf[..] };
    set(s.f);
    print(s.f);
}
