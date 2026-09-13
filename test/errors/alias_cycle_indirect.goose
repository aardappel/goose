// error: type alias cycle involving: B
type A = B[:];
type B = A;
fn main() { var value: A; }
