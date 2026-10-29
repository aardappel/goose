// error: relative reference
struct Node { value: i64, next: Node&<u32>? }
root struct S { first: Node[>..], second: Node[>..] }
fn bad(s: S&) { s.second.push(Node { 2, s.first[0] }); }
fn main() { var s = S { [Node { 1, null }], [] }; bad(s); }
