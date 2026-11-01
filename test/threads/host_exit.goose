// A library for the C host test/threads/host_exit.c (run_tests.py's
// host_exit task): the host starts it with goose_init, calls its exports,
// ends it with goose_exit and starts it again, each start running main over
// fresh globals and an empty queue; and once ends it while a worker still
// runs, which is a runtime error. As a fixture, main runs once.

struct Note { n: i64 }
struct Stop { n: i64 }

var calls: i64;
var seen: i64[>..] = [];

// Never gets its Stop: a worker that runs for as long as the program does.
thread_fn parked() {
    let s = qget<Stop>();
    print("parked: ", s.n);
}

export fn bump() -> i64 {
    calls += 1;
    seen.push(calls);
    qput(Note { n: calls });    // left in the queue, for goose_exit to free
    return calls;
}

export fn park() {
    thread_spawn(parked);
}

fn main() {
    print("main: calls ", calls, " seen ", seen.len);
}
