// error: returning a reference into a temporary
// A bare block's value is a copy of its tail's, in a temporary of its own
// (§9.2).
var g: i64[3] = [1, 2, 3];
var picks = 0;
fn pick() -> const i64[:] { ({ picks += 1; g })[..] }
fn main() { print(pick()[0]); }
