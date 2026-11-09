// error: thread_fn parameters must be flat
// An own value is not flat: the worker could not release into the
// spawning program's pool.
thread_fn worker(h: own u64) {}
fn main() {}
