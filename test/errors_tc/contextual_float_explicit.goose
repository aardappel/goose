// error: expected a value of type f32, got f64
// An explicit f64 argument fixes the generic computation's width.
import std;
fn main() {
    let value: f32 = max(0.5, min<f64>(0.9, 1.0));
    print(value);
}
