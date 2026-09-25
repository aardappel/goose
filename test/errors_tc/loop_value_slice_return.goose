// error: returning a reference into a temporary
// A loop's value is a copy of what its break names (§9.2).
var g: i64[3] = [1, 2, 3];
fn pick() -> const i64[:] { (loop { break g; })[..] }
fn main() { print(pick()[0]); }
