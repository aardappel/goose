// error: thread_fn parameters must be flat
thread_fn worker(v: i64&) {}
fn main() { var x = 1; thread_spawn(worker, &x); }
