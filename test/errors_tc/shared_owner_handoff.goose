// error: thread_fn parameters must be flat
thread_fn worker(v: SharedOwner) {}
fn main() { let o, ok = shared_alloc(64); thread_spawn(worker, o); }
