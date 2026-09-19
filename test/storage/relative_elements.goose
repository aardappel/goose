// Arrays whose elements are themselves relative references (§3.9), grown by
// resize(n, v) and shrunk by pop (§3.3). The fill is evaluated once, as the
// plain reference it is, and each new slot stores its offset measured from
// that slot, so every one of them leads to the same node; pop reads the
// offset back from the slot it leaves and hands out the plain reference.

struct Node { v: i64 }
struct Nest { nodes: Node[..8], links: (Node&<u16>?)[..8] }

var pool: Node[>..] = [];
var picks = 0;

fn pick(i: i64) -> i64 {
    picks += 1;
    return i;
}

fn show(n: Node&) { print(n.v); }

fn show_opt(n: Node&?) {
    guard n else { print("null"); return; }
    print(n.v);
}

// Pops through a reference to the array's holder, and to the array.
fn show_popped(t: Nest&) { show_opt(t.links.pop()); }

fn pop_value(a: (Node&<u32 in pool>)[..6]&) -> i64 { return a.pop().v; }

fn main() {
    // Self-relative links beside the nodes they point at, in one value.
    var t = Nest { nodes: [], links: [] };
    t.nodes.push(Node { v: 5 });
    t.nodes.push(Node { v: 6 });
    t.links.push(t.nodes[1]);
    t.links.resize(3, t.nodes[pick(0)]);
    t.links.resize(4, null);
    t.links.resize(6, t.links[0]);          // A link read out of the array itself.
    print(picks);                           // 1: one run filled two slots
    for i in t.links.len {
        let l = t.links[i];
        guard l else { print("null"); continue; }
        print(l.v, " at ", t.nodes.index_of(l));
    }
    show_opt(t.links.pop());                // 6
    t.links.resize(4);
    show_popped(t);                         // null
    show_opt(t.links.pop());                // 5
    print(t.links.len);                     // 2

    // `in pool` links, measured from the pool's base: a grow-only array,
    // then a grow-shrink one of optional u8 links, a limited one and one
    // whose capacity is chosen at run time.
    pool.push(Node { v: 7 });
    pool.push(Node { v: 8 });
    pool.push(Node { v: 9 });
    var g: (Node&<u32 in pool>)[>..] = [];
    g.push(pool[1]);
    g.resize(3, pool[pick(2)]);
    for l in g { print(l.v, " at ", pool.index_of(l)); }
    // A grow-only array pops only as a statement of its own (§5.1).
    let r = g.pop();                        // A copy of the node it led to.
    print(r.v);                             // 9
    g.pop();
    print(g.len, " ", g[0].v);              // 1 8

    var s: (Node&<u8 in pool>?)[>..<] = [];
    s.resize(2, null);
    s.resize(4, pool[pick(0)]);
    for l in s { show_opt(l); }
    show_opt(s.pop());                      // 7
    s.resize(2);
    show_opt(s.pop());                      // null
    print(s.len);                           // 1

    var k: (Node&<u32 in pool>)[..6] = [];
    k.push(pool[2]);
    k.resize(4, pool[pick(1)]);
    print(k.pop().v + pop_value(k));        // 16
    for l in k { print(l.v, " at ", pool.index_of(l)); }

    var d: (Node&<u16 in pool>)[..] = [..5];
    d.resize(5, pool[pick(0)]);
    d.pop();
    show(d.pop());                          // 7
    for l in d { print(l.v, " at ", pool.index_of(l)); }
    print(picks);                           // 5
}
