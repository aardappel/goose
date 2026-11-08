// An abort's message the runtime hands over for its append to a message
// buffer is freed however the append ends, here each time in a contained
// thread, which ends as a check aborts: by pool.apply_contained in a worker
// thread_spawn_contained starts, by thread_wait(id, msg) in one, and by
// pool.apply_contained in a task of another (§11.2,
// docs/design/parallel_blocks.md §3.3). A limited buffer too small for the
// message fails its capacity check (§9.3) with the message in hand, and
// frees it then. The bounds check on the way to an element of an array of
// buffers is made once, where the buffer is resolved, among the arguments,
// before any message is handed over, and the append writes where it found
// the buffer: also where a later argument has popped the limited array
// the buffer is in, whose slots stay storage (§5.3). The sanitizer
// profile's leak check sees a message that is not freed.

fn refuse(chunk: i64[:]) {
    assert(chunk.len == 0);
}

fn refuse_n(chunk: i64[:], n: i64) {
    assert(chunk.len == n);
}

fn shrink(xs: u8[..256][..2]&) -> i64 {
    xs.pop();
    0
}

// Pools of one worker, which the abort leaves open, start no thread.
thread_fn applier() {
    var pool = thread_pool(1);
    defer pool.close();
    var data:  i64[>..] = [0, 0];
    var short: u8[..1]  = [];
    print("not reached ", pool.apply_contained(chunks(data, 1), short, refuse));
}

thread_fn indexed_applier(k: i64) {
    var pool = thread_pool(1);
    defer pool.close();
    var data: i64[>..]    = [0, 0];
    var bufs: u8[..64][2] = [[], []];
    print("not reached ", pool.apply_contained(chunks(data, 1), bufs[k], refuse));
}

thread_fn popped_applier(k: i64) {
    var pool = thread_pool(1);
    defer pool.close();
    var data: i64[>..]       = [0, 0];
    var bufs: u8[..256][..2] = [[], []];
    let ok = pool.apply_contained(chunks(data, 1), bufs[k], refuse_n, shrink(bufs));
    print("  popped: ", ok, " ", bufs.len);
}

thread_fn job() { assert(false); }

thread_fn waiter() {
    var short: u8[..1] = [];
    print("not reached ", thread_wait(thread_spawn_contained(job), short));
}

thread_fn indexed_waiter(k: i64) {
    var bufs: u8[..64][2] = [[], []];
    print("not reached ", thread_wait(thread_spawn_contained(job), bufs[k]));
}

fn nested(chunk: i64[:]) {
    var inner = thread_pool(1);
    defer inner.close();
    var data:  i64[>..] = [0, 0];
    var short: u8[..1]  = [];
    print("not reached ", inner.apply_contained(chunks(data, 1), short, refuse));
}

// Where the " (file:line)" that ends an abort's message starts: the file is
// this one's path, which differs between runs of the suite.
fn located(msg: u8[:]) -> i64 {
    var i = msg.len;
    while i > 0 && msg[i - 1] != '(' { i--; }
    if i < 2 { return msg.len; }
    i - 2
}

fn main() {
    var pool = thread_pool(2);
    defer pool.close();
    var results: i64[>..] = [0];
    for round in 3 {
        var why: u8[>..] = [];
        let a = thread_wait(thread_spawn_contained(applier), why);
        print("worker's apply_contained: ", a, " ", why[..located(why)]);
        why.clear();
        let ai = thread_wait(thread_spawn_contained(indexed_applier, 5), why);
        print("worker's apply_contained, indexed: ", ai, " ", why[..located(why)]);
        why.clear();
        let ap = thread_wait(thread_spawn_contained(popped_applier, 1), why);
        print("worker's apply_contained, popped: ", ap, " ", why.len);
        why.clear();
        let w = thread_wait(thread_spawn_contained(waiter), why);
        print("worker's thread_wait: ", w, " ", why[..located(why)]);
        why.clear();
        let wi = thread_wait(thread_spawn_contained(indexed_waiter, 5), why);
        print("worker's thread_wait, indexed: ", wi, " ", why[..located(why)]);
        why.clear();
        let t = pool.apply_contained(chunks(results, 1), why, nested);
        print("task's apply_contained: ", t, " ", why[..located(why)]);
    }
}
