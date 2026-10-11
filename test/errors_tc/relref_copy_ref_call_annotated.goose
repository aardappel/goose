// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place
// A declaration annotated with the pointee type asks for the copy, which the
// links do not survive (§3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn main() {
    var ns: Node[>..] = [];
    let n: Node = pair(ns);
    print(n.v);
}
