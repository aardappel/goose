// error: cannot clear a while rr is still used: a reference to a slice that may point into it was stored there
// fill stores a reference to s, which views a, into rr, which held one to t
// (§5.1).
struct R { r: u8[:]& }
fn fill(o: R&, p: u8[:]&) { o = R { r: p }; }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var s = a[..];
    var t = b[..];
    var rr = R { r: t };
    fill(rr, s);
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.r);
}
