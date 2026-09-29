// error: cannot clear a while rr is still used: a reference into it was stored there
// rr's reference field names t, which may view a as far as its depth says,
// but its slice field certainly does: the error names that store (§5.1).
struct R2 { r: u8[:]&, v: u8[:] }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var t = b[..];
    var rr = R2 { r: t, v: b[..] };
    rr.v = a[..];
    a.clear();
    a.append("ZZZZZZZZ");
    print(rr.v, rr.r);
}
