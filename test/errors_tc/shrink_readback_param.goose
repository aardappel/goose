// error: cannot pop pp.y while s
// error: is still used: it may refer into pp.y
// pp.y is read back out of the caller's storage behind pp, which only bounds
// the array it points at, so the rule for that array's elements applies,
// whatever pp's own type can hold (§5.2).
struct P { x: i64[>..<]&, y: i64[>..<]& }
fn f(pp: P&) -> i64 {
    let s = pp.y[..];
    pp.y.pop();
    pp.y.push(77);
    s[1]
}
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    var p = P { x: a, y: b };
    print(f(p));
}
