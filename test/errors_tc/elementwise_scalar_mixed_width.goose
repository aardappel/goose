// error: operator * cannot be applied to Mixed and i64
struct Mixed { x: f32, y: f64 }
fn main() {
    print(Mixed { 1.0, 2.0 } * 2);
}
