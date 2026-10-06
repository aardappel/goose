// error: expected a value of type f32, got f64
// An outer call cannot propagate f32 through an overload boundary either.
import std;
fn choose<T>(value: T) -> T { value }
fn choose(value: f32) -> f32 { 17.0 }
fn main() {
    let value: f32 = max(0.25, choose(0.5));
    print(value);
}
