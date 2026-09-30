// error: thread_spawn's first argument names a thread_fn
// The local worker hides the thread_fn of its name (§11.1).
thread_fn worker() { print(1); }

fn main() {
    let worker = 3;
    thread_wait(thread_spawn(worker));
    print(worker);
}
