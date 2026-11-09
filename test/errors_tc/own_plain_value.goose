// error: cannot pass Node& as own Node&: a plain value carries no obligation
// Only a function declared to return an own value, or a trusted fn, makes one.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn main() { let n .= nodes.push(Node { v: 1 }); drop_node(n); }
