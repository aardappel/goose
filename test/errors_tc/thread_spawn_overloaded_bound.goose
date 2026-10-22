// error: ambiguous thread_spawn_contained of W: more than one thread_fn has that name
// A function parameter bound to a name with two thread_fns passes them both
// (§7.6), which the spawn does not choose among.
thread_fn worker(x: i64) { print("int ", x); }
thread_fn worker(x: f64) { print("flt ", x); }
fn spawn<W>(n: i64) -> i64 { return thread_spawn_contained(W, n); }
fn main() { thread_wait(spawn(7, worker)); }
