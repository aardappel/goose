// Pool-relative references end to end (§3.9): a global `reusable` pool, a
// separate slot array whose links point into it, sentinels built with `self`,
// and `index_of` turning a link back into the index `free` wants. Every
// offset here is measured from `kpool`'s base, so the slot array can hold
// 4-byte links into the pool and a slot copies like any other value.

struct KNode { key: i64, prev: KNode&<u32 in kpool>, next: KNode&<u32 in kpool> }
struct KSlot { key: i64, node: KNode&<u32 in kpool>? }   // node == null: empty.

reusable var kpool: KNode[>..] = [];
var kslots: KSlot[>..] = [];
var kmask: u64 = 0;

fn khome(key: i64) -> i64 { ((key as u64 * 0x9E3779B97F4A7C15 >> 24) & kmask) as! i64 }

// The slot holding `key`, or the empty slot where it would go.
fn kfind(key: i64) -> i64 {
    var i = khome(key);
    loop {
        let s = kslots[i];
        guard s.node != null && s.key != key;
        i = (((i + 1) as u64) & kmask) as! i64;
    }
    return i;
}

// Backward-shift deletion: later entries of the probe run slide down over the
// hole. The slots being copied hold pool-relative links, which mean the same
// wherever the slot sits.
fn kremove(at: i64) {
    var i = at;
    var j = i;
    loop {
        j = (((j + 1) as u64) & kmask) as! i64;
        let s = kslots[j];
        guard s.node != null;
        let h = khome(s.key);
        if (i < j && (h <= i || h > j)) || (i > j && h <= i && h > j) {
            kslots[i] = s;
            i = j;
        }
    }
    kslots[i].node .= null;
}

fn kunlink(n: KNode&) {
    let p: KNode& = n.prev;
    let q: KNode& = n.next;
    p.next .= q;
    q.prev .= p;
}

fn kfront(head: KNode&, n: KNode&) {
    let q: KNode& = head.next;
    n.prev .= head;
    n.next .= q;
    q.prev .= n;
    head.next .= n;
}

// The keys from most to least recent, as one decimal number.
fn korder() -> i64 {
    var order = 0;
    var cur: KNode& = kpool[0].next;
    loop {
        guard cur.key >= 0;
        order = order * 10 + cur.key;
        cur .= cur.next;
    }
    order
}

fn kadd(key: i64) {
    let i = kpool.alloc_index(KNode { key: key, prev: self, next: self });
    let n = &kpool[i];
    kfront(kpool[0], n);
    kslots[kfind(key)] = KSlot { key: key, node: n };
}

fn main_inpool_lru() {
    for s in 16 { kslots.push(KSlot { key: 0 }); }
    kmask = 15;
    // Two sentinels, so no list link is ever null; `self` gives each one its
    // own offset in the pool, which only a literal built there has.
    let hi = kpool.alloc_index(KNode { key: -1, prev: self, next: self });
    let ti = kpool.alloc_index(KNode { key: -1, prev: self, next: self });
    kpool[hi].next .= &kpool[ti];
    kpool[ti].prev .= &kpool[hi];
    print(hi);                           // 0
    print(ti);                           // 1
    for k in 4 { kadd(k + 1); }
    print(kpool.len);                    // 6
    print(korder());                     // 4321
    // Touch key 2 through the slot array: the link read back out of a slot is
    // rooted at kpool exactly, so it relinks without any index in sight.
    let m = kslots[kfind(2)].node;
    assert(m);
    kunlink(m);
    kfront(kpool[hi], m);
    print(korder());                     // 2431
    // Evict it: index_of is what turns the link back into a freelist index.
    let at = kfind(2);
    kunlink(m);
    kremove(at);
    kpool.free(kpool.index_of(m));
    print(korder());                     // 431
    kadd(9);
    print(kpool.len);                    // 6: the freed slot came back
    print(korder());                     // 9431
    // A slot copies by value, links and all.
    let c = kslots[kfind(3)];
    let cn = c.node;
    assert(cn);
    print(cn.key);                       // 3
    print(kpool.index_of(cn));           // 4: two sentinels, then keys 1 and 3
    // The probe run survived the backward shift.
    print(kfind(1) != kfind(9));         // true
    var found = 0;
    for k in 4 { if kslots[kfind(k + 1)].node != null { found += 1; } }
    print(found);                        // 3: key 2 was evicted
}

fn main() { main_inpool_lru(); }
