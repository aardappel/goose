// An LRU cache over a skewed key stream: see bench/goose/lru.goose. Nodes are
// freed and reallocated in an order that has nothing to do with scope, which
// is the case a stack discipline is worst placed for. The printed checksum
// walks the recency list from most to least recent, so the list order is
// checked, not just the counters.
//   VARIANT 0: std::list<Node> plus unordered_map<int32_t, iterator> -- the
//              textbook cache, an allocation per node and another per map
//              entry, splice() to move a hit to the front. Idiomatic.
//   VARIANT 1: a vector arena with uint32 links and a free list, plus a
//              hand-rolled open-addressed table with the same Fibonacci hash,
//              linear probing and backward-shift deletion the Goose row uses.
//              Expert, and the shape safe Rust is limited to.
#include "bench.h"
#include <vector>
#if VARIANT == 0
#include <list>
#include <unordered_map>
#endif

static const long long CAP = BENCH_N / 8;
static const long long KEYS = CAP * 2;

#if VARIANT == 0

struct Node { int32_t key, val; };

int main() {
    std::list<Node> recent;
    std::unordered_map<int32_t, std::list<Node>::iterator> map;
    long long hits = 0, misses = 0, removes = 0;
    uint64_t r = 12345;
    for (long long op = 0; op < BENCH_N; op++) {
        r = xs_next(r);
        long long a = (long long)xs_mod(r, (uint64_t)KEYS);
        int32_t key = (int32_t)((a * a) / KEYS);           // Skewed towards small keys.
        auto it = map.find(key);
        if (xs_mod(r >> 40, 16) == 0) {
            // An invalidation: drop the entry if it is cached.
            if (it != map.end()) { recent.erase(it->second); map.erase(it); removes++; }
            continue;
        }
        if (it != map.end()) {
            it->second->val += 1;
            recent.splice(recent.begin(), recent, it->second);
            hits++;
            continue;
        }
        misses++;
        if ((long long)recent.size() == CAP) {
            map.erase(recent.back().key);
            recent.pop_back();
        }
        recent.push_front(Node { key, (int32_t)(key % 1000) });
        map.emplace(key, recent.begin());
    }
    uint64_t walk = 0;
    long long vals = 0;
    for (const Node &n : recent) { walk = walk * 31 + (uint64_t)n.key; vals += n.val; }
    emit(hits);
    emit(misses);
    emit(removes);
    emit((long long)recent.size());
    emit_u(walk);
    emit(vals);
    return 0;
}

#else

struct Node { int32_t key, val; uint32_t prev, next; };
struct Slot { int32_t key, idx; };                         // idx < 0: empty.

static std::vector<Node> pool;
static std::vector<uint32_t> freelist;
static std::vector<Slot> slots;
static uint64_t mask;

static long long home(long long key) {
    return (long long)((((uint64_t)key * 0x9E3779B97F4A7C15ull) >> 24) & mask);
}

// The slot holding `key`, or the empty slot where it would go.
static long long find_slot(long long key) {
    long long i = home(key);
    while (slots[(size_t)i].idx >= 0 && slots[(size_t)i].key != key)
        i = (long long)(((uint64_t)i + 1) & mask);
    return i;
}

// Backward-shift deletion: slide later entries of the probe run down over the
// hole, so no tombstones accumulate.
static void map_remove(long long at) {
    long long i = at, j = at;
    for (;;) {
        j = (long long)(((uint64_t)j + 1) & mask);
        Slot s = slots[(size_t)j];
        if (s.idx < 0) break;
        long long h = home(s.key);
        if ((i < j && (h <= i || h > j)) || (i > j && h <= i && h > j)) { slots[(size_t)i] = s; i = j; }
    }
    slots[(size_t)i].idx = -1;
}

static uint32_t alloc_node(int32_t key, int32_t val) {
    if (!freelist.empty()) {
        uint32_t k = freelist.back();
        freelist.pop_back();
        pool[k].key = key;
        pool[k].val = val;
        return k;
    }
    pool.push_back(Node { key, val, 0, 0 });
    return (uint32_t)pool.size() - 1;
}

static void unlink(uint32_t n) {
    uint32_t p = pool[n].prev, q = pool[n].next;
    pool[p].next = q;
    pool[q].prev = p;
}

static void link_front(uint32_t head, uint32_t n) {
    uint32_t q = pool[head].next;
    pool[n].prev = head;
    pool[n].next = q;
    pool[q].prev = n;
    pool[head].next = n;
}

int main() {
    long long nslots = 16;
    while (nslots < CAP * 2) nslots *= 2;
    mask = (uint64_t)(nslots - 1);
    slots.assign((size_t)nslots, Slot { 0, -1 });
    // Two sentinels: the list is never empty, so no link is ever null.
    uint32_t hi = alloc_node(-1, 0), ti = alloc_node(-1, 0);
    pool[hi].next = ti;
    pool[ti].prev = hi;
    long long count = 0, hits = 0, misses = 0, removes = 0;
    uint64_t r = 12345;
    for (long long op = 0; op < BENCH_N; op++) {
        r = xs_next(r);
        long long a = (long long)xs_mod(r, (uint64_t)KEYS);
        long long key = (a * a) / KEYS;                    // Skewed towards small keys.
        long long at = find_slot(key);
        int32_t fi = slots[(size_t)at].idx;
        if (xs_mod(r >> 40, 16) == 0) {
            // An invalidation: drop the entry if it is cached.
            if (fi >= 0) {
                unlink((uint32_t)fi);
                map_remove(at);
                freelist.push_back((uint32_t)fi);
                count--;
                removes++;
            }
            continue;
        }
        if (fi >= 0) {
            pool[(uint32_t)fi].val += 1;
            unlink((uint32_t)fi);
            link_front(hi, (uint32_t)fi);
            hits++;
            continue;
        }
        misses++;
        long long ins = at;
        if (count == CAP) {
            uint32_t lru = pool[ti].prev;
            unlink(lru);
            long long lat = find_slot(pool[lru].key);
            int32_t li = slots[(size_t)lat].idx;
            map_remove(lat);
            freelist.push_back((uint32_t)li);
            count--;
            // The deletion may have shifted entries in this key's probe run,
            // so the empty slot found above can be stale.
            ins = find_slot(key);
        }
        uint32_t ni = alloc_node((int32_t)key, (int32_t)(key % 1000));
        slots[(size_t)ins] = Slot { (int32_t)key, (int32_t)ni };
        link_front(hi, ni);
        count++;
    }
    // Walk the list from most to least recent, so the order is checked too.
    uint64_t walk = 0;
    long long vals = 0;
    for (uint32_t cur = pool[hi].next; pool[cur].key >= 0; cur = pool[cur].next) {
        walk = walk * 31 + (uint64_t)pool[cur].key;
        vals += pool[cur].val;
    }
    emit(hits);
    emit(misses);
    emit(removes);
    emit(count);
    emit_u(walk);
    emit(vals);
    return 0;
}

#endif
