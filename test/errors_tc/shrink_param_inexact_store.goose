// error: cannot call f: it shrinks a while h is still used, and h may refer into a
// g's shrink of xs is checked at f's call of g against f's holders, from g's
// summary. The slice stored into h was read out of the holder src names,
// which holds what f's caller stored there: f leaves that to its call,
// which passes a view of a in src's holder and a for xs (§5.1).
struct H { s: const i64[:] }
fn g(xs: i64[>..]&) {
    xs.pop();
    xs.push(90);
}
fn f(xs: i64[>..]&, src: H&) {
    var z: i64[3] = [1, 2, 3];
    var h = H { s: z[..] };
    h.s = src.s;
    g(xs);
    print(h.s[2]);
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    let hh = H { s: a[..] };
    f(a, hh);
}
