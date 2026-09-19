// error: cannot call f, which may shrink b while s
// error: is still used: it may refer into b
// A by-value holder parameter's references lead to the caller's arrays: a
// shrink through one counts as a shrink of any array the argument's contents
// may point at (§5.2).
struct P { x: i64[>..<]&, y: i64[>..<]& }
fn f(p: P) { p.y.pop(); }
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    let pp = P { x: a, y: b };
    let s = b[..];
    f(pp);
    b.push(77);
    print(s[1]);
}
