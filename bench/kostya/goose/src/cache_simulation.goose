// Etc::CacheSimulation: an LRU cache of string keys and values, a hash map
// from key to node plus a doubly-linked recency list, driven by random keys:
// a hit refreshes the entry's value, a miss inserts it and evicts the least
// recently used entry when the cache is full.

namespace cache_simulation;

import std;
import dictionary;
import bench;
import helper;

// The nodes live in a reusable pool, so an evicted node's slot is what the
// next insertion takes, as with the free list of the other versions. Keys
// and values are stored inline, and the links are 4-byte offsets from the
// pool's base. The list is circular through a sentinel node, so no link is
// ever null.
struct Node {
    key: u8[..16],       // "item_" and an i32
    value: u8[..32],     // "updated_" and an i64
    prev: Node&<u32 in nodes>,
    next: Node&<u32 in nodes>,
}

reusable var nodes: Node[>..] = [];

struct LruCache {
    capacity: i64,
    size: i64 = 0,
    head: Node&<u32 in nodes>,   // the sentinel: head.next is the newest entry, head.prev the oldest
    cache: dictionary<u8[..16], Node&<u32 in nodes>?>,
}

fn new_cache(capacity: i64) -> LruCache {
    var c = LruCache {
        capacity: capacity,
        head: nodes.alloc_ref(Node { key: [], value: [], prev: self, next: self }),
        ..
    };
    c.cache.reserve(capacity);
    return c;
}

fn unlink(n: Node&) {
    n.prev.next .= n.next;
    n.next.prev .= n.prev;
}

fn add_to_front(c: LruCache&, n: Node&) {
    n.prev .= c.head;
    n.next .= c.head.next;
    c.head.next.prev .= n;
    c.head.next .= n;
}

fn move_to_front(c: LruCache&, n: Node&) {
    if n .== c.head.next { return; }
    unlink(n);
    add_to_front(c, n);
}

fn remove_oldest(c: LruCache&) {
    let oldest .= c.head.prev;
    if oldest .== c.head { return; }
    c.cache.remove(oldest.key);
    unlink(oldest);
    nodes.free(nodes.index_of(oldest));
    c.size--;
}

// The value stored for key, now the most recent entry, or null. This get
// shadows the dictionary's inside the namespace, so that one is `::get`.
fn get(c: LruCache&, key: u8[..16]) -> u8[..32]? {
    let n = ::get(c.cache, key);
    guard n else { return null; }
    move_to_front(c, n);
    return n.value;
}

fn put(c: LruCache&, key: u8[..16], value: u8[:]) {
    let found = ::get(c.cache, key);
    if found {
        found.value = value;
        move_to_front(c, found);
        return;
    }
    if c.size >= c.capacity { remove_oldest(c); }
    let n .= nodes.alloc_ref(Node { key: key, value: value, prev: self, next: self });
    c.cache.insert(key, n);
    add_to_front(c, n);
    c.size++;
}

fn run(b: Bench&) -> u32 {
    let values_size = config_i64(b.name, "values");
    var cache = new_cache(config_i64(b.name, "size"));
    var hits: u32 = 0;
    var misses: u32 = 0;
    // Keys and values are written into one reused builder.
    var text: u8[>..] = [];
    measure(b) { iteration_id =>
        for i in 1000 {
            text.clear();
            text.append("item_");
            format_int(text, next_int(values_size), 10, 0, ' ');
            let key: u8[..16] = text;
            text.clear();
            if cache.get(key) {
                hits++;
                text.append("updated_");
            } else {
                misses++;
                text.append("new_");
            }
            format_int(text, iteration_id, 10, 0, ' ');
            cache.put(key, text);
        }
    };
    var result: u32 = 5432;
    result = (result << 5) + hits;
    result = (result << 5) + misses;
    result = (result << 5) + cache.size as! u32;
    return result;
}
