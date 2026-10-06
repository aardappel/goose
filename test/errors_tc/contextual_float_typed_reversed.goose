// error: expected a value of type f32, got f64
// A typed f64 on the right commits the computation just as one on the left.
import std;
fn main() {
    let wide: f64 = 0.9;
    let value: f32 = max(0.5, min(1.0, wide));
    print(value);
}
