// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place, or bind a reference to it with .=: let a, b .= two(ns)
// A multi-value binding copies the pointee of each reference result as a
// single binding does (§3.8, §3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
fn two(ns: Node[>..]&) -> Node&, Node& { return pair(ns), pair(ns); }
fn main() {
    var ns: Node[>..] = [];
    let a, b = two(ns);
    print(a.v + b.v);
}
