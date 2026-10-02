// error: cannot call g: it may shrink ys while r is still used, and r may refer into ys
// The same for a grow-shrink array: h.a, read back out of a copy of an
// element of hs, may be ys (§9.5), which this call
// passes to both of g's parameters, and r is still used (§5.2).
struct H { a: i64[>..<]& }
fn g(xs: i64[>..<]&, r: i64&) { xs.pop(); xs.push(77); print(r); }
fn main() {
    var ys: i64[>..<] = [1, 2, 3];
    var zs: i64[>..<] = [4];
    let hs = [H { a: ys }];
    let h = hs[0];
    g(h.a, ys[2]);
}
