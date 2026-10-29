import slice_pool;
thread_fn worker() {
    var p = slice_pool<i64> {};
    let first = p.alloc_slice(3);
    first[1] = 9;
    p.free_slice(first);
    let second = p.alloc_slice(4);
    assert(first[1] == 0 && second.len == 4);
    print("worker slices");
}
fn main() { thread_wait(thread_spawn(worker)); }
