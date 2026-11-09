// error: h may have been consumed at
// The defer runs at the end of every iteration.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn main() { let h = make(1); for i in 2 { defer drop_node(h); } }
