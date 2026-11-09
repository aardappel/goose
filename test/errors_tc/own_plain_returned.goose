// error: expected an own value of type own Node&, got a Node&
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn fake() -> own Node& { return nodes.push(Node { v: 1 }); }
fn main() { drop_node(fake()); }
