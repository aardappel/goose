// error: thread_fn worker is spawned with thread_spawn, not called
// A function value bound to a thread_fn may be spawned (§11.2), but calling
// it calls the thread_fn.
thread_fn worker(n: i64) { print(n); }
fn apply<F>(n: i64) { F(n); }
fn main() { apply(5, worker); }
