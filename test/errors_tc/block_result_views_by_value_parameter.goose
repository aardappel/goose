// error: xs[..] views storage of its own block, which ends with it
// A block's by-value parameter is storage of the block's own activation, so a
// slice of it cannot be the block's result: the caller would read the storage
// after the block returns (§9.2, §7.6). Taking the parameter by reference is
// allowed, since it is rooted at the caller's storage instead.
fn observe(xs: const i64[:], n: i64) -> i64 { xs[0] + n }
fn churn() -> i64 { var fill: i64[>..] = [99, 98]; fill.len - 2 }
fn apply<F>() -> i64 { observe(F([7, 8]), churn()) }
fn main() { print(apply() { xs: i64[>..] => xs[..] }); }
