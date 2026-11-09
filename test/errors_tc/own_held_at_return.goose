// error: h (own Node&) is still held at this return
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn f(c: bool) { let h = make(1); if c { return; } drop_node(h); }
fn main() { f(true); }
