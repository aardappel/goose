// error: references may only be passed down, not stored, inside a recursive cycle
// left_node points into the storage c leads to, but at another node in each
// activation, so it is no storage the cycle may store into (§7.8).
import std;
struct Node { v: i64, left: Node?, right: Node? }
struct Ctx { depth: i64, nodes: Node[>..]& }
recursive fn expr(c: Ctx&) -> Node& {
    let node: Node& = c.nodes.push(Node { v: c.depth, left: null, right: null });
    return rest(c, node);
}
fn rest(c: Ctx&, left_node: Node&) -> Node& {
    if c.depth < 4 {
        c.depth++;
        let r: Node& = expr(c);
        left_node.right .= r;
    }
    return left_node;
}
fn main() {
    var nodes: Node[>..] = [];
    var c = Ctx { depth: 0, nodes: nodes };
    let root: Node& = expr(c);
    print(c.depth, " ", nodes.len);
}
