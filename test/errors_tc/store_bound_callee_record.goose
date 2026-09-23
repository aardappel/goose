// error: cannot clear arr while n is still used: a reference into it was stored there
// f stores into the node its holder points at, not into the holder: the call
// puts the store on record for n, which the shrink then finds (§5.1).
struct Node { p: i64? }
struct Holder { node: Node& }

fn f(h: Holder&, x: i64&) { h.node.p .= x; }

fn main() {
    var arr: i64[>..] = [1, 2, 3];
    var n = Node { p: null };
    var h = Holder { node: n };
    f(h, arr[0]);
    arr.clear();
    if n.p { print(n.p); }
}
