// error: cannot call g: it may shrink ys while r is still used, and r may refer into ys
// The first call passes g two arrays, the second reuses its body with h.a,
// which may be either (§9.5) and is ys, beside a reference into ys: what
// the body concluded from its two root classes holds only where every
// call passes two arrays (§5.1).
struct H { a: i64[>..]& }
fn g(xs: i64[>..]&, r: i64&) { xs.pop(); xs.push(77); print(r); }
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    var zs: i64[>..] = [4];
    let h = H { a: ys };
    g(zs, ys[2]);
    g(h.a, ys[2]);
}
