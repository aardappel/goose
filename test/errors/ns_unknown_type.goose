// A qualified type name can only be a declaration, so an unknown one is an
// error rather than a generic type parameter.
struct a::Point { x: i64 }
fn f(p: a::Pointt) -> i64 { 1 }
fn main() {}
