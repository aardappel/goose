// error: cannot clear a while rr is still used: a reference to what may hold a reference or slice into it was stored there
// Each iteration copies r1 into rr before the clear, and r1 leads to a
// through bx1: the next iteration's print through rr reads what the clear
// freed (§5.1). Without the loop, nothing uses rr after the clear.
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
        print(rr.r.s);
        rr = r1;
        a.clear();
        a.append("ZZZZZZZZ");
    }
}
