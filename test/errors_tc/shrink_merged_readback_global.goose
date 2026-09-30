// error: cannot clear NODES: global G may hold a reference into it
// put stores r, which may be h, a reference into the elements ns names, as
// well as what an element holds, into G: r was read out of NODES only in
// part (§5.1).
struct N { v: i64, next: N&? }
struct Box { n: N& }
var OTHER = N { v: 7, next: null };
var NODES: N[>..] = [];
var G = Box { n: OTHER };
fn put(ns: N[>..]&, c: bool) {
    let h = &ns[1];
    let nx = h.next;
    if nx {
        let r: N& = if c { h } else { nx };
        G = Box { n: r };
    }
}
fn main() {
    NODES.push(N { v: 1, next: null });
    NODES.push(N { v: 2, next: null });
    NODES[1].next .= &OTHER;
    put(NODES, true);
    NODES.clear();
    NODES.push(N { v: 90, next: null });
    NODES.push(N { v: 91, next: null });
    print(G.n.v);
}
