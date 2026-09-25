// error: returning a reference into a temporary
// An if whose other branch never produces is a copy of its value all the
// same (§9.2).
var g: i64[3] = [1, 2, 3];
fn pick(c: bool) -> const i64[:] { (if c { g } else { abort("no") })[..] }
fn main() { print(pick(true)[0]); }
