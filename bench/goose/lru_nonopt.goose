// lru.goose with non-optional links. The sentinels are the only reason the
// list links were `Node&<u32>?`: a non-optional relative reference needs a
// target at construction and the first node of the pool has none. `self`
// (spec 3.9) gives it one -- the value under construction -- so both
// sentinels are built pointing at themselves and relinked afterwards, every
// link is non-optional, and the null test and `assert` narrowing that every
// load of a link used to need are gone. Everything else, including the key
// stream and the printed output, is lru.goose unchanged.

import rng;

let N = 8000000;                         // BENCH_N
let CAP = N / 8;
let KEYS = CAP * 2;

struct Node { key: i32, val: i32, prev: Node&<u32>, next: Node&<u32> }
struct Slot { key: i32, idx: i32 }       // idx < 0: empty.

reusable var pool: Node[>..] = [];
var slots: Slot[>..] = [];
var mask: u64 = 0;

fn home(key: i64) -> i64 { ((key as u64 * 0x9E3779B97F4A7C15 >> 24) & mask) as! i64 }

// The slot holding `key`, or the empty slot where it would go.
fn find(key: i64) -> i64 {
    var i = home(key);
    loop {
        let s = slots[i];
        guard s.idx >= 0 && s.key != key;
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
        guard s.idx >= 0;
        let h = home(s.key);
        if (i < j && (h <= i || h > j)) || (i > j && h <= i && h > j) {
            slots[i] = s;
            i = j;
        }
    }
    slots[i].idx = -1;
}

fn unlink(n: Node&) {
    let p: Node& = n.prev;
    let q: Node& = n.next;
    p.next .= q;
    q.prev .= p;
}

fn link_front(head: Node&, n: Node&) {
    let q: Node& = head.next;
    n.prev .= head;
    n.next .= q;
    q.prev .= n;
    head.next .= n;
}

fn main() {
    var nslots = 16;
    while nslots < CAP * 2 { nslots *= 2; }
    mask = (nslots - 1) as u64;
    for s in nslots { slots.push(Slot { key: 0, idx: -1 }); }
    // Two sentinels: nothing exists to link them to yet, so they start out
    // pointing at themselves and are joined right after.
    let hi = pool.alloc_index(Node { key: -1, val: 0, prev: self, next: self });
    let ti = pool.alloc_index(Node { key: -1, val: 0, prev: self, next: self });
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
        let fi = slots[at].idx;
        if xs_mod(r >> 40, 16) == 0 {
            // An invalidation: drop the entry if it is cached.
            if fi >= 0 {
                unlink(pool[fi]);
                map_remove(at);
                pool.free(fi);
                count--;
                removes++;
            }
            continue;
        }
        if fi >= 0 {
            let n = &pool[fi];
            n.val += 1;
            unlink(n);
            link_front(pool[hi], n);
            hits++;
            continue;
        }
        misses++;
        var ins = at;
        if count == CAP {
            let lru: Node& = pool[ti].prev;
            unlink(lru);
            let lat = find(lru.key);
            let li = slots[lat].idx;
            map_remove(lat);
            pool.free(li);
            count--;
            // The deletion may have shifted entries in this key's probe run,
            // so the empty slot found above can be stale.
            ins = find(key);
        }
        let ni = pool.alloc_index(Node { key: key as i32, val: (key % 1000) as i32,
                                         prev: self, next: self });
        slots[ins] = Slot { key: key as i32, idx: ni as i32 };
        link_front(pool[hi], pool[ni]);
        count++;
    }
    // Walk the list from most to least recent, so the order is checked too.
    var walk: u64 = 0;
    var vals = 0;
    var cur: Node& = pool[hi].next;
    loop {
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
