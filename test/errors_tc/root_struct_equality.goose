// error: root owners do not support value operators
root struct S { n: i64 }
fn main() { var a = S { 1 }; var b = S { 1 }; print(a == b); }
