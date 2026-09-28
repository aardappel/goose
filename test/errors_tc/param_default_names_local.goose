// error: the default of parameter y of helper names k, a local where helper is declared
// A nested function's defaults name what a top-level declaration can (§7.1),
// not the variables around the declaration.
fn outer(k: i64) -> i64 {
    fn helper(x: i64, y: i64 = k) -> i64 { x + y }
    return helper(1);
}
fn main() { print(outer(2)); }
