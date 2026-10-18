// thread_spawn names its worker through a function parameter bound to a
// thread_fn (§11.2, §7.6) when the generic that spawns it is in another
// module and namespace than the worker, or than its caller.
import spawn.pool;
import spawn.tasks;

thread_fn worker(n: i32) { print("worker ", n); }

fn through<W>(count: i32) { pool::run(count, W); }

fn main() {
    pool::run(2, worker);
    pool::run(1, tasks::worker);
    pool::run_twice(2, tasks::square);
    through(1, worker);
    pool::run_own();
}
