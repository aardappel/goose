// error: re-binding r with a reference rooted at a different scope depth
// A reference variable may move from one argument's root to another's only
// where the two share a depth, as main's x and z do and x and inner's y do
// not: r would then be stored as if it still pointed into x.
struct N { v: i64 }
struct H { r: N& }
fn keep(h: H[>..]&, a: N&, b: N&) {
    var r: N& = a;
    r .= b;
    h.push(H { r: r });
}
fn inner(h: H[>..]&, x: N&) {
    var y = N { v: 42 };
    keep(h, x, y);
}
fn main() {
    var hs: H[>..] = [];
    var x = N { v: 1 };
    var z = N { v: 2 };
    keep(hs, x, z);
    inner(hs, x);
    print(hs[1].r.v);
}
