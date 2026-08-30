// Elementwise math needs uniformly int or float leaves (§6.1).
struct Mixed { a: int, b: flt }
fn main() {
    let x = Mixed { 1, 2.0 };
    let y = Mixed { 3, 4.0 };
    let z = x + y;
    print(z.a);
}
