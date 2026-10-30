// error: thread_fn parameters must be flat
thread_fn worker(a: SharedAccess) {}
fn main() { let o, ok = shared_alloc(8); thread_spawn(worker, shared_borrow(shared_view(o, 0, 8))); }
