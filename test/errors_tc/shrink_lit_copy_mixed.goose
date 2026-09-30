// error: cannot clear ns while q is still used: a reference into it was stored there
// q holds a copy of an element ns names and a reference to another of
// those elements: beside that reference, what q holds is no view the
// storage held (§5.1).
struct N { v: i64, next: N&? }
struct Q { n: N, at: N& }
fn f(ns: N[>..]&) -> i64 {
    let q = Q { n: ns[1], at: ns[0] };
    ns.clear();
    return q.at.v;
}
fn main() {
    var other = N { v: 7, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: other });
    print(f(nodes));
}
