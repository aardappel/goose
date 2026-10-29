// error: still used
root struct S { a: i64[>..<], b: i64[>..<] }
fn clear(a: i64[>..<]&) { a.clear(); }
fn main() { var s = S { [1], [2] }; let r .= s.a[0]; clear(s.a); print(r); }
