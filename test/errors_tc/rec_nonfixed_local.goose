// error: descend calls into its recursive cycle while non-fixed-size local scratch
// A non-fixed local of a recursive cycle's function may not be in scope where
// it calls back into the cycle: every activation would keep its own data
// stack (§7.8).
recursive fn descend(depth: i64) -> i64 {
    var scratch: i64[>..] = [];
    scratch.push(depth);
    if depth == 0 { return scratch[0]; }
    return scratch[0] + descend(depth - 1);
}
fn main() { print(descend(3)); }
