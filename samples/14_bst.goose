// A binary search tree in one grow-only array of nodes. Children are
// 4-byte self-relative links: an offset from the link's own address to the
// child, which is what lets the whole tree be moved or saved as a block.
// A link that is null is the offset 0. Insertion walks down with one
// reference that is retargeted with `.=`, and the recursive walks pass
// their output array down by reference, since a recursive function cannot
// own growable data of its own.
//
// Build and run (from this directory):
//   goose -o bst.c 14_bst.goose && cl bst.c && bst

import std;

struct Node { key: i32, count: i32 = 1, left: Node&<u32>?, right: Node&<u32>? }

// The tree is the pool plus the convention that slot 0 is the root.
fn insert(pool: Node[>..]&, key: i32) {
    if pool.len == 0 { pool.push(Node { key: key }); return; }
    var cur .= pool[0];                  // .= binds the element; = would copy it
    // The walk retargets that one reference; a null child is the offset 0.
    loop {
        if key == cur.key { cur.count += 1; return; }
        if key < cur.key {
            let l = cur.left;
            if l { cur .= l; continue; }
            cur.left .= pool.push(Node { key: key });   // a push never moves cur
            return;
        }
        let r = cur.right;
        if r { cur .= r; continue; }
        cur.right .= pool.push(Node { key: key });
        return;
    }
}

fn find(pool: Node[>..]&, key: i32) -> Node? {
    guard pool.len > 0 else { return null; }
    var cur .= pool[0];
    loop {
        if key == cur.key { return cur; }
        let next = if key < cur.key { cur.left } else { cur.right };
        guard next else { return null; }
        cur .= next;
    }
}

fn min_key(pool: Node[>..]&) -> i32 {
    var cur .= pool[0];
    loop {
        let l = cur.left;
        guard l;                         // leaves the loop when null
        cur .= l;
    }
    return cur.key;
}

fn max_key(pool: Node[>..]&) -> i32 {
    var cur .= pool[0];
    loop {
        let r = cur.right;
        guard r;
        cur .= r;
    }
    return cur.key;
}

recursive fn in_order(n: Node&, out: i32[>..]&) {
    let l = n.left;
    if l { in_order(l, out); }
    for i in n.count { out.push(n.key); }
    let r = n.right;
    if r { in_order(r, out); }
}

recursive fn height(n: Node&) -> i64 {
    var h = 0;
    let l = n.left;
    if l { h = height(l); }
    let r = n.right;
    if r { h = max(h, height(r)); }
    return h + 1;
}

// The keys in [lo, hi], in order, pruning subtrees that cannot contain any.
recursive fn range(n: Node&, lo: i32, hi: i32, out: i32[>..]&) {
    let l = n.left;
    if l && n.key > lo { range(l, lo, hi, out); }
    if n.key >= lo && n.key <= hi { out.push(n.key); }
    let r = n.right;
    if r && n.key < hi { range(r, lo, hi, out); }
}

fn main() {
    var pool: Node[>..] = [];
    var r = rng { 7 };
    var inserted = 0;
    for i in 1000 {
        insert(pool, r.rand_int(500) as i32);
        inserted++;
    }
    print(inserted, " inserts, ", pool.len, " distinct keys, ", pool.len * 12, " bytes of tree");
    print("height ", height(pool[0]), ", min ", min_key(pool), ", max ", max_key(pool));

    var sorted: i32[>..] = [];
    in_order(pool[0], sorted);
    print(sorted.len, " keys in order, first ten ", sorted[..10]);
    var ok = true;
    for i in 1..sorted.len { if sorted[i - 1] > sorted[i] { ok = false; } }
    print("sorted: ", ok);

    for k in [17, 42, 250, 499] {
        let n = find(pool, k as i32);
        if n { print(k, " present, seen ", n.count, " times"); } else { print(k, " absent"); }
    }
    var mid: i32[>..] = [];
    range(pool[0], 240, 260, mid);
    print("keys in 240..260: ", mid);

    // A second tree from sorted input degenerates into a list; the height
    // shows why balanced trees exist.
    var chain: Node[>..] = [];
    for i in 100 { insert(chain, i as i32); }
    print("100 sorted inserts: height ", height(chain[0]));
}
