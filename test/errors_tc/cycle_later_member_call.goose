// error: a calls into its recursive cycle while non-fixed-size local tmp
// By the second call b is a finished member of a's cycle: no back edge, but
// a call leading back into the cycle all the same, with tmp in scope (§7.8).
recursive fn a(n: i64) -> i64 {
    if n <= 0 { return 0; }
    let first = b(n);
    var tmp: i64[>..] = [];
    tmp.push(first);
    return b(n - 1) + tmp[0];
}
fn b(n: i64) -> i64 { return a(n - 1); }
fn main() { print(a(3)); }
