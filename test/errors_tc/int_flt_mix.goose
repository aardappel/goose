// No implicit i64/f64 mixing in expressions (§6.3).
fn main() {
    let x = 1 + 2.0;
    print(x);
}
