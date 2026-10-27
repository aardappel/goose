// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// t outlives a and cannot view it: only fill's store of a reference to s,
// which views a, leads rr there (§5.1).
struct R { r: u8[:]& }
fn fill(o: R&, p: u8[:]&) { o = R { r: p }; }
fn main() {
    var b: u8[>..] = [];
    b.append("xyz");
    var t = b[..];
    {
        var a: u8[>..] = [];
        a.append("abc");
        var s = a[..];
        var rr = R { r: t };
        fill(rr, s);
        a.clear();
        a.append("ZZZZZZZZ");
        print(rr.r);
    }
}
