// error: instantiating g(i64[1][1][1][1][1][1][1][1][1][1][1][1][1][1][1][1], i64) would put more than 16 specializations of g in progress on this call path
// error: a recursive call must reach a finite set of instantiations
// Every branch is typechecked, so a recursive call whose argument type grows
// with each round (T, T[1], T[1][1], ...) instantiates g without end, however
// soon n stops it at run time (§7.8).
recursive fn g<T>(x: T, n: i64) -> i64 {
    if n == 0 { return 0; }
    let a: T[1] = [x];
    g(a, n - 1) + 1
}
fn main() { print(g(1, 3)); }
