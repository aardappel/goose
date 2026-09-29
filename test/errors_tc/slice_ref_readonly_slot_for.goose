// error: cannot write through this reference: non-writable provenance
// A by-value `for` binding is a copy of the element, never written (§6.5):
// bound by reference, as `&s` of it is, it is a read-only reference (§9.5),
// so set may not re-point it, however writable the slice it holds.
var buf: u8[>..] = [];
fn set(q: u8[:]&) { q = buf[1..]; }
fn main() {
    buf.append("xyz");
    var arr: u8[:][>..] = [];
    arr.push(buf[..]);
    for s in arr { set(s); print(s); }
}
