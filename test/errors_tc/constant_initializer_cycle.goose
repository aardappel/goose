// error: cycle in constant initializer: A
let A = B;
let B = A;
struct Data { values: i64[A] }
fn main() { }
