// A function nothing calls is checked as if called with a reference into
// the one pool of its parameter's pointee type (§3.9), so one that links its
// parameter into the pool, or asks the pool for its index, compiles whether
// or not a call is left to it.
struct Node {
    id: i64,
    prev: Node&<u32 in nodes>,
    next: Node&<u32 in nodes>,
}

reusable var nodes: Node[>..] = [];

struct List { head: Node&<u32 in nodes> }

fn add_to_front(c: List&, n: Node&) {
    n.prev .= c.head;
    n.next .= c.head.next;
    c.head.next.prev .= n;
    c.head.next .= n;
}

fn index(n: Node&) -> i64 { return nodes.index_of(n); }

fn link(a: Node&, b: Node&) { a.next .= b; b.prev .= a; }

fn main() {
    var l = List { head: nodes.alloc_ref(Node { id: 0, prev: self, next: self }) };
    print(l.head.id, " ", l.head.next.id, " ", nodes.len);
}
