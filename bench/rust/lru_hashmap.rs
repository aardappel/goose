// An LRU cache: a map from key to node, a doubly-linked recency list, and a
// capacity that evicts the least recently used entry on a miss. See
// bench/goose/lru.goose.
//
// This is the idiomatic safe Rust shape, and it is already half a hand-built
// structure: `HashMap<i32, u32>` for the lookup, but the recency list has to be
// a `Vec<Node>` arena with `u32` links and a free list, because std has no
// intrusive list. `LinkedList` hands out no node identity at all, so there is no
// way to move a known element to the front in O(1); `Rc<RefCell<Node>>` would
// give identity but costs two allocations and a refcount pair per node. The
// arena is therefore not an optimisation here, it is the only shape available,
// and what separates this row from lru_open.rs is the map alone.
mod bench;
use bench::*;
use std::collections::HashMap;

const N: i64 = 2000000;                  // BENCH_N
const CAP: i64 = N / 8;
const KEYS: i64 = CAP * 2;

struct Node { key: i32, val: i32, prev: u32, next: u32 }

// Both sentinels live in the pool, so no link is ever null and no link is an
// `Option`: the list is never empty.
const HEAD: usize = 0;
const TAIL: usize = 1;

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

// A freed slot comes back on the next alloc, so the pool never exceeds CAP + 2.
fn alloc(pool: &mut Vec<Node>, free: &mut Vec<u32>, key: i32, val: i32) -> u32 {
    let n = Node { key, val, prev: 0, next: 0 };
    match free.pop() {
        Some(i) => { pool[i as usize] = n; i }
        None => { pool.push(n); pool.len() as u32 - 1 }
    }
}

fn main() {
    let mut pool: Vec<Node> = Vec::new();
    pool.push(Node { key: -1, val: 0, prev: 0, next: TAIL as u32 });
    pool.push(Node { key: -1, val: 0, prev: HEAD as u32, next: 0 });
    let mut free: Vec<u32> = Vec::new();
    let mut map: HashMap<i32, u32> = HashMap::new();
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
        let found = map.get(&k).copied();
        if xs_mod(r >> 40, 16) == 0 {
            // An invalidation: drop the entry if it is cached.
            if let Some(i) = found {
                unlink(&mut pool, i);
                map.remove(&k);
                free.push(i);
                count -= 1;
                removes += 1;
            }
            continue;
        }
        if let Some(i) = found {
            pool[i as usize].val += 1;
            unlink(&mut pool, i);
            link_front(&mut pool, i);
            hits += 1;
            continue;
        }
        misses += 1;
        if count == CAP {
            let lru = pool[TAIL].prev;
            unlink(&mut pool, lru);
            map.remove(&pool[lru as usize].key);
            free.push(lru);
            count -= 1;
        }
        let ni = alloc(&mut pool, &mut free, k, (key % 1000) as i32);
        map.insert(k, ni);
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
