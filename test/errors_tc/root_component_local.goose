// error: a relative reference's pool must be a global variable
root struct State { items: i32[>..] = [] }
type Link = i32&<u32 in state.items>;
fn main() { var state = State {}; }
