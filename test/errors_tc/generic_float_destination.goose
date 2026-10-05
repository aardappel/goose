// error: expected a value of type f32, got f64
// error: f64 to f32 requires an explicit `as f32` to round the value
// error: generic calls infer types from their arguments, not this destination
// error: before a nested call commits an f64 result
// The destination must not change generic specialization or its arithmetic.
// generic_float_arguments.goose checks both repairs suggested here.
import std;
fn main() {
    let width: i32 = 1280;
    let height: i32 = 800;
    let scale: f32 = max(0.5, min(width / 960.0, height / 600.0));
    print(scale);
}
