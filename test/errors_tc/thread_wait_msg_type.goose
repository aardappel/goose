// error: thread_wait(id, msg) appends to a growable u8 array, not i64[>..]
thread_fn work() { print(1); }
fn main() {
    var msg: i64[>..] = [];
    thread_wait(thread_spawn_contained(work), msg);
}
