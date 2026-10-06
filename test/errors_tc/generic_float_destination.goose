// error: expected a value of type f32, got f64
// error: f64 to f32 requires an explicit `as f32` to round the value
// error: this generic call has committed to f64
// Typed f64 operands still commit the nested generic computation to f64.
import std;
fn main() {
    let width: f64 = 1280;
    let height: i32 = 800;
    let scale: f32 = max(0.5, min(width / 960.0, height / 600.0));
    print(scale);
}
