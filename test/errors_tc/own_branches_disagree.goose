// error: h is consumed on one path through this if
// The paths joining after a branch must agree: consumed on each, or on none.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn main() { let h = make(1); if h.v > 0 { drop_node(h); } }
