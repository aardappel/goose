// error: returning a reference into a temporary
// An if's value is a copy of what the branch taken produced, in temporary
// storage of its own, even where that branch names a variable: a slice of
// it dies with the function (§9.2).
var g: i64[3] = [1, 2, 3];
fn pick(c: bool) -> const i64[:] { (if c { g } else { g })[..] }
fn main() { print(pick(true)[0]); }
