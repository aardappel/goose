// error: cannot clear ns while nx is still used
// n is a copy of an element ns names, then maybe linked to another of those
// elements: beside that link, what n holds is no view the storage held
// (§5.1).
struct N { v: i64, next: N&? }
fn follow(ns: N[>..]&, c: bool) {
    var n = ns[1];
    if c { n.next .= &ns[0]; }
    let nx = n.next;
    ns.clear();
    if nx { print(nx.v); }
}
fn main() {
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    follow(nodes, nodes.len < 5);
}
