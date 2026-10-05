// error: operator += cannot be applied to i64[2] and i64[:]
fn main() {
    var a: i64[2] = [1, 2];
    let b = a[..];
    a += b;
}
