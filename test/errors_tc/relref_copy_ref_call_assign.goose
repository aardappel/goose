// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place
// Assignment copies the pointee of a reference-returning call (§4.4, §3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn main() {
    var ns: Node[>..] = [];
    var n = Node { v: 0 };
    n = pair(ns);
    print(n.v);
}
