// error: build calls into its recursive cycle through build, whose non-fixed-size result would land in a temporary here
// A block's body runs inside the function calling it, as part of the
// statement making that call: a call into the cycle there whose result an
// operand takes lands in a temporary, held across the call (§7.8), as it
// would written in build itself.
fn apply<F>(n: i64) -> i64 { F(n) }
recursive fn build(n: i64) -> u8[] {
    if n == 0 { return "x"; }
    let k = apply(n) { m => build(m - 1).len };
    return str(k);
}
fn main() { print(build(3)); }
