// error: the result type of pass is inferred, which does not say that the own value h holds moves out
// error: declare it (-> own Node&)
// An inferred result takes over an own value nothing holds yet, and none a
// variable holds.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn pass(h: own Node&) { h }
fn main() { drop_node(pass(make(1))); }
