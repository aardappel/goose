// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// g shrinks its parent's a and then reads through its parent's rr, which
// leads to a through s (§5.1).
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    fn g() {
        a.clear();
        a.append("ZZZZZZZZ");
        print(rr.r);
    }
    g();
}
