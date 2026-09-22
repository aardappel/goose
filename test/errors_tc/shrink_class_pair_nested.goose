// error: cannot call g: it shrinks a while s is still used, and s may refer into a
// k is given a class of f and one of the nested g, whose slice s f made of
// a itself: classes of two specializations may be one array, however
// exactly each was rooted (§5.1).
fn k(xs: i64[>..]&, s: i64[:]) { xs.pop(); xs.push(77); print(s[2]); }
fn f(a: i64[>..]&) {
    fn g(s: i64[:]) { k(a, s); }
    g(a[..]);
}
fn main() {
    var ys: i64[>..] = [1, 2, 3];
    f(ys);
}
