// error: storing a read-only reference of type const (u8[:])& in a slot of type u8[:]&
// `&x` of a slice variable holding a read-only slice is a read-only
// reference (§9.5), which only a slot declared const holds: read back out of
// one that is not, it would let the slice be written.
struct H { r: u8[:]& }
fn text(s: u8[:]) {
    var x = s;
    let h = H { r: &x };
    h.r[0] = 65;
}
fn main() { text("abc"); }
