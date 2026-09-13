// error: struct X has no variable mode (.. applies to enums)
struct X { a: i64 }
fn f(x: X..) { }
fn main() { }
