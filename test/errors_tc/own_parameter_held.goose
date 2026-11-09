// error: h (own Node&) is still held at the end of the function
// A parameter declared own is the callee's to consume, unless the callee
// is a trusted fn, where its claim ends.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn eat(h: own Node&) { show(h); }
fn main() { eat(make(1)); }
