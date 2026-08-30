// Thread programs may not access globals (§11.2): they would be shared
// mutable memory between programs.
var counter = 0;
thread_fn worker(n: int) {
    counter += n;
}
fn main() {
    thread_wait(thread_spawn(worker, 1));
    print(counter);
}
