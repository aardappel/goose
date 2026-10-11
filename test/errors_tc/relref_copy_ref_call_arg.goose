// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place
// A by-value parameter copies the pointee of a reference-returning call
// (§4.1, §3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn value(n: Node) -> i64 { return n.v; }
fn main() {
    var ns: Node[>..] = [];
    print(value(pair(ns)));
}
