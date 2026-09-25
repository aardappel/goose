// error: returning a reference into a temporary
// A match's value is a copy of what the arm taken produced (§9.2).
var g: i64[3] = [1, 2, 3];
fn pick(k: i64) -> const i64[:] { (match k { 0 => g, _ => g })[..] }
fn main() { print(pick(0)[0]); }
