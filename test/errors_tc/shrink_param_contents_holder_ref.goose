// error: cannot call f: it shrinks a while h is still used, and h may refer into a
// The holder h refers to views a (§5.1).
struct H { s: u8[:] }
fn f(a: u8[>..]&, h: H&) {
    a.clear();
    a.append("ZZZZZZZZ");
    print(h.s);
}
fn main() {
    var a: u8[>..] = [];
    a.append("abc");
    var h = H { s: a[..] };
    f(a, h);
}
