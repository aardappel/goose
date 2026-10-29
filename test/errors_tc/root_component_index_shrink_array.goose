// error: an index type names state.items, so its length never goes down
root struct State { items: i32[>..] = [] }
var state = State {};
type Id = u32 in state.items;
fn reset(a: i32[>..]&) { a.clear(); }
fn main() { state.items.push(1); reset(state.items); }
