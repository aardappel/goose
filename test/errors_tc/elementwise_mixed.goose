// Elementwise math needs uniformly i64 or float leaves (§6.1).
struct Mixed { a: i64, b: f64 }
fn main() {
    let x = Mixed { 1, 2.0 };
    let y = Mixed { 3, 4.0 };
    let z = x + y;
    print(z.a);
}
