// error: expected a value of type f32, got f64
// A float of literals and integers is an f32 where it is written for an f32
// (§6.3), and so is an untyped `let` bound to one; a `let` annotated f64
// commits to f64, which narrows only with an explicit `as`.
fn main() {
    var n: i32 = 3;
    let a: f32 = n * 0.5;
    let h = n * 0.5;
    let g: f64 = n * 0.5;
    let b: f32 = h;
    let e: f32 = g;
    print(a, " ", b, " ", e);
}
