// error: dies with this function
root struct S { a: i64[>..] }
fn bad() -> S& { var s = S { [1] }; s }
fn main() { let s = bad(); print(s.a[0]); }
