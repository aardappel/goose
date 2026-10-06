// error: expected a value of type f32, got f64
// Storing a generic call result commits it, even in an unannotated let.
import std;
fn main() {
    let stored = min(0.9, 1.0);
    let value: f32 = max(0.5, stored);
    print(value);
}
