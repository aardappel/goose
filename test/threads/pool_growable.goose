// A pool.apply worker whose first parameter is a T[>..]& of its own grows
// output of a size only its run knows (docs/design/parallel_blocks.md §3.4):
// chunks(out, n) hands out n tasks, each starting with an empty array on
// its worker's stacks, and the apply appends what each grew to out, in task
// order, once every task has returned. The size depends on the data, as a
// filter's does, or on scheduling, as where tasks drain a queue of morsels.
// pool.apply_contained appends only where every task returned. Everything
// runs on a pool of four workers and again on a pool of one, which has no
// thread of its own, so that main runs every task, on stacks the pool keeps
// for it, and finds its own as it left them. Both print the same.

import std;

struct Row {
    key:  i64,
    keep: bool,
    tag:  u8[..4],
}

struct Morsel {
    lo: i64,
    hi: i64,
}

// A struct with a resizable tail: out can be that field.
struct Batch {
    id:   i64,
    rows: Row[>..],
}

var appended: i64[>..] = [];

// The rows of morsel idx that a filter keeps: how many is known only once
// it runs.
fn keep_rows(out: Row[>..]&, idx: i64, rows: const Row[:], step: i64) {
    let lo = idx * step;
    let hi = min(lo + step, rows.len);
    for i in lo..hi {
        if rows[i].keep { out.push(rows[i]); }
    }
}

// Each task drains a queue of morsels: which ones it takes, and so how many
// keys it keeps, is known only as it takes them.
fn drain_morsels(out: i64[>..]&, rows: const Row[:]) {
    loop {
        let m, ok = qpoll<Morsel>();
        if !ok { break; }
        for i in m.lo..m.hi {
            if rows[i].keep { out.push(rows[i].key); }
        }
    }
}

// No index, and every way to grow: append, resize and push, with a scratch
// array of the task's own beside the output.
fn grow_all_ways(out: i64[>..]&, n: i64) {
    var scratch: i64[>..] = [];
    for i in n { scratch.push(i * i); }
    out.append(scratch);
    out.resize(out.len + 2, -1);
    out.push(n);
}

// Many elements from the even tasks, none from the odd ones.
fn evens_only(out: i64[>..]&, idx: i64, per: i64) {
    if idx % 2 == 1 { return; }
    for i in per { out.push(idx * per + i); }
}

// What a task pushes before it aborts is not kept.
fn fails_on(out: i64[>..]&, idx: i64, bad: i64) {
    out.push(idx);
    assert(idx != bad);
    out.push(idx * 10);
}

// Where the " (file:line)" that ends an abort's message starts: the file is
// this one's path, which differs between runs of the suite.
fn located(msg: u8[:]) -> i64 {
    var i = msg.len;
    while i > 0 && msg[i - 1] != '(' { i--; }
    if i < 2 { return msg.len; }
    i - 2
}

fn keys(rows: Row[:]) -> i64[>..] {
    var ks: i64[>..] = [];
    for r in rows { ks.push(r.key); }
    return ks;
}

// out reached through a reference, on an array the caller owns.
fn filter_into(pool: thread_pool&, out: Row[>..]&, rows: const Row[:]) {
    pool.apply(chunks(out, 3), keep_rows, rows, 4);
}

// The same, grown round after round in a loop that reads it each round.
fn rounds(pool: thread_pool&, out: i64[>..]&) {
    for r in 3 {
        pool.apply(chunks(out, 2), evens_only, r + 1);
        print("  round ", r, ": ", out.len, " ", out[out.len - 1]);
    }
}

fn run_on(pool: thread_pool&) {
    var rows: Row[>..] = [];
    for i in 20 {
        let tag = if i % 2 == 0 { "even" } else { "odd" };
        rows.push(Row { key: i, keep: i % 3 != 1, tag: tag });
    }

    // A filter: the tasks keep 3, 2, 3, 3, 2 and 0 rows, appended in task
    // order after what out holds.
    var kept: Row[>..] = [Row { key: -1, keep: true, tag: "old" }];
    pool.apply(chunks(kept, 6), keep_rows, rows, 4);
    print("kept ", kept.len, ": ", keys(kept));
    let second = kept[1];    // bce:keep
    print("  ", second, " ", kept[kept.len - 1]);

    // Through a reference, from a slice of the rows.
    var again: Row[>..] = [];
    filter_into(pool, again, rows[8..]);
    print("through a reference: ", keys(again));
    var each: i64[>..] = [];
    rounds(pool, each);
    print("  ", each);

    // A struct's resizable tail, and a global.
    var batch = Batch { id: 7, rows: [] };
    pool.apply(chunks(batch.rows, 2), keep_rows, rows, 3);
    print("batch ", batch.id, ": ", keys(batch.rows));
    appended.clear();
    pool.apply(chunks(appended, 4), evens_only, 2);
    print("global: ", appended);

    // Scheduling decides which task takes which morsel, the rows kept are
    // the same: sorted, they are every key the filter keeps.
    for m in 5 { qput(Morsel { lo: m * 4, hi: m * 4 + 4 }); }
    var drained: i64[>..] = [];
    pool.apply(chunks(drained, 4), drain_morsels, rows);
    sort(drained);
    print("drained ", drained.len, ": ", drained);

    // No index: every task grows the same.
    var grown: i64[>..] = [];
    pool.apply(chunks(grown, 3), grow_all_ways, 3);
    print("grown: ", grown);

    // More tasks than workers, each of the even ones growing far past a
    // page, in order.
    var big: i64[>..] = [];
    pool.apply(chunks(big, 16), evens_only, 5000);
    var sum = 0;
    var ordered = true;
    for x, i in big {
        sum += x;
        if i > 0 && big[i - 1] >= x { ordered = false; }
    }
    print("big: ", big.len, " ", sum, " ", ordered, " ", big[0], " ", big[big.len - 1]);

    // A grow-shrink array takes the output too.
    var stack: i64[>..<] = [1];
    pool.apply(chunks(stack, 2), evens_only, 3);
    stack.pop();
    print("grow-shrink: ", stack);

    // Contained: every task returns, and their output is appended; one
    // aborts, and nothing is, out left as it was.
    var outc: i64[>..] = [100];
    var why: u8[>..] = [];
    let all = pool.apply_contained(chunks(outc, 4), why, fails_on, -1);
    print("contained: ", all, " ", outc, " ", why.len);
    let some = pool.apply_contained(chunks(outc, 4), why, fails_on, 2);
    print("aborted: ", some, " ", outc, " ", why[..located(why)]);
    print("no buffer: ", pool.apply_contained(chunks(outc, 2), fails_on, 1), " ",
          pool.apply_contained(chunks(outc, 2), fails_on, 5), " ", outc);

    // The worker is an ordinary fn too.
    var direct: Row[>..] = [];
    keep_rows(direct, 1, rows, 4);
    print("direct: ", keys(direct));
}

fn main() {
    var pool = thread_pool(4);
    defer pool.close();
    print("four workers:");
    run_on(pool);
    var solo = thread_pool(1);
    defer solo.close();
    print("one worker:");
    run_on(solo);
}
