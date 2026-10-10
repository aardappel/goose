// error: storing a reference rooted at left_node, which does not outlive the destination
// What is read through c outlives the cycle but not a global (§9.2).
import std;
enum Node {
    Leaf { v: i64 },
    Pair { left: Node..&, right: Node..& },
}
struct Ctx { depth: i64, nodes: Node..[>..]& }
var keep: Node..? = null;
recursive fn expr(c: Ctx&) -> Node..& {
    let node: Node..& = c.nodes.push(Node.Leaf { v: c.depth });
    return rest(c, node);
}
fn rest(c: Ctx&, left_node: Node..&) -> Node..& {
    keep .= left_node;
    if c.depth < 3 {
        c.depth++;
        let right: Node..& = expr(c);
    }
    return left_node;
}
fn main() {
    var nodes: Node..[>..] = [];
    var c = Ctx { depth: 0, nodes: nodes };
    let root: Node..& = expr(c);
    print(c.depth);
}
