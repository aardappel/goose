// Producers that retry a full queue (§11.2): every put qput accepts is
// received once, and the refusals leave the limit as it was.
import os;

struct Job { from: i64, n: i64 }
struct Done { from: i64, refused: i64 }

thread_fn producer(from: i64, count: i64) {
    var refused = 0;
    for n in count {
        // A refused producer sleeps rather than spin, so the test stays
        // light on a machine with few cores.
        while !qput(Job { from: from, n: n }) { refused++; sleep_ms(1); }
    }
    qput(Done { from: from, refused: refused });
}

fn main() {
    qlimit<Job>(4);
    var ids: i64[>..] = [];
    for p in 4 { ids.push(thread_spawn(producer, p, 200)); }
    // A producer's Done follows its last Job, so once all four are in,
    // what is left of the jobs is queued.
    var got = 0;
    var sum = 0;
    var done = 0;
    while done < 4 {
        let j, ok = qget<Job>(1);
        if ok { got++; sum += j.n; }
        let d, dok = qpoll<Done>();
        if dok { done++; }
    }
    var more = true;
    while more {
        let j, ok = qpoll<Job>();
        if ok { got++; sum += j.n; }
        more = ok;
    }
    for id in ids { thread_wait(id); }
    print(got, " ", sum);                                   // 800 79600

    // The queue is empty and its limit admits exactly four again.
    var admitted = 0;
    for n in 10 { if qput(Job { from: 0, n: n }) { admitted++; } }
    print(admitted);                                        // 4
}
