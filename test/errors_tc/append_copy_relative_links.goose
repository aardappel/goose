// error: .append copies the elements of p.links[..]: copying a value of type Node&<u16>, which contains self-relative references, is not supported
// An element that is itself a self-relative reference is copied as its
// offset, which then measures from the new slot (§3.9).
struct Node { v: i64 }
struct Pool { nodes: Node[..8], links: (Node&<u16>)[..8] }
fn main() {
    var p = Pool { nodes: [], links: [] };
    p.nodes.push(Node { v: 5 });
    p.links.push(p.nodes[0]);
    p.links.append(p.links[..]);
    print(p.links[1].v);
}
