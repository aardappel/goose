// Etc::CacheSimulation: an LRU cache of string keys and values, a hash map
// from key to node plus a doubly-linked recency list, driven by random keys:
// a hit refreshes the entry's value, a miss inserts it and evicts the least
// recently used entry when the cache is full.

namespace cache_simulation;

import std;
import dictionary;
import bench;
import helper;

// The nodes live in a reusable pool, which hands an evicted node's slot to
// the next insertion, as the Rust version's nodes and free list do. A pool
// element has a fixed size, so the key and value strings are stored inline
// with room for "item_" and "updated_" plus a number. The links are offsets
// from the pool's base.
struct Node {
    key: u8[..16],
    value: u8[..32],
    prev: Node&<u32 in nodes>?,
    next: Node&<u32 in nodes>?,
}

reusable var nodes: Node[>..] = [];

fn new_node(key: u8[:], value: u8[:]) -> Node {
    return Node { key: key, value: value, prev: null, next: null };
}

struct LruCache {
    capacity: i64,
    head: Node&<u32 in nodes>?,
    tail: Node&<u32 in nodes>?,
    size: i64,
    cache: dictionary<u8[..16], Node&<u32 in nodes>?>,
}

fn new_cache(capacity: i64) -> LruCache {
    var c = LruCache { capacity: capacity, head: null, tail: null, size: 0, .. };
    c.cache.reserve(capacity);
    return c;
}

// The value stored for key, now the most recent entry, or null. This get
// shadows the dictionary's inside the namespace, so that one is `::get`.
fn get(c: LruCache&, key: u8[..16]) -> u8[..32]? {
    let node = ::get(c.cache, key);
    guard node else { return null; }
    c.move_to_front(node);
    return node.value;
}

fn put(c: LruCache&, key: u8[..16], value: u8[:]) {
    let found = ::get(c.cache, key);
    if found {
        found.value = value;
        c.move_to_front(found);
        return;
    }

    if c.size >= c.capacity {
        c.remove_oldest();
    }

    let node .= nodes.alloc_ref(new_node(key, value));

    c.cache.insert(key, node);
    c.add_to_front(node);
    c.size++;
}

fn len(c: LruCache&) -> i64 { c.size }

fn move_to_front(c: LruCache&, node: Node&) {
    if node .== c.head { return; }

    let prev = node.prev;
    let next = node.next;

    if prev { prev.next .= next; }
    if next { next.prev .= prev; }

    if node .== c.tail { c.tail .= prev; }

    node.prev .= null;
    node.next .= c.head;

    let head = c.head;
    if head { head.prev .= node; }

    c.head .= node;

    if c.tail == null { c.tail .= node; }
}

fn add_to_front(c: LruCache&, node: Node&) {
    node.next .= c.head;

    let head = c.head;
    if head { head.prev .= node; }

    c.head .= node;

    if c.tail == null { c.tail .= node; }
}

fn remove_oldest(c: LruCache&) {
    let tail_node = c.tail;
    if tail_node {
        c.cache.remove(tail_node.key);

        let prev = tail_node.prev;
        if prev { prev.next .= null; }

        c.tail .= prev;

        if tail_node .== c.head { c.head .= null; }

        nodes.free(nodes.index_of(tail_node));
        c.size--;
    }
}

fn run(b: Bench&) -> u32 {
    let values_size = config_i64(b.name, "values");
    let cache_size = config_i64(b.name, "size");
    var cache = new_cache(cache_size);
    var hits: u32 = 0;
    var misses: u32 = 0;

    measure(b) { iteration_id =>
        for i in 1000 {
            let key: u8[..16] = str("item_", next_int(values_size));
            if cache.get(key) {
                hits++;
                cache.put(key, str("updated_", iteration_id));
            } else {
                misses++;
                cache.put(key, str("new_", iteration_id));
            }
        }
    };

    var result: u32 = 5432;
    result = (result << 5) + hits;
    result = (result << 5) + misses;
    result = (result << 5) + cache.len() as! u32;
    return result;
}
