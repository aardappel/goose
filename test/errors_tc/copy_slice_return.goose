// error: returning a reference into a temporary
// copy(x) is a fresh value in a temporary, not x (§4.2, §9.2).
var g: i64[3] = [1, 2, 3];
fn pick() -> const i64[:] { copy(g)[..] }
fn main() { print(pick()[0]); }
