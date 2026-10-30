// no-jit
struct AtomicPacket { tag: u8, next: SharedAtomicU64 }
thread_fn claim(tag: u8, next: SharedAtomicU64) {
    assert(tag == 7);
    loop {
        let job = atomic_fetch_add(next, 1, "Relaxed");
        if job >= 4000 { break; }
        qput(job);
    }
}
thread_fn gated(next: SharedAtomicU64) {
    qput(true);
    qget<u8>();
    atomic_fetch_add(next, 1, "Release");
}
thread_fn atomic_bad(kind: i64) {
    let o, ok = shared_alloc(64); assert(ok);
    if kind == 0 { shared_atomic_u64(o, 1, 0); }
    if kind == 1 { shared_atomic_u64(o, 60, 0); }
    if kind == 2 {
        let access = shared_borrow(shared_view(o, 0, 8));
        shared_atomic_u64(o, 0, 0);
    }
    if kind == 3 { shared_atomic_u64(o, 18446744073709551615, 0); }
    let a = shared_atomic_u64(o, 0, 0);
    if kind == 4 { shared_atomic_u64(o, 0, 0); }
    if kind == 5 { shared_borrow(shared_view(o, 0, 1)); }
    if kind == 6 || kind == 8 {
        assert(shared_close(o));
        if kind == 8 { let newer, yes = shared_alloc(64); assert(yes); }
        atomic_load(a, "Relaxed");
    }
    if kind == 7 { abort("atomic worker abort"); }
}
fn main() {
    let o, ok = shared_alloc(64); assert(ok);
    let next = shared_atomic_u64(o, 0, 0);
    assert(atomic_is_lock_free(next));
    let packet = AtomicPacket { tag: 7, next: next };
    var workers: i64[>..] = [];
    for i in 8 { workers.push(thread_spawn(claim, packet.tag, packet.next)); }
    for id in workers { assert(thread_wait(id)); }
    var seen = default<bool[4000]>();
    for i in 4000 {
        let job = qget<u64>();
        assert(job < 4000 && !seen[job]); seen[job] = true;
    }
    for yes in seen { assert(yes); }
    assert(atomic_load(next, "Acquire") == 4008);
    atomic_store(next, 18446744073709551615, "SeqCst");
    assert(atomic_fetch_add(next, 1, "AcqRel") == 18446744073709551615);
    assert(atomic_load(next, "Relaxed") == 0);
    let observed, swapped = atomic_compare_exchange(next, 0, 99, "AcqRel", "Acquire");
    assert(swapped && observed == 0);
    let old, failed = atomic_compare_exchange(next, 0, 7, "SeqCst", "Relaxed");
    assert(!failed && old == 99);
    let worker = thread_spawn(gated, next);
    assert(qget<bool>()); assert(!shared_close(o));
    qput(1 as u8); assert(thread_wait(worker));
    assert(atomic_load(next, "Acquire") == 100);
    let ordinary = shared_borrow(shared_view(o, 8, 8));
    memory_write<u64>(ordinary, 0, 42, false); shared_release(ordinary);
    assert(shared_close(o));
    for kind in 9 { assert(!thread_wait(thread_spawn_contained(atomic_bad, kind))); }
    print("shared atomics ok");
}
