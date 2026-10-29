// error: nested root owners are not supported
root struct State {}
struct Wrapper { states: State[0] }
fn main() {}
