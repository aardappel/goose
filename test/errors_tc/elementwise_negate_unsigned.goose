// error: cannot negate a value of type Mixed, which holds the unsigned type u8
struct Mixed { a: i32, b: u8 }
fn main() {
    let m = Mixed { a: 1, b: 2 };
    let n = -m;
    print(n.a);
}
