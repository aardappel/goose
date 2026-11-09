// A tree whose nodes own their children through relative links
// (bench/plb/merkletrees with its links and handles declared own): make
// returns a tree as an obligation, a recursive free_tree consumes it, and
// the walks between borrow it.

import slot_pool;
import std;

let MIN_DEPTH = 4;

struct Node {
    hash: i64,
    value: i64,
    l: own Node&<u32 in pool.items>?,
    r: own Node&<u32 in pool.items>?,
    has_hash: bool,
    has_value: bool,
}

var pool = slot_pool<Node> {};

recursive fn make(depth: i64) -> own Node& {
    if depth == 0 { return pool.take(Node { value: 1, has_value: true, .. }); }
    let l = make(depth - 1);
    let r = make(depth - 1);
    return pool.take(Node { l: l, r: r, .. });
}

recursive fn free_tree(n: own Node&) {
    var l: own Node? = null;
    var r: own Node? = null;
    pool.release(n) { l .= take(it.l); r .= take(it.r); };
    if l { free_tree(l); }
    if r { free_tree(r); }
}

fn get_hash(n: Node&) -> i64 { if n.has_hash { n.hash } else { -1 } }

recursive fn cal_hash(n: Node&) {
    guard !n.has_hash;
    if n.has_value {
        n.hash = n.value;
        n.has_hash = true;
        return;
    }
    let l = n.l;
    guard l;
    let r = n.r;
    guard r;
    cal_hash(l);
    cal_hash(r);
    n.hash = get_hash(l) + get_hash(r);
    n.has_hash = true;
}

recursive fn check(n: Node&) -> bool {
    guard n.has_hash else { return false; }
    if n.has_value { return true; }
    let l = n.l;
    guard l else { return false; }
    let r = n.r;
    guard r else { return false; }
    return check(l) && check(r);
}

fn main() {
    let max_depth = MIN_DEPTH + 2;
    {
        let stretch = make(max_depth + 1);
        cal_hash(stretch);
        print("stretch tree of depth ", max_depth + 1, "\t root hash: ", get_hash(stretch),
              " check: ", check(stretch));
        free_tree(stretch);
    }
    let long_lived = make(max_depth);
    var d = MIN_DEPTH;
    while d <= max_depth {
        let iterations = 1 << (max_depth - d + MIN_DEPTH);
        var sum = 0;
        for i in iterations {
            let t = make(d);
            cal_hash(t);
            sum += get_hash(t);
            free_tree(t);
        }
        print(iterations, "\t trees of depth ", d, "\t root hash sum: ", sum);
        d += 2;
    }
    cal_hash(long_lived);
    print("long lived tree of depth ", max_depth, "\t root hash: ", get_hash(long_lived),
          " check: ", check(long_lived));
    free_tree(long_lived);
    print("live ", pool.items.len - pool.free_slots.len, " of ", pool.items.len);
}
