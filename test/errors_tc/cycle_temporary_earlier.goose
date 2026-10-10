// error: depth calls into its recursive cycle while the non-fixed-size temporary str(n) (evaluated at
// A temporary lives to the end of its statement, so str(n), evaluated
// before the call into the cycle in the same statement, would hold a data
// stack of every activation across it (§7.8).
fn size(s: u8[:], k: i64) -> i64 { s.len + k }

recursive fn depth(n: i64) -> i64 {
    if n == 0 { return 0; }
    return size(str(n), depth(n - 1));
}

fn main() {
    print(depth(3));
}
