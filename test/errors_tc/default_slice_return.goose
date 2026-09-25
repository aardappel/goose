// error: returning a reference into a temporary
// default<T>() fills an array in a temporary, as a literal is built (§9.2).
fn pick() -> const i64[:] { default<i64[4]>()[..] }
fn main() { print(pick()[0]); }
