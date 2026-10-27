// error: cannot clear a while r1 is still used: a reference to what may hold a reference or slice into it was stored there
// Inside a loop body, r1's field refers to bx1, whose slice views a, and the
// copy into rr after the clear reads r1 in the same iteration (§5.1).
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
