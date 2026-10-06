// error: expected a value of type f32, got f64
// Reversing declaration order cannot let context select the f32 overload.
fn choose(value: f32) -> f32 { 17.0 }
fn choose<T>(value: T) -> T { value }
fn main() {
    let value: f32 = choose(0.5);
    print(value);
}
