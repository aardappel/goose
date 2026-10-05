// error: operator += cannot be applied to Mixed
struct Mixed { a: i64, b: f64 }
fn main() {
    var a = Mixed { 1, 2.0 };
    a += Mixed { 3, 4.0 };
}
