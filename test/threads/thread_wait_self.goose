// An otherwise valid worker ID cannot be used to wait for the same worker.
thread_fn worker() { thread_wait(qget<i64>()); }
fn main() {
    let id = thread_spawn(worker);
    qput(id);
    thread_wait(id);
}
