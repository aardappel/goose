// error: branches disagree: one gives an own value (own Node&), which moves
struct Node { v: i64 }
var nodes: Node[>..] = [];
trusted fn make(v: i64) -> own Node& { return nodes.push(Node { v: v }); }
trusted fn drop_node(h: own Node&) {}
fn show(n: Node&) { print(n.v); }
fn pick(c: bool) {
    let n .= nodes[0];
    let x = if c { make(1) } else { n };
    show(x);
}
fn main() { pick(true); }
