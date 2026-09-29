// error: storing a read-only slice of type u8[:] (read-only in this instantiation) in a slot of type u8[:]
// A slice loaded through a reference to a slice variable is the variable's,
// read-only where its binding is (§9.5).
var buf: u8[>..] = [];
fn text(s: u8[:]) {
    var x = s;
    let r = &x;
    var z = buf[..];
    z = r;
}
fn main() {
    buf.append("xyz");
    text("abc");
}
