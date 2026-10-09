// error: storing a reference rooted at mine, which does not outlive the destination
// A function nothing calls is still checked (§3.9): whatever its parameter
// points into, a link to its own local cannot go into the pool.
struct Node {
    id: i64,
    prev: Node&<u32 in nodes>,
    next: Node&<u32 in nodes>,
}

reusable var nodes: Node[>..] = [];

fn bad(n: Node&) {
    var mine: Node[>..] = [];
    let m .= mine.push(Node { id: 1, prev: n.prev, next: n.next });
    n.next .= m;
}

fn main() {
    let h .= nodes.alloc_ref(Node { id: 0, prev: self, next: self });
    print(h.id);
}
