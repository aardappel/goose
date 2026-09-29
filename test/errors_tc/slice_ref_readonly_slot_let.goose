// error: storing a read-only reference of type const (u8[:])& in a slot of type u8[:]&
// A variable declared a reference binds a slice lvalue by reference (§4.1),
// a by-value `for` binding as a read-only reference (§6.5, §9.5), which only
// a slot declared const holds.
var buf: u8[>..] = [];
fn main() {
    buf.append("xyz");
    var arr: u8[:][>..] = [];
    arr.push(buf[..]);
    for s in arr {
        let r: u8[:]& = s;
        r = buf[1..];
        print(s);
    }
}
