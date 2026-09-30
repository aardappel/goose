// error: the default of parameter a of f leads to this call, which takes it again
// f's default calls g leaving out b, whose default calls f leaving out a
// (§7.1).
fn f(a: i64 = g()) -> i64 { a }
fn g(b: i64 = f()) -> i64 { b }
fn main() { print(f()); }
