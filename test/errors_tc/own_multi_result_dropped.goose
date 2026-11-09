// error: result 2 of this call is a own Node&, which is dropped here
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn two() -> i64, own Node& { return 1, make(1); }
fn main() { print(two()); }
