// error: a named array component must be a direct field of a global root owner
struct State { items: i32[>..] }
var state = State { [] };
type Id = u32 in state.items;
fn main() {}
