// `return ... from` names a function the returning function's context can see.
fn a::deep() -> i64 { return -1 from nope; }
fn main() { print(a::deep()); }
