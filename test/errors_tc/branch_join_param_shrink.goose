// error: cannot call f: it shrinks gl while xs is still used, and xs may refer into gl
// A slice parameter views the branches' arrays where they lie (§6.4), so
// the callee is checked for an argument that may be gl itself (§5.2).
var gl: i64[>..<] = [1, 2, 3];
fn f(xs: i64[:]) -> i64 {
    gl.pop();
    xs[0]
}
fn main() {
    let other: i64[3] = [4, 5, 6];
    let c = gl.len > 2;
    print(f(if c { gl } else { other }));
}
