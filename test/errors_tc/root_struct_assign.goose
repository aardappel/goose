// error: a root owner cannot be assigned as a whole
root struct S { a: i64[>..] }
fn main() { var s = S { .. }; s = S { .. }; }
