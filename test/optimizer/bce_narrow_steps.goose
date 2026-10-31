// A constant step on a narrow integer wraps at its own width in release
// builds (§6.2), but only ever against the step's sign: `e + 1` is at most
// e + 1 and `e - 1` at least e - 1, wrapped or not. That one side is what
// bounds the next index of a `u32 in nodes` by `upto nodes`; the other side
// holds only where the facts show the step fits the type.

import std;

var nodes: i32[>..] = [];
type NodeId = u32 in nodes;
type Edge = u32 upto nodes;

// e < nodes.len, so e + 1 <= nodes.len, and a wrap could only give 0.
fn next_edge(e: NodeId) -> Edge {
    return (e + 1) as Edge;                    // bce:elide
}

// But e + 1 may be nodes.len itself.
fn next_node(e: NodeId) -> NodeId {
    return (e + 1) as NodeId;                  // bce:keep
}

// e - 1 wraps for e == 0 to 2^32 - 1, which may be past nodes.len...
fn prev_edge(e: NodeId) -> Edge {
    return (e - 1) as Edge;                    // bce:keep
}

// ...unless e >= 1, where it fits the type and is exactly e - 1.
fn prev_node(e: NodeId) -> NodeId {
    guard e >= 1 else { return e; }
    return (e - 1) as NodeId;                  // bce:elide
}

// A byte plus one is at most 256, wrapped (to 0) or not.
fn shifted(counts: i64[:], s: u8[:]) -> i64 {
    guard counts.len > 256 else { return 0; }
    var t = 0;
    for b in s { t += counts[b + 1]; }         // bce:elide
    return t;
}

// A u16 step can wrap past a short table.
fn wide(tab: i64[:], s: u16[:]) -> i64 {
    guard tab.len > 256 else { return 0; }
    var t = 0;
    for w in s { t += tab[w + 1]; }            // bce:keep
    return t;
}

// The ids come out of storage, so the callees know only their types.
fn main() {
    for v in 4 { nodes.push(v as i32 * 10); }
    var ids: NodeId[>..] = [];
    ids.push(3 as NodeId);                     // bce:elide
    ids.push(0 as NodeId);                     // bce:elide
    let e = ids[0];                            // bce:elide
    let z = ids[1];                            // bce:elide
    print(next_edge(e), " ", prev_node(e), " ", prev_node(z), " ", next_node(z));
    var counts: i64[>..] = [];
    for i in 257 { counts.push(i); }
    var bytes: u8[>..] = [];
    bytes.append("A~");
    bytes.push(255);
    var shorts: u16[>..] = [7, 200];
    print(shifted(counts, bytes), " ", wide(counts, shorts));
    print(prev_edge(e));
}
