// error: cannot call f, which may shrink pp while s
// error: is still used: it may refer into pp
// The back edge's summary is incomplete, so it counts as shrinking every
// grow-shrink array the arguments reach, through their references too: the
// pop below it reaches b through pp (§5.2).
struct P { x: i64[>..<]&, y: i64[>..<]& }
recursive fn f(n: i64, pp: P&) -> i64 {
    if n > 0 {
        let s = pp.y[..];
        let z = f(n - 1, pp);
        pp.y.push(77);
        return s[1] + z;
    }
    pp.y.pop();
    return 0;
}
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5];
    var p = P { x: a, y: b };
    print(f(1, p));
}
