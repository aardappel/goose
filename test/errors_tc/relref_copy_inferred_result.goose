// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place
// A result type inferred from a return takes it as an un-annotated `let`
// does (§7.1): an element of a fixed-size type is copied out of its array
// (§3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn first(ns: Node[>..]&) { return ns[1]; }
fn main() {
    var ns: Node[>..] = [];
    pair(ns);
    print(first(ns).v);
}
