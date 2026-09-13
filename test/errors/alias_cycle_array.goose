// error: type alias cycle involving: A
type A = A[1];
fn main() { var value: A; }
