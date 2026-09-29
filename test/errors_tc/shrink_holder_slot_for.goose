// error: cannot clear a while h is still used: a reference to a slice that may point into it was stored there
// The binder is a copy of rs's element, whose field refers to s, which views
// a (§5.1).
struct R { r: u8[:]& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rs: R[>..] = [];
    rs.push(R { r: s });
    for h in rs {
        a.clear();
        a.append("ZZZZZZZZ");
        print(h.r);
    }
}
