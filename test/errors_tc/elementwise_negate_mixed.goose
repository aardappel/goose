// error: cannot negate a value of type Mixed
// An aggregate negates only where its leaves are all floats or all integers,
// as for its binary arithmetic.
struct Mixed { a: i32, b: f32 }
fn main() {
    let m = Mixed { a: 1, b: 2.0 };
    let n = -m;
    print(n.a);
}
