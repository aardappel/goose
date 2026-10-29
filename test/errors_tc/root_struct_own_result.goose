// error: return a reference instead
root struct S { n: i64 }
fn f() -> S { S { 1 } }
fn main() {}
