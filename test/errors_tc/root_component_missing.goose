// error: root struct State has no field missing
root struct State { items: i32[>..] = [] }
var state = State {};
type Id = u32 in state.missing;
fn main() {}
