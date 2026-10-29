// error: root owners cannot be copied
root struct S { a: i64[>..] }
fn main() { var s = S { .. }; let t = copy(s); }
