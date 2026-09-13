// A function that only spawns still assembles the argument packet on a data
// stack of its own, not on its caller's.
thread_fn worker(a: i64) { print(a); }
fn spawn_one(a: i64) {
    thread_wait(thread_spawn(worker, a));
}
fn main() {
    var s: u8[>..] = [];
    s.push('x');
    spawn_one(5);
    s.push('y');
    print(s);
}
