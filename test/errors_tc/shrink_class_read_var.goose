// error: cannot clear ns while r is still used
// r, read out of the elements ns names, may be rebound to one of them
// (§5.1).
struct N { v: i64, next: N&? }
fn f(ns: N[>..]&, c: bool, keep: N&) -> N& {
    let nx = ns[1].next;
    if nx {
        var r: N& = nx;
        if c { r .= &ns[1]; }
        ns.clear();
        return r;
    }
    return keep;
}
fn main() {
    var other = N { v: 7, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &other;
    let r: N& = f(nodes, true, other);
    nodes.push(N { v: 90, next: null });
    nodes.push(N { v: 91, next: null });
    print(r.v);
}
