// A context of borrowed tables (A.6): the tables are locals of the driver,
// and the recursion reaches them through one struct of references it passes
// on as it was given. Every call back into the cycle passes `c` on, so in
// every activation `c` points where the driver's own does, outside the
// cycle, and views read through it -- the names the walk copies into the
// text arena -- may be stored into its other tables inside the cycle, as a
// pool's elements may (§7.8). The same holds for a context passed around a
// cycle of two functions, for one a nested recursive function captures,
// and inside a function value's body that calls back into its cycle.
struct Node { name: const u8[:], depth: i64 }
struct Ctx {
    source: const u8[:],
    text: u8[>..]&,
    nodes: Node[>..]&,
}

fn own(c: Ctx&, s: const u8[:]) -> const u8[:] {
    let begin = c.text.len;
    c.text.append(s);
    return c.text[begin..];
}

recursive fn walk(c: Ctx&, at: i64, depth: i64) -> i64 {
    if at >= c.source.len { return 0; }
    let name = own(c, c.source[at..at + 1]);
    c.nodes.push(Node { name: name, depth: depth });
    return 1 + walk(c, at + 1, depth + 1);
}

// term is a member of expr's cycle from its call back on.
recursive fn expr(c: Ctx&, at: i64, depth: i64) -> i64 {
    c.nodes.push(Node { name: own(c, "("), depth: depth });
    if at >= c.source.len { return at; }
    return term(c, at, depth + 1);
}

fn term(c: Ctx&, at: i64, depth: i64) -> i64 {
    let piece = own(c, c.source[at..at + 1]);
    let end = expr(c, at + 1, depth + 1);
    c.nodes.push(Node { name: piece, depth: depth });
    return end;
}

fn outline(c: Ctx&, n: i64) {
    recursive fn level(k: i64) {
        c.nodes.push(Node { name: own(c, "-"), depth: k });
        if k < n { level(k + 1); }
    }
    level(0);
}

fn each<F>(n: i64) { for i in n { F(i); } }

recursive fn fan(c: Ctx&, k: i64) {
    each(2) { i =>
        if k > 0 && i == 0 { fan(c, k - 1); }
        c.nodes.push(Node { name: own(c, "+"), depth: 10 * k + i });
    };
}

fn compile(source: const u8[:]) -> i64 {
    var text: u8[>..] = [];
    var nodes: Node[>..] = [];
    var c = Ctx { source: source, text: text, nodes: nodes };
    let n = walk(c, 0, 0);
    print(expr(c, 0, 0));
    outline(c, 2);
    fan(c, 1);
    for &nd in nodes { print(nd.depth, " ", nd.name); }
    return n + text.len;
}

fn main() { print(compile("abc")); }
