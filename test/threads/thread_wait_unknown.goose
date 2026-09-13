// An unissued worker ID must abort, including before any worker was spawned.
fn main() { thread_wait(0); }
