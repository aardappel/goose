// error: storing a read-only slice of type const u8[:] in a slot of type u8[:]
// A slice of a const slice type is read-only however writable the reference
// to its slot is (§9.5): `&b.s` may re-point the field, the struct being a
// `var`, but what re-slicing the field's slice gives stays read-only.
struct Box { s: const u8[:] }
var buf: u8[>..] = [];
fn take(p: const u8[:]&, q: u8[:]&) { q = p[..]; }
fn main() {
    buf.append("xyz");
    var b = Box { s: "abc" };
    var y = buf[..];
    take(&b.s, y);
    y[0] = 65;
}
