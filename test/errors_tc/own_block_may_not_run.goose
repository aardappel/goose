// error: h is consumed on one path through this if
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn maybe<F>(c: bool) { if c { F(); } }
fn main() { let h = make(1); maybe(true) { drop_node(h); }; }
