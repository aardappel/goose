// error: show calls into its recursive cycle while initializing non-fixed-size local inner
// A non-fixed local is built in place, so its data stack is taken before
// the call into the cycle in its initializer runs (§7.8).
recursive fn show(n: i64) -> u8[] {
    if n == 0 { return str("x"); }
    let inner = show(n - 1);
    return str("(", inner, ")");
}
fn main() { print(show(3)); }
