// error: a named array component must be mutable
root struct State { let items: i32[>..] = [] }
var state = State {};
type Id = u32 in state.items;
fn main() {}
