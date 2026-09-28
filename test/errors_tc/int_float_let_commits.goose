// error: expected a value of type f32, got f64
// A float of literals and integers is an f32 where it is written for an f32
// (§6.3); bound to a `let` with no type it is an f64, which narrows only with
// an explicit `as`.
fn main() {
    var n: i32 = 3;
    let a: f32 = n * 0.5;
    let h = n * 0.5;
    let b: f32 = h;
    print(a, " ", b);
}
