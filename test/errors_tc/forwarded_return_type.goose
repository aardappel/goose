// error: expected a value of type i64, got f64
// A call's values forwarded by a return meet the function's return types as
// values of their own would.
fn halves() -> f64, f64 { return 0.5, 1.5; }
fn counts() -> i64, i64 { return halves(); }
fn main() {
    let a, b = counts();
    print(a, " ", b);
}
