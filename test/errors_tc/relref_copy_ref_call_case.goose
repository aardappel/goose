// error: copying a value of type Node, which contains self-relative references, is not supported; construct it in place, or bind a reference to it with .=: let n .= pick(k, ns)
// Case functions returning a reference are a reference-returning call
// (§8.2, §3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
enum Kind { A, B }
fn pick(k: Kind.A, ns: Node[>..]&) -> Node& { return pair(ns); }
fn pick(k: Kind.B, ns: Node[>..]&) -> Node& { return ns.push(Node { v: 3 }); }
fn main() {
    var ns: Node[>..] = [];
    let k: Kind = Kind.A;
    let n = pick(k, ns);
    print(n.v);
}
