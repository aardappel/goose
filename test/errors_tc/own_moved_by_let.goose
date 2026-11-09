// error: a was consumed at
// A declaration without a type takes an own value over.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn main() { let a = make(1); let b = a; show(a); drop_node(b); }
