// error: operator * cannot be applied to Mixed and i64
struct Mixed { x: f32, y: i32 }
fn main() {
    print(Mixed { 1.0, 2 } * 2);
}
