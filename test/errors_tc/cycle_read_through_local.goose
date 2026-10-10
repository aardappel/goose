// error: storing a reference rooted at left_node, which does not outlive the destination
// rest's left_node is given a node read through c on one call and a local
// of expr, which is in the cycle, on another: what it points at is not
// outside the cycle on every call, so it is not stored there (§7.8, §9.2).
import std;
struct Node { v: i64, left: Node?, right: Node? }
struct Ctx { depth: i64, nodes: Node[>..]& }
recursive fn expr(c: Ctx&) -> Node& {
    let node: Node& = c.nodes.push(Node { v: c.depth, left: null, right: null });
    if c.depth != 2 { return rest(c, node); }
    c.depth++;
    var mine = Node { v: 7, left: null, right: null };
    return rest(c, mine);
}
fn rest(c: Ctx&, left_node: Node&) -> Node& {
    let made: Node& = c.nodes.push(Node { v: 0, left: left_node, right: null });
    if c.depth < 4 {
        c.depth++;
        let r: Node& = expr(c);
    }
    return made;
}
fn main() {
    var nodes: Node[>..] = [];
    var c = Ctx { depth: 0, nodes: nodes };
    let root: Node& = expr(c);
    print(c.depth);
}
