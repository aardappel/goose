// error: depth calls into its recursive cycle while the non-fixed-size temporary str(n) (evaluated at
// An if's condition is evaluated in the scope around the if, so a temporary
// it builds lives on through the branches: the call into the cycle in the
// then-block holds it (§7.8).
fn size(s: u8[:]) -> i64 { s.len }
recursive fn depth(n: i64) -> i64 {
    if n == 0 { return 0; }
    var total = 0;
    if size(str(n)) > 0 { total += depth(n - 1); }
    return total;
}
fn main() { print(depth(3)); }
