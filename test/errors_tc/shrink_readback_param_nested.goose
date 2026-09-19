// error: cannot call g, which may shrink pp while s
// error: is still used: it may refer into pp
// g was checked at its first call, before s existed; the second call replays
// its summary, in which the array g pops is one pp only leads to (§5.2).
struct P { x: i64[>..<]&, y: i64[>..<]& }
fn f(pp: P&) -> i64 {
    fn g() { pp.y.pop(); }
    g();
    let s = pp.y[..];
    g();
    pp.y.push(77);
    s[1]
}
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5, 6];
    var p = P { x: a, y: b };
    print(f(p));
}
