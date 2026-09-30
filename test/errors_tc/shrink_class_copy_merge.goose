// error: cannot clear ns while b is still used: a reference into it was stored there
// b keeps r, which may be h, a reference into the elements ns names, as
// well as the node the element links to: merged with the storage's own
// element, r is no view the storage held (§5.1).
struct N { v: i64, next: N&? }
struct B { n: N& }
fn f(ns: N[>..]&, c: bool, keep: N&) -> B {
    let h = &ns[1];
    let nx = h.next;
    if nx {
        let r: N& = if c { h } else { nx };
        let b = B { n: r };
        ns.clear();
        return b;
    }
    return B { n: keep };
}
fn main() {
    var other = N { v: 7, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &other;
    let b = f(nodes, true, other);
    nodes.push(N { v: 90, next: null });
    nodes.push(N { v: 91, next: null });
    print(b.n.v);
}
