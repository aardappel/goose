// A library for the C host test/threads/host_exit.c (run_tests.py's
// host_exit task): the host starts it with goose_init, calls its exports,
// ends it with goose_exit and starts it again, each start running main over
// fresh globals, a fresh thread pool and an empty queue; and once ends it
// while a worker still runs, which is a runtime error. As a fixture, main
// runs once.

struct Note { n: i64 }
struct Stop { n: i64 }

var calls: i64;
var seen: i64[>..] = [];
// No scope's exit destroys a pool, a global's or a local's: goose_exit
// destroys those the program leaves open, joining their workers, and the next
// start makes a new one.
var pool = thread_pool(2);

// Never gets its Stop: a worker that runs for as long as the program does.
thread_fn parked() {
    let s = qget<Stop>();
    print("parked: ", s.n);
}

// Each of seen's elements its place in it, counted from 1.
fn number(chunk: i64[:], idx: i64) {
    chunk[0] = idx + 1;
}

export fn bump() -> i64 {
    calls += 1;
    seen.push(0);
    pool.apply(chunks(seen, 1), number);
    var mine = thread_pool(1);  // left open, for goose_exit to destroy
    mine.apply(chunks(seen, 1), number);
    qput(Note { n: calls });    // left in the queue, for goose_exit to free
    return seen[seen.len - 1];
}

export fn park() {
    thread_spawn(parked);
}

fn main() {
    print("main: calls ", calls, " seen ", seen.len);
}
