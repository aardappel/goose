// The Goose standard library: `dictionary` (docs/stdlib.md).
// Open addressing with linear probing over a power-of-two slot array,
// hash(key) & mask, backward-shift deletion (no tombstones). Keys and
// values are fixed-size; strings are keyed as u8[:] slices into text the
// caller owns, or as inline u8[..k]. A user key type provides
// `fn hash(k: key) -> u64` (and `==`).
//
// The slot array is grow-shrink so that growth can replace it whole
// through a reference (spec §5.2); as a consequence a reference obtained
// from get/get_or_insert, or a binder inside each, may not be in scope at
// the next insert, update, remove or clear (the checker says so).

import std;

struct dictionary_slot<K, V> { key: K, val: V, used: bool }
struct dictionary<K, V> { count: i64 = 0, slots: dictionary_slot<K, V>[>..<] = [] }

// The slot holding key, or -1.
fn dict_probe<K, V>(d: dictionary<K, V>&, key: K) -> i64 {
    if d.slots.len == 0 { return -1; }
    let mask = d.slots.len - 1;
    var i = (hash(key) as i64) & mask;
    loop {
        if !d.slots[i].used { return -1; }
        if d.slots[i].key == key { return i; }
        i = (i + 1) & mask;
    }
}

// A fresh table of cap slots (a power of two), the used entries re-placed.
fn dict_grow<K, V>(d: dictionary<K, V>&, cap: i64) {
    var ns: dictionary_slot<K, V>[>..<] = [];
    push_n(ns, dictionary_slot<K, V> { default<K>(), default<V>(), false }, cap);
    let mask = cap - 1;
    for s in d.slots {
        if s.used {
            var i = (hash(s.key) as i64) & mask;
            while ns[i].used { i = (i + 1) & mask; }
            ns[i] = s;
        }
    }
    d.slots = copy(ns);
}

// A reference to the value for key, or null.
fn get<K, V>(d: dictionary<K, V>&, key: K) -> V? {
    let i = dict_probe(d, key);
    if i < 0 { return null; }
    return d.slots[i].val;
}

fn contains<K, V>(d: dictionary<K, V>&, key: K) -> bool { dict_probe(d, key) >= 0 }

// Sets key to val; true if the key was new.
fn insert<K, V>(d: dictionary<K, V>&, key: K, val: V) -> bool {
    if (d.count + 1) * 3 > d.slots.len * 2 { dict_grow(d, max(d.slots.len * 2, 8)); }
    let mask = d.slots.len - 1;
    var i = (hash(key) as i64) & mask;
    while d.slots[i].used {
        if d.slots[i].key == key {
            d.slots[i].val = val;
            return false;
        }
        i = (i + 1) & mask;
    }
    d.slots[i] = dictionary_slot<K, V> { key, val, true };
    d.count += 1;
    return true;
}

// The value for key, inserted as v0 first if absent.
fn get_or_insert<K, V>(d: dictionary<K, V>&, key: K, v0: V) -> V& {
    var i = dict_probe(d, key);
    if i < 0 {
        insert(d, key, v0);
        i = dict_probe(d, key);
    }
    return d.slots[i].val;
}

// F(val&) on the value for key, inserted as v0 first if absent.
fn update<K, V, F>(d: dictionary<K, V>&, key: K, v0: V) {
    var i = dict_probe(d, key);
    if i < 0 {
        insert(d, key, v0);
        i = dict_probe(d, key);
    }
    F(&d.slots[i].val);
}

// Removes key; true if it was there.
fn remove<K, V>(d: dictionary<K, V>&, key: K) -> bool {
    var i = dict_probe(d, key);
    if i < 0 { return false; }
    let mask = d.slots.len - 1;
    var j = i;
    loop {
        j = (j + 1) & mask;
        if !d.slots[j].used { break; }
        // An entry whose home is not in the cyclic range (i, j] can move up
        // into the hole without leaving its probe sequence.
        let h = (hash(d.slots[j].key) as i64) & mask;
        var stays = false;
        if i <= j { stays = h > i && h <= j; } else { stays = h > i || h <= j; }
        if !stays {
            d.slots[i] = d.slots[j];
            i = j;
        }
    }
    d.slots[i] = dictionary_slot<K, V> { default<K>(), default<V>(), false };
    d.count -= 1;
    return true;
}

fn clear<K, V>(d: dictionary<K, V>&) {
    for &s in d.slots { s.used = false; }
    d.count = 0;
}

// Room for n entries without a rehash.
fn reserve<K, V>(d: dictionary<K, V>&, n: i64) {
    let need = max(next_pow2((n * 3 + 1) / 2), 8);
    if need > d.slots.len { dict_grow(d, need); }
}

// F(key, val&) for every entry, in no particular order.
fn each<K, V, F>(d: dictionary<K, V>&) {
    for &s in d.slots { if s.used { F(s.key, &s.val); } }
}
