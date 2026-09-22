// error: cannot call f: it may shrink an array p leads to while s is still used, and s may refer into it
// A pop through a reference that the parameter's pointee holds frees an
// array the argument's root only bounds (§5.2, §9.2).
struct P { y: i64[>..<]& }
fn f(pp: P&, s: i64[:]) {
    pp.y.pop();
    pp.y.push(9);
    print(s);
}
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var p = P { a };
    f(p, a[..]);
}
