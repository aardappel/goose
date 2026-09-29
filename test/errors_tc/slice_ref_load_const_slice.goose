// error: storing a read-only slice of type const u8[:] in a slot of type u8[:]
// A slice loaded through a reference to a const-typed slice is read-only
// however writable the reference is (§9.5), so only a const slot takes it.
struct Box { s: const u8[:] }
var buf: u8[>..] = [];
fn take(p: const u8[:]&, q: u8[:]&) { q = p; }
fn main() {
    buf.append("xyz");
    var b = Box { s: "abc" };
    var y = buf[..];
    take(&b.s, y);
    y[0] = 65;
}
