// error: h (own Node&) is still held at the end of the function
// An own value is consumed on every path: a borrow does not discharge it.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn main() { let h = make(1); show(h); }
