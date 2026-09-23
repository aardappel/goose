// A store through a reference whose root only bounds where it points is a
// store into every storage that root may stand for (§9.2): a value that
// outlives all of them may be stored there, through a call too, and a shrink
// is checked against each of them still used, and against no other.
struct Node { p: i64?, name: const u8[:] }
struct Holder { node: Node& }

fn pick(a: Node&, b: Node&, c: bool) -> Node& { if c { a } else { b } }
fn link(r: Node&, x: i64&) { r.p .= x; }
fn link_behind(h: Holder&, x: i64&) { h.node.p .= x; }

fn show(n: Node&) {
    if n.p { print(n.name, " ", n.p); } else { print(n.name, " -"); }
}

fn main() {
    var x: i64 = 1;
    var y: i64 = 2;
    var a1 = Node { p: null, name: "a1" };
    var c = x > 0;
    {
        var a2 = Node { p: null, name: "a2" };
        // r may be a1 or a2, both of which x and y outlive.
        let r: Node& = if c { a2 } else { a1 };
        r.p .= x;
        // So may what pick returns, and what link stores into through it.
        link(pick(a1, a2, !c), y);
        // A literal is static data, which outlives every candidate.
        r.name = "r2";
        show(a1);
        show(a2);
    }
    // Behind h: storage of main's that link_behind cannot see, n here.
    var n = Node { p: null, name: "n" };
    {
        var h = Holder { node: n };
        link_behind(h, y);
    }
    show(n);
    // A store that may be into b1 or b2 leaves either holding a reference
    // into xs, but neither is used after the clear, and b3, declared after
    // the store, is none it may have gone into.
    var xs: i64[>..] = [7, 8, 9];
    {
        var b1 = Node { p: null, name: "b1" };
        var b2 = Node { p: null, name: "b2" };
        let r: Node& = if c { b1 } else { b2 };
        r.p .= xs[1];
        show(b1);
        var b3 = Node { p: null, name: "b3" };
        xs.clear();
        show(b3);
    }
    print(xs.len);
}
