// lru.goose with the map holding node *references* instead of pool indices.
// A reference read back out of a container is rooted at the storage that can
// own it rather than at the container (spec 9.5), and here only `pool` can
// hold a Node, so a node reached through the map is rooted at `pool` exactly
// and still links relatively into it. That is the version this data structure
// wants to be written in; lru.goose stays on indices because 8-byte slots
// match the C++ and Rust arenas it is compared against.
//
// The one thing a reference cannot supply is the index `pool.free` takes, so
// each node carries its own (20-byte nodes against lru.goose's 16). The
// lookup path has no `&pool[i]` in it at all; the insert path has one, since
// `alloc_index` is what yields the index to store.
//
// Otherwise identical to lru.goose: same workload, same output.

import rng;

let N = 8000000;                         // BENCH_N
let CAP = N / 8;
let KEYS = CAP * 2;

struct Node { key: i32, val: i32, idx: i32, prev: Node&<u32>?, next: Node&<u32>? }
struct Slot { key: i32, node: Node&? }   // node == null: empty.

reusable var pool: Node[>..] = [];
var slots: Slot[>..] = [];
var mask: u64 = 0;

fn home(key: i64) -> i64 { ((key as u64 * 0x9E3779B97F4A7C15 >> 24) & mask) as! i64 }

// The slot holding `key`, or the empty slot where it would go.
fn find(key: i64) -> i64 {
    var i = home(key);
    loop {
        let s = slots[i];
        guard s.node != null && s.key != key;
        i = (((i + 1) as u64) & mask) as! i64;
    }
    return i;
}

// Backward-shift deletion: slide later entries of the probe run down over
// the hole, so no tombstones accumulate.
fn map_remove(at: i64) {
    var i = at;
    var j = i;
    loop {
        j = (((j + 1) as u64) & mask) as! i64;
        let s = slots[j];
        guard s.node != null;
        let h = home(s.key);
        if (i < j && (h <= i || h > j)) || (i > j && h <= i && h > j) {
            slots[i] = s;
            i = j;
        }
    }
    slots[i].node .= null;
}

fn unlink(n: Node&) {
    let p = n.prev;
    let q = n.next;
    assert(p);
    assert(q);
    p.next .= q;
    q.prev .= p;
}

fn link_front(head: Node&, n: Node&) {
    let q = head.next;
    assert(q);
    n.prev .= head;
    n.next .= q;
    q.prev .= n;
    head.next .= n;
}

fn main() {
    var nslots = 16;
    while nslots < CAP * 2 { nslots *= 2; }
    mask = (nslots - 1) as u64;
    for s in nslots { slots.push(Slot { key: 0, node: null }); }
    // Two sentinels: the list is never empty, so no link is null in practice.
    let hi = pool.alloc_index(Node { key: -1, val: 0, idx: 0 });
    let ti = pool.alloc_index(Node { key: -1, val: 0, idx: 1 });
    pool[hi].next .= &pool[ti];
    pool[ti].prev .= &pool[hi];
    var count = 0;
    var hits = 0;
    var misses = 0;
    var removes = 0;
    var r: u64 = 12345;
    for op in N {
        r = xs_next(r);
        let a = xs_mod(r, KEYS);
        let key = (a * a) / KEYS;          // Skewed towards small keys.
        let at = find(key);
        let f = slots[at].node;
        if xs_mod(r >> 40, 16) == 0 {
            // An invalidation: drop the entry if it is cached.
            if f {
                unlink(f);
                map_remove(at);
                pool.free(f.idx);
                count--;
                removes++;
            }
            continue;
        }
        if f {
            f.val += 1;
            unlink(f);
            link_front(&pool[hi], f);
            hits++;
            continue;
        }
        misses++;
        var ins = at;
        if count == CAP {
            let lru = pool[ti].prev;
            assert(lru);
            unlink(lru);
            let lat = find(lru.key);
            map_remove(lat);
            pool.free(lru.idx);
            count--;
            // The deletion may have shifted entries in this key's probe run,
            // so the empty slot found above can be stale.
            ins = find(key);
        }
        let ni = pool.alloc_index(Node { key: key as i32, val: (key % 1000) as i32, idx: 0 });
        let n = &pool[ni];
        n.idx = ni as! i32;
        slots[ins] = Slot { key: key as i32, node: n };
        link_front(&pool[hi], n);
        count++;
    }
    // Walk the list from most to least recent, so the order is checked too.
    var walk: u64 = 0;
    var vals = 0;
    var cur = pool[hi].next;
    loop {
        guard cur;
        guard cur.key >= 0;
        walk = walk * 31 + cur.key as u64;
        vals += cur.val;
        cur .= cur.next;
    }
    print(hits);
    print(misses);
    print(removes);
    print(count);
    print(walk);
    print(vals);
}
