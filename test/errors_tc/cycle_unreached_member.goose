// error: cannot clear GARR while r is still used
// The first round of h's cycle reaches m, the second does not: h(n - 1)
// never returns, which the first round's record of h says. m's record then
// settled with nothing, so main's call checks m anew rather than reusing
// it, and finds r rooted at GARR, as it would without the cycle.
var GARR: i64[>..] = [1, 2, 3];
fn top() -> i64 { return h(3); }
recursive fn h(n: i64) -> i64 {
    if n == 0 { exit(0); }
    h(n - 1);
    let r: i64& = m(n, GARR);
    exit(1);
}
recursive fn m(n: i64, xs: i64[>..]&) -> i64& {
    if n < 0 { h(n); }
    return xs[0];
}
fn main() {
    if GARR.len > 5 { print(top()); }
    let r: i64& = m(1, GARR);
    GARR.clear();
    print(r);
}
