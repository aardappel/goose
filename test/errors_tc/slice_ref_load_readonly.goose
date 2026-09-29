// error: storing a read-only slice of type u8[:] (read-only in this instantiation) in a slot of type u8[:]
// A slice loaded through a reference to a slice variable holding a read-only
// slice is read-only (§9.5): in the callee, for this instantiation.
var buf: u8[>..] = [];
fn take(p: u8[:]&, q: u8[:]&) { q = p; }
fn text(s: u8[:]) {
    var x = s;
    var y = buf[..];
    take(x, y);
}
fn main() {
    buf.append("xyz");
    text("abc");
}
