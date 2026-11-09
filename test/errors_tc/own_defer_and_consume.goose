// error: h is consumed here and again by the defer
// A defer that consumes a variable claims it for every exit of its block.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn main() { let h = make(1); defer drop_node(h); drop_node(h); }
