// error: cannot call f: it shrinks nodes while n is still used, and n may refer into nodes
// n was read out of the elements ns names, and the caller linked it to
// another of them (§5.1).
struct N { v: i64, next: N&? }
fn f(ns: N[>..]&, keep: N&) -> N& {
    let n = ns[1].next;
    if n {
        ns.clear();
        return n;
    }
    return keep;
}
fn main() {
    var other = N { v: 7, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &nodes[0];
    let n: N& = f(nodes, other);
    nodes.push(N { v: 90, next: null });
    print(n.v);
}
