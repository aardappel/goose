// error: copy of a own Window, which holds an own value
// A struct that is itself the claim is borrowed through a reference, never copied.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
struct Window { id: u64 }
trusted fn open() -> own Window { return Window { id: 1 }; }
trusted fn close(w: own Window) {}
fn main() { let w = open(); let c = copy(w); close(w); }
