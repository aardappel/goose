// error: storing a read-only slice of type u8[:] (read-only in this instantiation) in a slot of type u8[:]
// A slice loaded through a const reference is read-only, whatever the slot
// the reference names holds (§9.5).
var buf: u8[>..] = [];
fn take(p: const (u8[:])&, q: u8[:]&) { q = p; }
fn main() {
    buf.append("xyz");
    var x = buf[..];
    var y = buf[0..1];
    take(x, y);
}
