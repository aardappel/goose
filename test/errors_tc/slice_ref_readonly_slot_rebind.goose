// error: storing a read-only reference of type const (u8[:])& in a slot of type
// `.=` binds a slice lvalue by reference (§3.8), a field of a `const` value
// as a read-only reference (§9.5), which only a slot declared const holds.
struct S { f: u8[:] }
var buf: u8[>..] = [];
fn main() {
    buf.append("xyz");
    const s = S { f: buf[..] };
    var o: u8[:]&? = null;
    o .= s.f;
    if o { o = buf[1..]; }
    print(s.f);
}
