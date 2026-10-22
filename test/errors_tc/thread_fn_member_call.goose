// error: thread_fn worker is spawned with thread_spawn, not called
// error: thread_fn parameters must be flat (§11.2), not i64[:]
// A member call names the thread_fn as a call by name does, and its
// spawn's checks still hold the declaration to a thread_fn's rules.
thread_fn worker(s: i64[:]) { print(s.len); }
fn main() {
    let a: i64[3] = [1, 2, 3];
    a.worker();
}
