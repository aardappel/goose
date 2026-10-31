// Inlining only adds facts (§10.5): a call returning `nodes`' index type
// indexes nodes directly once the call is spliced in, as it does while it
// stays a call, though the inlined body declares locals of its own (whose
// kills leave the receiver's length alone) and may return from two places
// (where the block's value is still a value of the index type).

import std;

var nodes: f64[>..];
type Id = u32 in nodes;
type Link = u32 upto nodes;

inline fn child(l: Link) -> Id {
    let k: i64 = l;
    assert(k > 0);
    return (k - 1) as Id;                      // bce:elide
}

inline fn child_or_root(l: Link, root: bool) -> Id {
    let k: i64 = l;
    assert(k > 0);
    if root { return 0 as Id; }                // bce:elide
    return (k - 1) as Id;                      // bce:elide
}

fn direct(l: Link) -> f64 { return nodes[child(l)]; }                      // bce:elide
fn direct2(l: Link, r: bool) -> f64 { return nodes[child_or_root(l, r)]; } // bce:elide

fn main() {
    nodes.push(1.0);
    nodes.push(2.0);
    print(direct(2 as Link), " ", direct2(2 as Link, true), " ", direct2(1 as Link, false));
}
