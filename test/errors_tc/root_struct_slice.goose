// error: nested root owners are not supported
root struct State { count: i64 }
struct Wrapper { states: State[:] }
fn main() {}
