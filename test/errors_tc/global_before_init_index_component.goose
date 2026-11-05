// error: converting to u32 in state.items checks against state.items.len, which reads state before it is assigned
// error: so state has no value until its initializer at
// An index type of a root owner's component reads the owner, whose
// initializer has not run when first's does (§3.11, §11.1).
root struct State { items: i32[>..] = [] }
type Id = u32 in state.items;
fn convert(n: i64) -> Id { n as Id }
var first = convert(0);
var state = State {};
fn main() { print(first); }
