// error: nested root owners are not supported
root struct State { count: i64 }
enum Choice { Empty, Owned { state: State } }
fn main() {}
