// error: h (own Node&) is still held at this return, which leaves outer and the calls between
// A return ... from leaves every function on the way: claim h with a defer.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn deep(n: i64) -> i64 { if n > 2 { return -1 from outer; } n }
fn outer(n: i64) -> i64 { let h = make(n); let v = deep(n); drop_node(h); return v; }
fn main() { print(outer(1)); }
