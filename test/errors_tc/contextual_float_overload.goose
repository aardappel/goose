// error: expected a value of type f32, got f64
// Context must not replace a selected generic function with an f32 overload.
fn choose<T>(value: T) -> T { value }
fn choose(value: f32) -> f32 { 17.0 }
fn main() {
    let value: f32 = choose(0.5);
    print(value);
}
