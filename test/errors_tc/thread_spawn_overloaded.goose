// error: ambiguous thread_spawn of worker: more than one thread_fn has that name
// A spawn names one worker: its arguments do not choose among thread_fns of
// one name (§11.2).
thread_fn worker(x: i64) { print("int ", x); }
thread_fn worker(x: f64) { print("flt ", x); }
fn main() {
    let id = thread_spawn(worker, 7);
    thread_wait(id);
}
