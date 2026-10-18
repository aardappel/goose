// error: thread_spawn_contained's first argument names a thread_fn
fn work() { print(1); }
fn main() { thread_wait(thread_spawn_contained(work)); }
