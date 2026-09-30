// error: cannot clear nodes while boxes is still used: a reference into it was stored there
// put stores r, which may be h, a reference into the elements ns names, as
// well as what an element holds: r was read out of ns's storage only in
// part, so what boxes holds is not only what nodes' elements do (§5.1).
struct N { v: i64, next: N&? }
struct Box { n: N& }
fn put(dst: Box[:], ns: N[>..]&, c: bool) {
    let h = &ns[1];
    let nx = h.next;
    if nx {
        let r: N& = if c { h } else { nx };
        dst[0] = Box { n: r };
    }
}
fn main() {
    var other = N { v: 7, next: null };
    var nodes: N[>..] = [];
    nodes.push(N { v: 1, next: null });
    nodes.push(N { v: 2, next: null });
    nodes[1].next .= &other;
    var boxes: Box[>..] = [];
    boxes.push(Box { n: other });
    put(boxes[..], nodes, true);
    nodes.clear();
    nodes.push(N { v: 90, next: null });
    nodes.push(N { v: 91, next: null });
    print(boxes[0].n.v);
}
