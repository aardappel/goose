// Each pool.apply_contained keeps to its own tasks' aborts
// (docs/design/parallel_blocks.md §3.3), as where an argument of an apply
// applies the same pool again between the apply's tasks. A resizable
// argument is built for each chunk, once the chunks before it are handed
// out, and the inner apply's wait runs those too, on a pool of one worker
// every one of them. The outer apply's first chunk aborts in there: each
// inner apply says whether a task of its own aborted, and the outer one that
// its own did, with that task's message. On a pool of one worker the outer
// apply's chunks after the first, which the inner waits take after the
// abort, are skipped.

var rounds: i64 = 0;

fn first_fails(chunk: i64[:], idx: i64, word: u8[>..]) {
    assert(idx != 0);
    for &x in chunk { x = word.len; }
}

fn fill_one(chunk: i64[:]) {
    for &x in chunk { x = 1; }
}

fn second_fails(chunk: i64[:], idx: i64) {
    if idx == 1 { abort("inner chunk 1 gives up"); }
    for &x in chunk { x = 1; }
}

// Where the " (file:line)" that ends an abort's message starts: the file is
// this one's path, which differs between runs of the suite.
fn located(msg: u8[:]) -> i64 {
    var i = msg.len;
    while i > 0 && msg[i - 1] != '(' { i--; }
    if i < 2 { return msg.len; }
    i - 2
}

// Built for each chunk of the outer apply, on its pool: an inner apply
// whose tasks return, one whose second task aborts, and a plain one.
fn word_of(pool: thread_pool&, ordered: bool) -> u8[>..] {
    let round = rounds;
    rounds++;
    var xs:  i64[>..] = [0, 0, 0];
    var why: u8[>..]  = [];
    var ok = true;
    if round % 3 == 0 { ok = pool.apply_contained(chunks(xs, 1), why, fill_one); }
    if round % 3 == 1 { ok = pool.apply_contained(chunks(xs, 1), why, second_fails); }
    if round % 3 == 2 { pool.apply(chunks(xs, 1), fill_one); }
    print("  inner ", round % 3, ": ", ok, " ", why[..located(why)]);
    if ordered { print("    ", xs); }
    return str("w");
}

fn run_on(pool: thread_pool&, ordered: bool) {
    var data: i64[>..] = [0, 0, 0];
    var why:  u8[>..]  = [];
    rounds = 0;
    let ok = pool.apply_contained(chunks(data, 1), why, first_fails, word_of(pool, ordered));
    print("outer: ", ok, " ", why[..located(why)]);
    if ordered { print("  ", data); }
}

fn main() {
    var solo = thread_pool(1);
    defer solo.close();
    print("one worker:");
    run_on(solo, true);
    var pool = thread_pool(4);
    defer pool.close();
    print("four workers:");
    run_on(pool, false);
}
