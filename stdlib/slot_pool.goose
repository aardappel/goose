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

// Owning handles (docs/design/ownership.md). take gives a slot as an
// `own T&`, a claim the checker holds its holder to passing to release
// exactly once; anything else borrows the handle as the T& it is. The
// index and reference operations above stay, unchecked: a pool is in the
// own world as far as its slots are taken and released through these.
// A slot reused by take was released, so what it held owns nothing.
trusted fn take<T>(pool: slot_pool<T>&, value: T) -> own T& {
    if pool.free_slots.len != 0 {
        let i = pool.free_slots.pop();
        pool.items[i] .= value;
        return pool.items[i];
    }
    return pool.items.push(value);
}

// As alloc_with: F builds the value in the slot take chose.
trusted fn take_with<T, F>(pool: slot_pool<T>&) -> own T& {
    if pool.free_slots.len != 0 {
        let i = pool.free_slots.pop();
        pool.items[i] .= F();
        return pool.items[i];
    }
    return pool.items.push(F());
}

// Ends the claim of h and makes its slot available again. A T holding own
// values of its own takes the form with a block, which is given the
// element and must take() every own field of it: the checker refuses a
// release that leaves one.
// A handle kept in a field and taken out again is no longer provably this
// pool's to the checker, which the release then checks as it runs.
trusted fn release<T>(pool: slot_pool<T>&, h: own T&) {
    free(pool, pool.items.index_of(h, "slot_pool.release: handle is not a slot of this pool"));
}

trusted fn release<T, F>(pool: slot_pool<T>&, h: own T&) {
    F(h);
    free(pool, pool.items.index_of(h, "slot_pool.release: handle is not a slot of this pool"));
}

// Empties the pool: F is given every element, moved out, live or free, to
// consume what it owns, a released one owning nothing. A local pool whose
// elements hold own values must end this way, and no handle into the pool
// may be used after it.
fn release_all<T, F>(pool: slot_pool<T>&) {
    while pool.items.len > 0 { F(pool.items.pop()); }
    pool.free_slots.clear();
}
