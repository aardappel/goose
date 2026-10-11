// error: copying a value of type Pair, which contains self-relative references, is not supported; construct it in place, or bind a reference to it with .=: let p .= last(ps)
// A struct holding a linked value by value holds its links (§3.9).
struct Node { next: Node&<u32>?, v: i64 }
fn pair(ns: Node[>..]&) -> Node& {
    let a .= ns.push(Node { v: 1 });
    return ns.push(Node { next: a, v: 2 });
}
struct Pair { n: Node, k: i64 }
fn last(ps: Pair[>..]&) -> Pair& { return ps[ps.len - 1]; }
fn main() {
    var ps: Pair[>..] = [];
    ps.push(Pair { n: Node { v: 1 }, k: 2 });
    let p = last(ps);
    print(p.k);
}
