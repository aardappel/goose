import slot_pool;
thread_fn worker() {
    var pool = slot_pool<i64> {};
    let first .= pool.alloc_ref(4);
    pool.free(0);
    pool.alloc_index(9);
    assert(first == 9 && pool.items.len == 1);
    print("worker pool");
}
fn main() { thread_wait(thread_spawn(worker)); }
