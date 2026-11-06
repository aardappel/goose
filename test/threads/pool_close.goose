// A thread_pool lives until the program closes it. No code runs at a scope
// exit that the program did not write (docs/design/ownership.md), so
// pool.close() -- usually under defer -- is where a pool's workers are joined
// and their data stacks released, and a pool left open goes with the
// process, whose exit takes its threads and their mappings at once.
// run_tests.py reads this program's C for that: only what writes a close
// destroys a pool (docs/design/parallel_blocks.md §3.1).

fn fill(chunk: i64[:], idx: i64) {
    for &x in chunk { x = idx + 1; }
}

// Leaves its pool open: nothing at its exit ends it.
fn kept(n: i64) -> i64 {
    var pool = thread_pool(2);
    var data: i64[>..] = [];
    data.resize(n, 0);
    pool.apply(chunks(data, 1), fill);
    return data[n - 1];
}

// Ends its pool on each way out, the early return among them.
fn deferred(n: i64, early: bool) -> i64 {
    var pool = thread_pool(2);
    defer pool.close();
    var data: i64[>..] = [];
    data.resize(n, 0);
    pool.apply(chunks(data, 2), fill);
    if early { return data[0]; }
    return data[n - 1];
}

// Ends the pool it is given, which its caller sees closed.
fn end_pool(p: thread_pool&) {
    p.close();
}

// No scope's exit could end a global's pool either.
var shared = thread_pool(2);

fn main() {
    print("kept: ", kept(3));
    print("deferred: ", deferred(4, false), " ", deferred(4, true));
    var data: i64[>..] = [0, 0, 0, 0, 0, 0];
    var rounds = 0;
    while rounds < 3 {
        let pool = thread_pool(2);
        pool.apply(chunks(data, 3), fill);
        end_pool(pool);
        end_pool(pool);   // closed already: nothing to do
        rounds++;
    }
    print("rounds: ", rounds, " ", data);
    shared.apply(chunks(data, 1), fill);
    shared.close();
    print("shared: ", data);
    var left = thread_pool(4);
    left.apply(chunks(data, 2), fill);
    print("left open: ", data);
}
