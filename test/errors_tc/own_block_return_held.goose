// error: h (own Node&) is still held at this return, which leaves f and the calls between
// A block's return leaves the function it is written in, past its locals.
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn once<F>() { F(); }
fn f() { let h = make(1); once() { return; }; drop_node(h); }
fn main() { f(); }
