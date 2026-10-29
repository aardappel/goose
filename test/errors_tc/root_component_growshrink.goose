// error: the array of an index type must be a grow-only resizable global var
root struct State { items: i32[>..<] = [] }
var state = State {};
type Id = u32 in state.items;
fn main() {}
