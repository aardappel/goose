// Inlining only adds facts (§10.5): a probe index masked below a slice
// parameter's length stays below it, and needs no check, in each iteration
// of a caller's loop, which binds the inlined body's parameter anew. The
// bound holds only of a value placed after the last rebind: an index placed
// against a reference that is then rebound keeps its check.

import std;

inline fn probe(xs: f64[:], k: i64) -> f64 {
    assert(xs.len > 0);
    let m = xs.len - 1;
    var i = k & m;
    while xs[i] < 0.0 { i = (i + 1) & m; }     // bce:elide
    return xs[i];                              // bce:elide
}

fn rebound(a: f64[>..]&, b: f64[>..]&, k: i64) -> f64 {
    var r .= a;
    assert(r.len > 0);
    let m = r.len - 1;
    var i = k & m;
    var s = 0.0;
    for j in 2 {
        s += r[i];                             // bce:keep (r is b from the second iteration)
        r .= b;
    }
    return s;
}

fn main() {
    let ys: f64[>..] = [-1.0, 2.0, -3.0, 4.0];
    var s = 0.0;
    for j in 4 { s += probe(ys, j); }
    print(s);
    var a: f64[>..] = [1.0, 2.0, 3.0, 4.0];
    var b: f64[>..] = [5.0, 6.0];
    print(rebound(a, b, 5));
}
