// error: build calls into its recursive cycle through build, whose non-fixed-size result would land in a temporary here
// A block whose last construct supplies no value on some path ends in a
// statement, whose value is dropped: the result of the call into the cycle
// there lands in a temporary, held across the call (§7.8).
fn each<F>(n: i64) { F(n); }
recursive fn build(n: i64) -> u8[] {
    if n == 0 { return "x"; }
    each(n) { m => if m > 1 { build(m - 1) } };
    return str(n);
}
fn main() { print(build(3)); }
