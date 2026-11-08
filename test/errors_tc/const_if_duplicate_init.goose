// error: duplicate initializer for field x
struct S { x: i64, y: i64 }
fn main() { print(S { x: 1, y: 2, const if true { x: 3, } }); }
