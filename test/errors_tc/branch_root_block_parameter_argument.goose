// error: the block's value is a slice rooted at xs, which does not outlive it
// A trailing block's by-value parameter ends with the block, before the
// call its value is passed to: churn() would reuse its data stack.
fn observe(xs: const i64[:], n: i64) -> i64 { xs[0] + n }
fn churn() -> i64 {
    var fill: i64[>..] = [99, 98];
    fill.len - 2
}
fn apply<F>() -> i64 { observe(F([7, 8]), churn()) }
fn main() {
    print(apply() { xs: i64[>..] => xs[..] });
}
