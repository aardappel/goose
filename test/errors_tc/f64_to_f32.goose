// f64 never narrows to f32 implicitly (§6.3).
fn main() {
    var x: f32 = 0.5;
    var y: f64 = 1.0;
    x = y;
    print(x);
}
