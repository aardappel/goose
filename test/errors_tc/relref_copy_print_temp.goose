// error: copying a value of type Node, which contains self-relative references, is not supported: its links, read out of this copy, would be measured from the copy; read them where the value is stored
// print renders a reference as its pointee (§3.7), so it would follow the
// popped copy's link from the copy (§3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn main() {
    var ns: Node[>..] = [];
    pair(ns);
    print(ns.pop());
}
