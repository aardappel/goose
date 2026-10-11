// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place
// An array literal's element is a destination like any other (§4.2, §3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn main() {
    var ns: Node[>..] = [];
    let a: Node[1] = [pair(ns)];
    print(a[0].v);
}
