// error: cannot call f: it may shrink ys while b is still used, and b may refer into ys
// f passes g its own two parameters' classes, so they are two arrays in g
// wherever they are in f, and f is given h.a, which may be ys (§9.5), beside
// ys itself (§5.1).
struct H { a: i64[>..]& }
fn g(xs: i64[>..]&, r: i64&) { xs.pop(); xs.push(77); print(r); }
fn f(a: i64[>..]&, b: i64[>..]&) { g(a, b[2]); }
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    var zs: i64[>..] = [4];
    let h = H { a: ys };
    f(h.a, ys);
}
