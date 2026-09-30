// error: cannot clear ns while nx is still used
// follow itself linked the element n is a copy of to another element
// (§5.1).
struct N { v: i64, next: N&? }
fn follow(ns: N[>..]&) {
    ns[1].next .= &ns[0];
    let n = ns[1];
    let nx = n.next;
    ns.clear();
    if nx { print(nx.v); }
}
fn main() {
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    follow(nodes);
}
