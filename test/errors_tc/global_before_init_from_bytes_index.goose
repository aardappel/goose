// error: from_bytes<Box<Box<u32 in nodes>>[>..]> verifying u32 in nodes checks against nodes.len, which reads nodes before it is assigned
// from_bytes verifies each index in the image against its array's length
// (§3.11), however deep in the element it is, which reads nodes before its
// initializer has run (§11.1).
type Id = u32 in nodes;
struct Box<T> { value: T }
fn load<T>() -> bool {
    let image: u8[5] = [4, 0, 0, 0, 0];
    let values, ok = from_bytes<T[>..]>(image);
    ok
}
var first = load<Box<Box<Id>>>();
var nodes: i64[>..] = [10];
fn main() { print(first); }
