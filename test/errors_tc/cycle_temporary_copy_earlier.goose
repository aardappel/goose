// error: depth calls into its recursive cycle while the non-fixed-size temporary "abc" (evaluated at
// A by-value non-fixed-size parameter takes a copy of its argument, a
// temporary of the statement whatever the argument is (§4.1): size's copy of
// the static text, evaluated before the call into the cycle in the same
// statement, would be held across it (§7.8).
fn size(s: u8[]) -> i64 { s.len }
recursive fn depth(n: i64) -> i64 {
    if n == 0 { return 0; }
    return size("abc") + depth(n - 1);
}
fn main() { print(depth(3)); }
