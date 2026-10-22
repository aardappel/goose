// h's cycle reaches m in its first round only (h(n - 1) never returns), so
// main's call checks a specialization of m of its own, recursive in its own
// cycle: it returns the reference it is given, which main writes through
// while its non-fixed-size local s is in scope. The rejected shapes are
// errors_tc/cycle_unreached_member*.
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
    if n > 5 { return m(n - 1, xs); }
    return xs[0];
}
fn main() {
    if GARR.len > 5 { print(top()); }
    var s: u8[] = "x";
    let r: i64& = m(7, GARR);
    r = 40;
    print(GARR[0]);
    print(s);
}
