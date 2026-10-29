// error: nested root owners are not supported
root struct State { items: i64[>..] }
struct Wrapper { states: State[] }
fn main() {}
