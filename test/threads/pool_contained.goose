// pool.apply_contained (docs/design/parallel_blocks.md §3.3): a failed check
// or abort(msg) in a task ends that task alone, as in a worker
// thread_spawn_contained starts (§11.2). The apply returns false once every
// other task has ended or been skipped, the first abort's message appended to
// its message buffer where it has one, and the pool runs on: the next apply
// on it, plain or contained, has the same workers. Everything runs on a pool
// of four workers and again on a pool of one, which has no thread of its own,
// so that main runs every task, in order, and skips those after the first
// that aborted. Main gets its own stacks and globals back after an abort in a
// task it ran, as after a return.

import std;

var applies: i64 = 0;

// Each kind fails one runtime check (§9.3) in the chunk of index `bad`; the
// other chunks fill themselves with 2 * idx + 1.
fn fill_chunk(chunk: i64[:], idx: i64, bad: i64, kind: i64) {
    applies += 1;       // the task's copy, not main's
    var scratch: i64[>..] = [];
    for i in idx + 1 { scratch.push(i); }
    if idx == bad {
        let xs = [1, 2, 3];
        if kind == 0 { assert(idx < 0); }
        if kind == 1 { print(xs[idx + 5]); }
        if kind == 2 { abort(str("chunk ", idx, " gives up")); }
        if kind == 3 { print(10 / (kind - 3)); }
    }
    for &x in chunk { x = idx + scratch.len; }
}

// Every chunk aborts, with the same message wherever it runs.
fn refuse(chunk: i64[:]) {
    assert(chunk.len == 0);
}

fn flaky(chunk: i64[:], attempt: i64) {
    assert(attempt >= 2);
    for &x in chunk { x = attempt; }
}

// Where the " (file:line)" that ends an abort's message starts: the file is
// this one's path, which differs between runs of the suite.
fn located(msg: u8[:]) -> i64 {
    var i = msg.len;
    while i > 0 && msg[i - 1] != '(' { i--; }
    if i < 2 { return msg.len; }
    i - 2
}

// A message buffer reached through a reference.
fn refused(pool: thread_pool&, data: i64[:], why: u8[>..]&) -> bool {
    pool.apply_contained(chunks(data, 2), why, refuse)
}

fn run_on(pool: thread_pool&, ordered: bool) {
    var data: i64[>..] = [];
    data.resize(6, 0);
    var log: u8[>..] = "main's";
    for kind in 4 {
        var why: u8[>..] = "why: ";   // The message appends to what is there.
        let ok = pool.apply_contained(chunks(data, 1), why, fill_chunk, 2, kind);
        print("kind ", kind, ": ", ok, " ", why[..located(why)]);
        // In order, the chunks after the one that aborted are skipped.
        if ordered { print("  ", data); }
        data.clear();
        data.resize(6, 0);
    }
    // Every task returns: true, and nothing appended.
    var none: u8[>..] = [];
    let all = pool.apply_contained(chunks(data, 2), none, fill_chunk, -1, 0);
    print("returned: ", all, " ", none.len, " ", data);
    // No message buffer; and the same call spelled as a function.
    print("no buffer: ", pool.apply_contained(chunks(data, 3), refuse), " ",
          apply_contained(pool, chunks(data, 3), fill_chunk, -1, 0), " ", data);
    // A limited array, and a reference, take the message too.
    var short:   u8[..512] = [];
    var offsets: i64[>..]  = [0, 1, 4, 6];
    let fine = apply_contained(pool, chunks(data, offsets), short, refuse);
    var held: u8[>..] = [];
    print("limited: ", fine, " ", short[..located(short)], "; by reference: ",
          refused(pool, data, held), " ", held[..located(held)]);
    var told: u8[>..] = [];
    pool.apply_contained(chunks(data, 3), told, refuse);   // A statement drops the answer.
    print("dropped: ", told[..located(told)]);
    // A plain apply on the pool after the aborts.
    pool.apply(chunks(data, 4), fill_chunk, -1, 0);
    // Retried until every task returns, as a supervisor restarts a worker.
    var attempt = 0;
    var why: u8[>..] = [];
    while !pool.apply_contained(chunks(data, 2), why, flaky, attempt) {
        print("attempt ", attempt, ": ", why[..located(why)]);
        why.clear();
        attempt++;
    }
    log.append(" storage");
    print("attempt ", attempt, " ran: ", data, "; ", log, ", applies ", applies);
}

fn main() {
    var pool = thread_pool(4);
    defer pool.close();
    print("four workers:");
    run_on(pool, false);
    var solo = thread_pool(1);
    defer solo.close();
    print("one worker:");
    run_on(solo, true);
}
