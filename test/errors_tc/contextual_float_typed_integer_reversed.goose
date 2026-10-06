// error: expected a value of type f32, got f64
// Nor may reversing those arguments make context accept the computation.
import std;
fn main() {
    var integer: i32 = 1;
    let value: f32 = min(max(0.25, 0.5), integer);
    print(value);
}
