// error: cannot pop xs while h is still used: a reference into it was stored there
// g's shrink of xs is checked against every holder on the compile-time call
// path. The slice stored into h was read out of src, a parameter whose
// contents f cannot see: it is bounded by src's class, as deep as xs's, so it
// may point into xs's array too (§5.1).
struct H { s: const i64[:] }
fn g(xs: i64[>..]&) { xs.pop(); }
fn f(xs: i64[>..]&, src: H&) {
    var z: i64[3] = [1, 2, 3];
    var h = H { s: z[..] };
    h.s = src.s;
    g(xs);
    print(h.s[0]);
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    let hh = H { s: a[..] };
    f(a, hh);
}
