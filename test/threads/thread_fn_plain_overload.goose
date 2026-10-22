// A plain function may share a thread_fn's name: thread_spawn takes the
// thread_fn (§11.2), and a call the plain function its arguments resolve to,
// directly, as a member or through a function value. The calls that resolve
// to the thread_fn are errors_tc/thread_fn_*.
thread_fn worker(n: i64) { print("worker ", n); }
fn worker(x: f64) { print("plain ", x); }
fn apply<F>(x: f64) { F(x); }
fn main() {
    thread_wait(thread_spawn(worker, 7));
    worker(1.5);
    let y = 2.5;
    y.worker();
    apply(3.5, worker);
}
