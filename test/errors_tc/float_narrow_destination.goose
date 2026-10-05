// error: expected a value of type f32, got f64
// error: f64 to f32 requires an explicit `as f32` to round the value
// Even a typed let initialized from a literal commits its f64 type.
fn main() {
    let source: f64 = 0.5;
    let narrowed: f32 = source;
    print(narrowed);
}
