// error: type alias cycle involving: A
type A = fn(A) -> i64;
fn main() { }
