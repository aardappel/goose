// A trailing block's value may view what outlives the block: the caller's
// storage behind a reference parameter, a slice argument's target, or a
// temporary of the calling statement (§9.2).
fn observe(xs: const i64[:], n: i64) -> i64 { xs[0] + n }
fn churn() -> i64 {
    var fill: i64[>..] = [99, 98];
    fill.len - 2
}
fn apply<F>() -> i64 { observe(F([7, 8]), churn()) }
fn over<F>(v: i64[>..]&) -> i64 { observe(F(v), churn()) }
fn outer<F>(v: i64[:]) -> i64 { observe(F(), churn()) + v.len }
fn main() {
    print(apply() { xs: i64[:] => xs[1..] });
    var keep: i64[>..] = [5, 6];
    print(over(keep) { v => v[..] });
    let held: i64[>..] = [3, 4, 5];
    print(outer(held) { held[2..] });
}
