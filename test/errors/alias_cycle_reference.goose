// error: type alias cycle involving: A
type A = A?;
fn main() { var value: A = null; }
