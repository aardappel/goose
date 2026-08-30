// Thread arguments must be flat types (§11.2).
thread_fn worker(r: int&) {
    print(r);
}
fn main() {
    var x = 1;
    thread_spawn(worker, &x);
}
