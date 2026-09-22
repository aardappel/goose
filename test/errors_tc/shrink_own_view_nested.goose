// error: cannot call outer: it shrinks G while s is still used, and s may refer into G
// The same where a nested function shrinks the enclosing function's
// parameter: outer's callers learn it through outer (§5.2, §7.5).
var G: i64[>..<] = [1, 2, 3];
fn outer(xs: i64[>..<]&) {
    fn f() {
        let s = G[..];
        xs.clear();
        xs.push(9);
        print(s);
    }
    f();
}
fn main() { outer(G); }
