// error: h was consumed at
// A block is checked as part of the function calling it, which runs it twice.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn twice<F>() { F(); F(); }
fn main() { let h = make(1); twice() { drop_node(h); }; }
