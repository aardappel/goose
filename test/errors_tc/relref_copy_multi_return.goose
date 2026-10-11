// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place
// Forwarding a multi-value call's reference results as values copies their
// pointees (§7.3, §3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn two(ns: Node[>..]&) -> Node&, Node& { return pair(ns), pair(ns); }
fn both(ns: Node[>..]&) -> Node, Node { return two(ns); }
fn main() {
    var ns: Node[>..] = [];
    let a, b = both(ns);
    print(a.v + b.v);
}
