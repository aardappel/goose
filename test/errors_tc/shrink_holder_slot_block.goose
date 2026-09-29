// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// The block shrinks main's a and then reads through main's rr, which leads
// to a through s (§5.1).
struct R { r: u8[:]& }
fn apply<F>() { F(); }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    apply() {
        a.clear();
        a.append("ZZZZZZZZ");
        print(rr.r);
    };
}
