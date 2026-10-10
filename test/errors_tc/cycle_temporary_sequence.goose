// error: depth calls into its recursive cycle while the non-fixed-size temporary make(n) (evaluated at
// A for loop walks its sequence in place for all of its body, so a
// temporary sequence is held across the call into the cycle the body makes
// (§7.8): iterate scratch owned outside the cycle instead.
fn make(k: i64) -> i64[>..] { return [k, k + 1]; }
recursive fn depth(n: i64) -> i64 {
    if n == 0 { return 0; }
    var total = 0;
    for x in make(n) { total += x + depth(n - 1); }
    return total;
}
fn main() { print(depth(3)); }
