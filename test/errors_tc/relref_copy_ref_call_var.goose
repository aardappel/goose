// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place, or bind a reference to it with .=: var n .= pair(ns)
// A `var` copies the pointee of a reference-returning call as a `let` does
// (§3.8, §3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn main() {
    var ns: Node[>..] = [];
    var n = pair(ns);
    print(n.v);
}
