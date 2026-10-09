// Types that hold slices of themselves, where the C a type's first use asks
// for is the slice, or a variant, before the type itself: the slice's
// typedef needs only the element's name, and a variant's struct comes with
// its enum's.

// A slice parameter is the first use of Op and Op[:].
enum Op { Dec, Inc, Loop { body: Op[:] } }

recursive fn count(ops: Op[:]) -> i64 {
    var n = 0;
    for op in ops {
        match op {
            Loop l => { n += 1 + count(l.body); },
            _ => { n += 1; }
        }
    }
    return n;
}

// A variant result is the first use of Tree.
enum Tree { Leaf { v: i64 }, Node { kids: Tree[:] } }
var forest: Tree[>..];

fn grove(n: i64) -> Tree.Node {
    let start = forest.len;
    for i in n { forest.push(Tree.Leaf { v: i * 10 }); }
    return Tree.Node { kids: forest[start..] };
}

recursive fn total(t: Tree) -> i64 {
    match t {
        Leaf l => { return l.v; },
        Node n => {
            var s = 0;
            for k in n.kids { s += total(k); }
            return s;
        }
    }
}

// A struct holding a slice of itself, first named by that slice.
struct Dir { name: const u8[:], subs: const Dir[:] }

recursive fn names(ds: const Dir[:], out: u8[>..]&) {
    for d in ds {
        out.append(d.name);
        out.push('(');
        names(d.subs, out);
        out.push(')');
    }
}

// A variant's own slice, before the variant or its enum.
fn bodies(ls: Op.Loop[:]) -> i64 {
    var n = 0;
    for l in ls { n += l.body.len; }
    return n;
}

fn main() {
    var pool: Op[>..] = [];
    pool.push(Op.Inc);
    pool.push(Op.Dec);
    pool.push(Op.Loop { body: pool[0..2] });
    pool.push(Op.Inc);
    print(count(pool[2..4]));

    let g = grove(3);
    print(g.kids.len, " ", total(Tree.Node { kids: g.kids }));

    var none: Dir[>..] = [];
    var leaves: Dir[>..] = [];
    leaves.push(Dir { name: "a", subs: none[..] });
    leaves.push(Dir { name: "b", subs: none[..] });
    var top: Dir[>..] = [];
    top.push(Dir { name: "root", subs: leaves[..] });
    var out: u8[>..] = [];
    names(top[..], out);
    print(out);

    var loops: Op.Loop[>..] = [];
    loops.push(Op.Loop { body: pool[0..3] });
    loops.push(Op.Loop { body: pool[1..2] });
    print(bodies(loops[..]));
}
