// error: still used
root struct S { a: i64[>..<], b: i64[>..<] }
fn main() { var s = S { [1], [2] }; let r .= s.a[0]; s.a.clear(); print(r); }
