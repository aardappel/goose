// Bounded queues (§11.2): a queue's limit makes qput false at once, and
// qget<T>(ms) waits at most ms milliseconds.
import os;

struct Job { n: i64 }

thread_fn late(ms: i64) {
    sleep_ms(ms);
    qput(Job { n: 7 });
}

fn main() {
    // A limit counts messages.
    qlimit<Job>(2);
    print(qput(Job { n: 1 }), " ", qput(Job { n: 2 }), " ", qput(Job { n: 3 }));  // true true false
    print(qget<Job>().n, " ", qput(Job { n: 4 }));          // 1 true
    // A lower limit keeps what is queued; puts fail until the queue is below it.
    qlimit<Job>(1);
    print(qget<Job>().n, " ", qput(Job { n: 5 }));          // 2 false
    print(qget<Job>().n, " ", qput(Job { n: 6 }));          // 4 true
    print(qget<Job>().n);                                   // 6
    qlimit<Job>(1000);
    qput(Job { n: 8 });                                     // a statement drops the bool
    let p, gotp = qpoll<Job>();
    let e, gote = qpoll<Job>();
    print(p.n, " ", gotp, " ", e.n, " ", gote);             // 8 true 0 false

    // A wait with nothing queued ends after its time. Windows's clock moves
    // in steps of about 16 ms, so the wait may end that much early.
    let t0 = clock_ns();
    let j, got = qget<Job>(50);
    let waited = (clock_ns() - t0) / 1000000;
    print(j.n, " ", got, " ", waited >= 25 && waited < 5000);  // 0 false true
    let z, gotz = qget<Job>(0);
    print(z.n, " ", gotz);                                  // 0 false
    // A wait of less than nothing does not wait either.
    let t1 = clock_ns();
    let m, gotm = qget<Job>(-5);
    print(m.n, " ", gotm, " ", (clock_ns() - t1) / 1000000 < 5000);  // 0 false true
    // A value another thread puts before the time is up.
    let id = thread_spawn(late, 20);
    let k, gotk = qget<Job>(60000);
    print(k.n, " ", gotk);                                  // 7 true
    thread_wait(id);

    // Each type's queue has its own limit.
    qlimit<u8[>..]>(1);
    var bulk: u8[>..] = [];
    bulk.resize(600, 'x');
    print(qput(bulk), " ", qput(bulk), " ", qput(Job { n: 9 }));  // true false true
    print(qget<u8[>..]>().len, " ", qput(bulk));            // 600 true
    print(qget<u8[>..]>().len, " ", qget<Job>().n);         // 600 9

    // qput is an expression: a refusal ends a loop, and `&&` puts only
    // when its left side holds.
    qlimit<i64>(3);
    var n = 0;
    while qput(n) { n++; }
    print(n, " ", false && qput(n), " ", qget<i64>() == 0 && qput(n));  // 3 false true
    let x, gotx = qpoll<i64>();
    let y, goty = qpoll<i64>();
    let w, gotw = qpoll<i64>();
    let v, gotv = qpoll<i64>();
    print(x, " ", y, " ", w, " ", gotx && goty && gotw, " ", gotv);  // 1 2 3 true false
}
