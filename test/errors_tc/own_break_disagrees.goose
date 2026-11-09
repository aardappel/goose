// error: h is consumed on one path through this loop
// The break leaves with h consumed, the end of the loop with it held.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn main() { let h = make(1); for i in 3 { if i == 1 { drop_node(h); break; } } }
