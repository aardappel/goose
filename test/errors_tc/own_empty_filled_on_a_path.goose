// error: h (own Node?) is still held at the end of the function
// A variable that began empty and was given a value on one path may hold one.
struct Node { v: i64, next: own Node? }
var nodes: Node[>..] = [];
var live = 0;
trusted fn make(v: i64, next: own Node?) -> own Node& {
    live += 1;
    return nodes.push(Node { v: v, next: next });
}
trusted fn free_node<F>(h: own Node&) { F(h); live -= 1; }
fn main() {
    var h: own Node? = null;
    if live == 0 { h .= make(1, null); }
}
