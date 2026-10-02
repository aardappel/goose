// error: cannot call g: it may shrink ys while r is still used, and r may refer into ys
// h is a copy of an element of hs, so h.a, read back out of it, may point
// into ys or zs (§9.5), and g's parameters get a root class each. Inside the body two classes are two
// arrays, but this call passes ys to both: the pop frees the slot r points
// at, and the push reuses it (§5.1).
struct H { a: i64[>..]& }
fn g(xs: i64[>..]&, r: i64&) { xs.pop(); xs.push(77); print(r); }
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    var zs: i64[>..] = [4];
    let hs = [H { a: ys }];
    let h = hs[0];
    g(h.a, ys[2]);
}
