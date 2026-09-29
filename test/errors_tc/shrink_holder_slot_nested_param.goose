// error: cannot clear x while rr is still used: a reference to a slice that may point into it was stored there
// g's parameter may name any array its parent's s could view, here a: rr,
// which g reads after the clear, leads to it through s (§5.1).
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    fn g(x: u8[>..]&) {
        x.clear();
        x.append("ZZZZZZZZ");
        print(rr.r);
    }
    g(a);
}
