// error: a relative reference's pool must be a var
root struct State { items: i32[>..] = [] }
let state = State {};
type Link = i32&<u32 in state.items>;
fn main() {}
