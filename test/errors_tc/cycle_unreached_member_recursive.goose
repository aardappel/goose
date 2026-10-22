// error: cannot clear GARR while r is still used
// As cycle_unreached_member.goose, with m calling itself: main's call checks
// a specialization of m of its own, whose recursion is a cycle of its own,
// not h's, so main does not join one and may hold s across the call.
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
    let r: i64& = m(1, GARR);
    GARR.clear();
    print(r);
    print(s);
}
