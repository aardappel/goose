// Fixed-size values with stable slots and a LIFO freelist.
// A free marks a slot reusable; it does not destroy or remove its value.
// References remain valid and observe the replacement when that slot is reused.
// Like reusable, double frees are logical errors, not tracked occupancy checks.
// Treat free_slots as bookkeeping and do not shrink or replace items directly.
root struct slot_pool<T> {
    items:      T[>..]    = [],
    free_slots: i64[>..<] = [],
}

fn alloc_index<T>(pool: slot_pool<T>&, value: T) -> i64 {
    if pool.free_slots.len != 0 {
        let i = pool.free_slots.pop();
        pool.items[i] .= value;
        return i;
    }
    let i = pool.items.len;
    pool.items.push(value);
    return i;
}

fn alloc_ref<T>(pool: slot_pool<T>&, value: T) -> T& {
    if pool.free_slots.len != 0 {
        let i = pool.free_slots.pop();
        pool.items[i] .= value;
        return pool.items[i];
    }
    return pool.items.push(value);
}

// Select the destination before calling F, then construct its result there.
// This supports relative links whose offsets depend on the receiving slot.
fn alloc_with<T, F>(pool: slot_pool<T>&) -> T& {
    if pool.free_slots.len != 0 {
        let i = pool.free_slots.pop();
        pool.items[i] .= F();
        return pool.items[i];
    }
    return pool.items.push(F());
}

fn alloc_index_with<T, F>(pool: slot_pool<T>&) -> i64 {
    if pool.free_slots.len != 0 {
        let i = pool.free_slots.pop();
        pool.items[i] .= F();
        return i;
    }
    pool.items.push(F());
    return pool.items.len - 1;
}

fn free<T>(pool: slot_pool<T>&, i: i64) {
    if i < 0 || i >= pool.items.len { abort("slot_pool.free: index is not a slot of this pool"); }
    pool.free_slots.push(i);
}

fn free_ref<T>(pool: slot_pool<T>&, value: T&) {
    free(pool, pool.items.index_of(value));
}

// Retain storage and values, but make every slot available again, in index
// order. Existing references observe replacements just as with individual frees.
fn recycle_all<T>(pool: slot_pool<T>&) {
    pool.free_slots.clear();
    var i = pool.items.len;
    while i > 0 { i--; pool.free_slots.push(i); }
}
