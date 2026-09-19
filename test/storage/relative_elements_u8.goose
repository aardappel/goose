// A resize fill of relative-reference elements is stored in each new slot as
// the offset from that slot, and range-checked there (§3.3, §3.9): one node
// linked from enough slots ends up too far from the last of them.

struct Node { v: i64 }
struct Fan { nodes: Node[..2], links: (Node&<u8>)[..200] }

fn main() {
    var f = Fan { nodes: [], links: [] };
    f.nodes.push(Node { v: 1 });
    // Node 0 is at byte 1 and link i at byte 18 + i, so link 111 is 128
    // bytes past it: the farthest a u8 offset reaches back.
    f.links.resize(112, f.nodes[0]);
    print(f.links.len);                  // 112
    print(f.links[111].v);               // 1
    f.links.resize(113, f.nodes[0]);     // Aborts: link 112 is 129 bytes past.
    print(0);                            // Not reached.
}
