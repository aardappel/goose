// A worker's program copies the globals it uses at spawn (§11.2), which a
// value holding references cannot be.
var views: const u8[:][>..] = [];
thread_fn worker() { print(views.len); }
fn main() {
    views.push("abc");
    thread_wait(thread_spawn(worker));
}
