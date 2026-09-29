// error: cannot clear a while r3 is still used: a reference to what may hold a reference or slice into it was stored there
// r3 refers to rr, whose field refers to s, which views a (§5.1).
struct R { r: u8[:]& }
struct R3 { q: R& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var s = a[..];
    var rr = R { r: s };
    var r3 = R3 { q: rr };
    a.clear();
    a.append("ZZZZZZZZ");
    print(r3.q.r);
}
