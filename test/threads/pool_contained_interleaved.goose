// A contained thread program, a task of pool.apply_contained or a worker
// thread_spawn_contained starts, is not contained while any pool it applies
// has tasks out (docs/design/parallel_blocks.md §5), and is again once none
// has, in whatever order the pools' waits end. Here pool A's second chunk
// has an argument that applies pool B, and B's second chunk one that
// applies A with no chunk, whose wait runs A's first chunk while B's first
// is still out: A hands out, then B, then A's wait ends, then B's. Once A's
// last wait has ended too, an abort ends the thread program alone.

fn note(chunk: i64[:], word: u8[>..]) {
    for &x in chunk { x = word.len; }
}

fn fill_one(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn word_b(a: thread_pool&, calls: i64&) -> u8[>..] {
    calls++;
    if calls == 2 {
        var none: i64[>..] = [];
        a.apply(chunks(none, 1), fill_one);
    }
    return str("b");
}

fn word_a(a: thread_pool&, b: thread_pool&, calls: i64&) -> u8[>..] {
    calls++;
    if calls == 2 {
        var xb: i64[>..] = [0, 0];
        var calls_b = 0;
        b.apply(chunks(xb, 1), note, word_b(a, calls_b));
    }
    return str("a");
}

// Pools of one worker, whose tasks stay queued until a wait runs them.
fn interleave() -> i64 {
    var a = thread_pool(1);
    defer a.close();
    var b = thread_pool(1);
    defer b.close();
    var xa: i64[>..] = [0, 0];
    var calls_a = 0;
    a.apply(chunks(xa, 1), note, word_a(a, b, calls_a));
    return xa[0] + xa[1];
}

fn task(chunk: i64[:]) {
    chunk[0] = interleave();
    assert(chunk[0] < 0);
}

thread_fn worker() {
    print("worker's pools: ", interleave());
    assert(false);
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
    var why: u8[>..] = [];
    for workers in 1..3 {
        var pool = thread_pool(workers);
        defer pool.close();
        var res: i64[>..] = [0];
        let ok = pool.apply_contained(chunks(res, 1), why, task);
        print("task on ", workers, " worker(s): ", ok, " ", why[..located(why)], " ", res);
        why.clear();
    }
    let w = thread_wait(thread_spawn_contained(worker), why);
    print("worker: ", w, " ", why[..located(why)]);
    print("main runs on");
}
