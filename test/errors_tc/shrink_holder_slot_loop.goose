// error: cannot clear a while r1 is still used: a reference to what may hold a reference or slice into it was stored there
// The first iteration copies r1 into rr, and r1 leads to a through bx1: the
// second one's clear frees what rr.r.s views (§5.1).
struct B { s: u8[:] }
struct R { r: B& }
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var b: u8[>..] = [];
    b.append("xyz");
    var bx0 = B { s: b[..] };
    var bx1 = B { s: a[..] };
    let r1 = R { r: bx1 };
    var rr = R { r: bx0 };
    for i in 0..2 {
        a.clear();
        a.append("ZZZZZZZZ");
        print(rr.r.s);
        rr = r1;
    }
}
