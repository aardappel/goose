// error: cannot call f: it shrinks nodes while n is still used, and n may refer into nodes
// n is a copy of an element of the array ns names, which the caller linked
// to another of them (§5.1).
struct N { v: i64, next: N&? }
fn f(ns: N[>..]&) -> N {
    let n = ns[1];
    ns.clear();
    return n;
}
fn main() {
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &nodes[0];
    let m = f(nodes);
    nodes.push(N { v: 90, next: null });
    let nx = m.next;
    if nx { print(nx.v); }
}
