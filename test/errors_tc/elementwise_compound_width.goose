// error: operator += cannot be applied to i32[2] and i64[2]
fn main() {
    var a: i32[2] = [1, 2];
    let b: i64[2] = [3, 4];
    a += b;
}
