// error: cannot call source, which shrinks a
var a: i64[>..] = [1, 2];
var b: i64[>..] = [3, 4];
fn source() -> i64[:] { a.clear(); b[..] }
fn main() { a[..].move_from(source()); }
