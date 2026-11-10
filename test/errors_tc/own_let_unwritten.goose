// error: let b = a does not say what becomes of the own value a holds
// error: write let b: own Node& = a to move it, let b .= a to borrow it, or copy(a) for a copy of what it refers to
// A declaration without a type takes over an own value nothing holds yet,
// and none a variable holds: the move, the borrow and the copy each have a
// spelling (docs/design/ownership.md 4.3).
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn main() { let a = make(1); let b = a; drop_node(b); }
