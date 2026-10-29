// error: a named array component must be a direct field of a global root owner
root struct State { items: i32[>..] = [] }
var state = State {};
let alias .= state;
type Link = i32&<u32 in alias.items>;
fn main() {}
