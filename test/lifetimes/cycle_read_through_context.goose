// A recursive-descent builder over a context struct of borrowed tables
// (§7.8, A.6): `rest` is handed the node built so far, read through the
// context, and links it under a new node after calling back into the cycle.
// Every call gives it a node read through the context, which every call
// passes on, so it points outside the cycle in every activation and may be
// stored there.
import std;

enum Node {
    Leaf { v: i64 },
    Pair { left: Node..&, right: Node..& },
}

struct Ctx { depth: i64, nodes: Node..[>..]& }

recursive fn expr(c: Ctx&) -> Node..& {
    let node: Node..& = c.nodes.push(Node.Leaf { v: c.depth });
    return rest(c, node);
}

fn rest(c: Ctx&, left_node: Node..&) -> Node..& {
    var current: Node..& = left_node;
    if c.depth < 3 {
        c.depth++;
        let right: Node..& = expr(c);
        current .= c.nodes.push(Node.Pair { left: current, right: right });
    }
    return current;
}

recursive fn sum(n: Node..&) -> i64 {
    match n {
        Leaf &l => { return l.v; },
        Pair &p => { return sum(p.left) + sum(p.right); },
    }
}

// The same with the pool reached through a reference held by a struct
// that owns a growable table of its own.
struct Parser { pos: i64, nodes: Node..[>..]&, roots: (Node..&)[>..] }

recursive fn term(p: Parser&) -> Node..& {
    let node: Node..& = p.nodes.push(Node.Leaf { v: p.pos });
    return term_rest(p, node);
}

fn term_rest(p: Parser&, left_node: Node..&) -> Node..& {
    var current: Node..& = left_node;
    while p.pos < 4 {
        p.pos++;
        let right: Node..& = term(p);
        current .= p.nodes.push(Node.Pair { left: current, right: right });
    }
    return current;
}

fn main() {
    var nodes: Node..[>..] = [];
    var c = Ctx { depth: 0, nodes: nodes };
    let root: Node..& = expr(c);
    print(sum(root), " ", nodes.len);
    var more: Node..[>..] = [];
    var p = Parser { pos: 0, nodes: more, roots: [] };
    let t: Node..& = term(p);
    p.roots.push(t);
    print(sum(p.roots[0]), " ", more.len);
}
