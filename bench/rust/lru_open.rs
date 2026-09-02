// The same LRU cache with the map hand-rolled: an open-addressed table of
// (key, pool index) slots with a Fibonacci hash, linear probing and
// backward-shift deletion, which is what a Rust programmer reaches for once the
// profiler points at SipHash and no hasher crate is allowed. It mirrors
// bench/goose/lru.goose's map slot for slot; the recency list is the same
// `Vec<Node>` arena with `u32` links and a free list that lru_hashmap.rs uses,
// so the gap between the two rows is the map and nothing else.
//
// The slots hold indices rather than node references for the same reason the
// Goose row does: safe Rust cannot store a reference into the pool it is still
// mutating. Still entirely safe -- an out-of-range index panics like any other.
mod bench;
use bench::*;

const N: i64 = 2000000;                  // BENCH_N
const CAP: i64 = N / 8;
const KEYS: i64 = CAP * 2;

struct Node { key: i32, val: i32, prev: u32, next: u32 }
struct Slot { key: i32, idx: i32 }       // idx < 0: empty.

// Both sentinels live in the pool, so no link is ever null.
const HEAD: usize = 0;
const TAIL: usize = 1;

fn home(key: i32, mask: u64) -> usize {
    (((key as u64).wrapping_mul(0x9E3779B97F4A7C15) >> 24) & mask) as usize
}

// The slot holding `key`, or the empty slot where it would go.
fn find(slots: &[Slot], mask: u64, key: i32) -> usize {
    let mut i = home(key, mask);
    while slots[i].idx >= 0 && slots[i].key != key {
        i = (i + 1) & mask as usize;
    }
    i
}

// Backward-shift deletion: slide later entries of the probe run down over the
// hole, so no tombstones accumulate.
fn map_remove(slots: &mut [Slot], mask: u64, at: usize) {
    let mut i = at;
    let mut j = i;
    loop {
        j = (j + 1) & mask as usize;
        let (jkey, jidx) = (slots[j].key, slots[j].idx);
        if jidx < 0 { break; }
        let h = home(jkey, mask);
        if (i < j && (h <= i || h > j)) || (i > j && h <= i && h > j) {
            slots[i] = Slot { key: jkey, idx: jidx };
            i = j;
        }
    }
    slots[i].idx = -1;
}

fn unlink(pool: &mut [Node], n: u32) {
    let (p, q) = (pool[n as usize].prev, pool[n as usize].next);
    pool[p as usize].next = q;
    pool[q as usize].prev = p;
}

fn link_front(pool: &mut [Node], n: u32) {
    let q = pool[HEAD].next;
    pool[n as usize].prev = HEAD as u32;
    pool[n as usize].next = q;
    pool[q as usize].prev = n;
    pool[HEAD].next = n;
}

fn alloc(pool: &mut Vec<Node>, free: &mut Vec<u32>, key: i32, val: i32) -> u32 {
    let n = Node { key, val, prev: 0, next: 0 };
    match free.pop() {
        Some(i) => { pool[i as usize] = n; i }
        None => { pool.push(n); pool.len() as u32 - 1 }
    }
}

fn main() {
    let mut nslots: i64 = 16;
    while nslots < CAP * 2 { nslots *= 2; }
    let mask = (nslots - 1) as u64;
    let mut slots: Vec<Slot> = (0..nslots).map(|_| Slot { key: 0, idx: -1 }).collect();
    let mut pool: Vec<Node> = Vec::new();
    pool.push(Node { key: -1, val: 0, prev: 0, next: TAIL as u32 });
    pool.push(Node { key: -1, val: 0, prev: HEAD as u32, next: 0 });
    let mut free: Vec<u32> = Vec::new();
    let mut count: i64 = 0;
    let mut hits: i64 = 0;
    let mut misses: i64 = 0;
    let mut removes: i64 = 0;
    let mut r: u64 = 12345;
    for _ in 0..N {
        r = xs_next(r);
        let a = xs_mod(r, KEYS);
        let key = (a * a) / KEYS;          // Skewed towards small keys.
        let k = key as i32;
        let at = find(&slots, mask, k);
        let fi = slots[at].idx;
        if xs_mod(r >> 40, 16) == 0 {
            // An invalidation: drop the entry if it is cached.
            if fi >= 0 {
                unlink(&mut pool, fi as u32);
                map_remove(&mut slots, mask, at);
                free.push(fi as u32);
                count -= 1;
                removes += 1;
            }
            continue;
        }
        if fi >= 0 {
            pool[fi as usize].val += 1;
            unlink(&mut pool, fi as u32);
            link_front(&mut pool, fi as u32);
            hits += 1;
            continue;
        }
        misses += 1;
        let mut ins = at;
        if count == CAP {
            let lru = pool[TAIL].prev;
            unlink(&mut pool, lru);
            let lat = find(&slots, mask, pool[lru as usize].key);
            let li = slots[lat].idx;
            map_remove(&mut slots, mask, lat);
            free.push(li as u32);
            count -= 1;
            // The deletion may have shifted entries in this key's probe run,
            // so the empty slot found above can be stale.
            ins = find(&slots, mask, k);
        }
        let ni = alloc(&mut pool, &mut free, k, (key % 1000) as i32);
        slots[ins] = Slot { key: k, idx: ni as i32 };
        link_front(&mut pool, ni);
        count += 1;
    }
    // Walk the list from most to least recent, so the order is checked too.
    let mut walk: u64 = 0;
    let mut vals: i64 = 0;
    let mut cur = pool[HEAD].next as usize;
    while pool[cur].key >= 0 {
        walk = walk.wrapping_mul(31).wrapping_add(pool[cur].key as u64);
        vals += pool[cur].val as i64;
        cur = pool[cur].next as usize;
    }
    emit(hits);
    emit(misses);
    emit(removes);
    emit(count);
    println!("{}", walk);
    emit(vals);
}
