// error: cannot clear ns while h is still used: a reference into it was stored there
// h is a copy of an element ns names or a node linking to one of those
// elements: merged with a reference into the storage itself, what h holds
// is no view that storage held (§5.1).
struct N { v: i64, next: N&? }
fn f(ns: N[>..]&, c: bool) -> i64 {
    let h = if c { ns[1] } else { N { v: 0, next: ns[0] } };
    ns.clear();
    let nx = h.next;
    if nx { return nx.v; }
    return -1;
}
fn main() {
    var other = N { v: 7, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: other });
    print(f(nodes, nodes.len > 5));
}
