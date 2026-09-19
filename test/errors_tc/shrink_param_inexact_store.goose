// error: cannot pop xs while h is still used: a reference into it was stored there
// g's shrink of xs is replayed at the call in f, where xs is a parameter class
// whose storage f cannot see. The slice stored into h is only bounded by y, as
// deep as f's locals, so it may point into xs's array too (§5.1).
struct H { s: const i64[:] }
fn g(xs: i64[>..]&) { xs.pop(); }
fn f(xs: i64[>..]&, c: bool) {
    var z: i64[3] = [1, 2, 3];
    var y: i64[3] = [4, 5, 6];
    var h = H { s: z[..] };
    let q: const i64[:] = if c { z[..] } else { y[..] };
    h.s = q;
    g(xs);
    print(h.s[0]);
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    f(a, a.len > 1);
}
