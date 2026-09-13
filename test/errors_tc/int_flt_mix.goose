// error: operator + cannot be applied to i64 and f64
// No implicit i64/f64 mixing in expressions (§6.3).
fn main() {
    var integer: i64 = 1;
    var floating: f64 = 2.0;
    let x = integer + floating;
    print(x);
}
