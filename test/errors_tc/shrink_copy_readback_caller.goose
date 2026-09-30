// error: cannot call follow: it shrinks nodes while nx is still used, and nx may refer into nodes
// nx is read out of a copy of the element ns names, which the caller
// linked to another element of nodes (§5.1).
struct N { v: i64, next: N&? }
fn follow(ns: N[>..]&) {
    let n = ns[1];
    let nx = n.next;
    ns.clear();
    if nx { print(nx.v); }
}
fn main() {
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &nodes[0];
    follow(nodes);
}
