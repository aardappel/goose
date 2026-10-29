// error: thread_fn parameters must be flat
root struct S { n: i64 }
thread_fn worker(s: S&) { print(s.n); }
fn main() {
    var s = S { 1 };
    thread_wait(thread_spawn(worker, s));
}
