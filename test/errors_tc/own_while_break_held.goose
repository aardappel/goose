// error: cur (own Node?) is still held
// A while that ends on its condition found the optional null; one left by a
// break may still hold it.
struct Node { v: i64, next: own Node? }
var nodes: Node[>..] = [];
var live = 0;
trusted fn make(v: i64, next: own Node?) -> own Node& {
    live += 1;
    return nodes.push(Node { v: v, next: next });
}
trusted fn free_node<F>(h: own Node&) { F(h); live -= 1; }
fn main() {
    var cur: own Node? = make(1, make(2, null));
    while cur {
        if cur.v == 2 { break; }
        var rest: own Node? = null;
        free_node(cur) { rest .= take(it.next); };
        cur .= rest;
    }
}
