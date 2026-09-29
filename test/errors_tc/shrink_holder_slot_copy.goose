// error: cannot clear a while r2 is still used: a reference to a slice that may point into it was stored there
// r2 is a copy of rr, holding the same reference to s, which views a (§5.1).
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    var r2 = rr;
    a.clear();
    a.append("ZZZZZZZZ");
    print(r2.r);
}
