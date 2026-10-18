// runtime-define: GS_DEBUG=1
// thread_spawn_contained (§11.2): each kind of abort ends only its worker, and
// every thread_wait for it answers false. Other workers, waiters, and a
// replacement worker run on. A debug build, for the checks only it has.

struct Job { n: i64 }
struct Done { n: i64 }
struct Ready { n: i64 }
struct Seen { ok: bool }

// Each kind fails one runtime check (§9.3), but the first, which returns.
thread_fn job(kind: i64) {
    let xs = [1, 2, 3];
    var lim: i64[..2] = [];
    reusable var pool: i64[>..] = [1];
    if kind == 0 { print("job 0 returns"); }
    if kind == 1 { print(xs[kind + 5]); }
    if kind == 2 { assert(kind == 0); }
    if kind == 3 { abort(str("job ", kind, " gives up")); }
    if kind == 4 { print(10 / (kind - 4)); }
    if kind == 5 { for i in kind { lim.push(i); } }
    if kind == 6 { pool.free(kind); }
    if kind == 7 { thread_wait(-kind); }    // Aborts holding the runtime's lock.
    if kind == 8 { let big = (kind * 250000000) as i32; print(big + big); }
    if kind == 9 { print((kind * 32) as u8); }
}

// Waits for a job it then answers: it runs through the others' aborts.
thread_fn bystander() {
    let j = qget<Job>();
    qput(Done { n: j.n + 1 });
}

thread_fn parent() {
    print("child: ", thread_wait(thread_spawn_contained(job, 1)));
    print("parent runs on");
    assert(false);
}

thread_fn gated() {
    qget<i64>();
    assert(false);
}

thread_fn waiter(id: i64) {
    qput(Ready { n: 1 });
    qput(Seen { ok: thread_wait(id) });
}

thread_fn flaky(attempt: i64) {
    assert(attempt >= 3);
    print("attempt ", attempt, " ran");
}

// Starts the worker again until it returns, as a server's supervisor does.
fn supervise<W>() -> i64 {
    var attempt = 0;
    while !thread_wait(thread_spawn_contained(W, attempt)) { attempt++; }
    attempt
}

fn main() {
    let other = thread_spawn(bystander);
    for kind in 10 {
        let id = thread_spawn_contained(job, kind);
        let first = thread_wait(id);
        print("job ", kind, ": ", first, " ", thread_wait(id));
    }
    thread_wait(thread_spawn_contained(job, 0));    // A statement discards the answer.
    qput(Job { n: 41 });
    print("bystander answers ", qget<Done>().n, ", ", thread_wait(other));
    print("parent: ", thread_wait(thread_spawn_contained(parent)));

    // Main and a plain worker both wait for a contained worker that aborts.
    let id = thread_spawn_contained(gated);
    let w = thread_spawn(waiter, id);
    qget<Ready>();
    qput(0);
    print("main: ", thread_wait(id));
    print("waiter: ", qget<Seen>().ok, ", ", thread_wait(w));

    print("succeeded after ", supervise(flaky), " restarts");
}
