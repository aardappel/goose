// error: call expr stores a reference rooted at c into what its parameter c leads to, which may be pa
// The nodes read through c may lie in pb, which dies before pa, where the
// cycle links them: the entry call judges the stores it records into
// what c leads to (§7.8, §9.2).
import std;
enum Node {
    Leaf { v: i64 },
    Pair { left: Node..&, right: Node..& },
}
struct Ctx { depth: i64, a: Node..[>..]&, b: Node..[>..]& }
recursive fn expr(c: Ctx&) -> Node..& {
    let node: Node..& = c.b.push(Node.Leaf { v: c.depth });
    return rest(c, node);
}
fn rest(c: Ctx&, left_node: Node..&) -> Node..& {
    var current: Node..& = left_node;
    if c.depth < 3 {
        c.depth++;
        let right: Node..& = expr(c);
        current .= c.a.push(Node.Pair { left: current, right: right });
    }
    return current;
}
fn main() {
    var pa: Node..[>..] = [];
    {
        var pb: Node..[>..] = [];
        var c = Ctx { depth: 0, a: pa, b: pb };
        let root: Node..& = expr(c);
    }
    print(pa.len);
}
