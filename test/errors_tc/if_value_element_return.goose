// error: returning a reference into a temporary
// An element of an if's value is an element of its temporary copy (§9.2).
var g: i64[3] = [1, 2, 3];
fn pick(c: bool) -> i64& { (if c { g } else { g })[0] }
fn main() { print(pick(true)); }
