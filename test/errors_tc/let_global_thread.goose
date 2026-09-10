// Only a const global is a constant a worker may read: a let global's
// contents can be written through a reference to it (§11.2).
let W = 78;
thread_fn worker() { print(W); }
fn main() { thread_wait(thread_spawn(worker)); }
