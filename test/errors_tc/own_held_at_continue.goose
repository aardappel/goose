// error: h (own Node&) is still held at this continue
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn main() { for i in 3 { let h = make(i); if i == 1 { continue; } drop_node(h); } }
