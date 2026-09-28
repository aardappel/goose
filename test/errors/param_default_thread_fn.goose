// error: thread_fn parameter n cannot have a default
// thread_spawn passes a thread_fn every argument (§11.2).
thread_fn worker(n: i64 = 1) { print(n); }
fn main() { }
