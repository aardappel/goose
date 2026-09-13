// error: thread_fn parameters must be flat
// error: , not i64&
// Thread arguments must be flat types (§11.2).
thread_fn worker(r: i64&) {
    print(r);
}
fn main() {
    var x = 1;
    thread_spawn(worker, &x);
}
