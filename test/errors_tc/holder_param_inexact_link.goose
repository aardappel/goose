// error: a relative reference must point within the same root as its location
// error: this reference's root is not known exactly, only that it outlives w
// What link's by-value parameter holds is one array only where the call
// site's value held references into one. The first call's does, so its body
// may link one view into the other; the second's views xs and inner's ys,
// which dies first, and gets a body of its own, where what id(w) holds is
// only bounded by w's class (§9.2, §3.9).
struct N { v: i64, next: N&<u32>? }
struct W2 { a: N[:], b: N[:] }
fn id(w: W2) -> W2 { w }
fn link(w: W2) {
    let a = id(w).a;
    let b = id(w).b;
    a[0].next .= b[0];
}
fn inner(xs: N[>..]&) {
    var ys: N[>..] = [N { v: 2 }];
    link(W2 { a: xs[..], b: xs[..] });
    link(W2 { a: xs[..], b: ys[..] });
}
fn main() {
    var xs: N[>..] = [N { v: 1 }];
    inner(xs);
    let n = xs[0].next;
    if n { print(n.v); }
}
