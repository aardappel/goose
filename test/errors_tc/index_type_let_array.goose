// error: the array of an index type must be a grow-only resizable global var
// A let global is not a var. (docs/design/index_types.md)
let nodes: i32[>..] = [1, 2, 3];
fn main() {
    let w = 0 as (u32 in nodes);
    print(w);
}
