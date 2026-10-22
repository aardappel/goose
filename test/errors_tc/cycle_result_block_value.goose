// error: apply calls into its recursive cycle inside the block it calls here, whose non-fixed-size value would land in a temporary
// A block's value is built where its call's value goes, which is taken
// before the block's body runs: here a temporary of apply's statement, held
// across the call into the cycle the body makes (§7.8).
fn apply<F>(n: i64) -> i64 { F(n).len }
recursive fn count(n: i64) -> i64 {
    if n == 0 { return 0; }
    return apply(n) { m => let j = count(m - 1); str(j) };
}
fn main() { print(count(12)); }
