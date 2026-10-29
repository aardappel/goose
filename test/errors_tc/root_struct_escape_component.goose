// error: dies with this function
root struct S { a: i64[>..], b: i64[>..] }
fn f() -> i64& { var s = S { [1], [2] }; s.a[0] }
fn main() {}
