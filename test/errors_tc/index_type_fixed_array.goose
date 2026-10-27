// error: the array of an index type must be a grow-only resizable global var (nodes: T[>..]), not i32[3]
// A fixed array's length never goes down either, but only a grow-only
// resizable array is an index type's (docs/design/index_types.md leaves fixed
// arrays to later): the type decides it, a var of i32[3] being rejected
// alike, and the message names it.
let nodes: i32[3] = [1, 2, 3];
fn main() {
    let w = 0 as (u32 in nodes);
    print(w);
}
