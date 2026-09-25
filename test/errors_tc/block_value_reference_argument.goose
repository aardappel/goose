// error: no matching overload for call to id
// A block's value is a copy (§9.2), no storage for a reference parameter to
// bind (§4.1).
var g: i64[3] = [1, 2, 3];
fn id(x: i64[3]&) -> i64[3]& { x }
fn pick() -> i64[3]& { id(block { g }) }
fn main() { print(pick()[0]); }
