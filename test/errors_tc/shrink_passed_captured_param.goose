// error: cannot call f: it shrinks xs while s is still used, and s may refer into xs
// The captured array is the enclosing function's parameter: two classes,
// which the enclosing function's call makes one array (§5.2, §7.5).
fn outer(xs: i64[>..<]&) {
    fn f(s: i64[:]) {
        xs.clear();
        xs.push(9);
        print(s);
    }
    f(xs[..]);
}
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    outer(a);
}
