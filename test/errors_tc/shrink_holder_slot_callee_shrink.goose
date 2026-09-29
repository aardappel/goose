// error: cannot call clr, which shrinks a while rr is still used: a reference to a slice that may point into it was stored there
// clr's shrink of a is the call's, and rr leads to a through s (§5.1).
struct R { r: u8[:]& }
fn clr(a: u8[>..]&) {
    a.clear();
    a.append("ZZZZZZZZ");
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    clr(a);
    print(rr.r);
}
