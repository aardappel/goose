// error: storing a read-only slice of type const u8[:] in a slot of type u8[:]
// A reference to a const-typed slice field is writable where the struct is;
// the slice it reaches is read-only, and so is what re-slicing it gives
// (§9.5).
struct Box { s: const u8[:] }
var buf: u8[>..] = [];
fn main() {
    buf.append("xyz");
    var b = Box { s: "abc" };
    let r = &b.s;
    var y = buf[..];
    y = r[..];
    y[0] = 65;
}
