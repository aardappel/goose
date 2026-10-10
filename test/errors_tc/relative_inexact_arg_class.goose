// error: a relative reference must point within the same root as its location
// error: this reference is rooted at b
// h.r is read back out of a copy of an element of hs, which holds references
// into xs and into ys, so it may point into either: it points into xs. The
// exact ys[0] after it shares a root with it and still gets a class of its
// own, since sharing one would make link's parameters one array and let a
// link from xs into inner's ys, which dies first, compile (§9.5, §3.9).
struct N { v: i64, next: N&<u32>? }
struct H { r: N& }
fn link(a: N&, b: N&) { a.next .= b; }
fn inner(xs: N[>..]&) {
    var ys: N[>..] = [N { v: 2 }];
    let hs = [H { r: xs[0] }, H { r: ys[0] }];
    let h = hs[0];
    link(h.r, ys[0]);
}
fn main() {
    var xs: N[>..] = [N { v: 1 }];
    inner(xs);
    let n = xs[0].next;
    if n { print(n.v); }
}
