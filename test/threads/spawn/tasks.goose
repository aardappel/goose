// A namespace that only declares workers, passed to pool's generics from
// files other than its own.
namespace tasks;

thread_fn square(n: i32) { print("tasks::square ", n * n); }
thread_fn worker(n: i32) { print("tasks::worker ", n); }
