// The library side of thread_spawn_modules: its generics spawn whichever
// thread_fn their caller binds W to, wherever that is declared (§11.2).
namespace pool;

import .tasks;

// One at a time, so the workers print in order.
fn run<W>(count: i32) {
    for i in count { thread_wait(thread_spawn(W, i as i32)); }
}

fn run_twice<W>(count: i32) { run(count, W); run(count, W); }

thread_fn echo(n: i32) { print("pool::echo ", n); }

// A bare name in this namespace binds pool's own worker, and a qualified
// one another namespace's.
fn run_own() {
    run(1, echo);
    run(3, tasks::square);
}
