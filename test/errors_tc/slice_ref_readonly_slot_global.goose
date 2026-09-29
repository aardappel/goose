// error: cannot write through this reference: non-writable provenance
// A field of a `const` global is not written (§9.5): bound by reference it
// is a read-only reference, as `&g.f` is.
struct S { f: u8[:] }
var buf: u8[>..] = [];
const g = S { f: buf[..] };
fn set(q: u8[:]&) { q = buf[1..]; }
fn main() {
    buf.append("xyz");
    set(g.f);
    print(g.f);
}
