// The cost of today's share-nothing fork-join: spawn n empty workers and
// wait for all of them, repeated, which is what a design that spawns its
// workers per pass pays per pass (docs/design/child_threads.md §1.1). The
// worker owns no data stacks; give it a `var scratch: u8[>..] = [];` to add
// a region reservation per worker to the measurement.
//
// Build and run (from this directory):
//   goose build spawn_cost.goose && ./spawn_cost
import std;
import os;

thread_fn worker(k: i64) { }

fn round(n: i64) {
    var ids: i64[>..] = [];
    for k in n { ids.push(thread_spawn(worker, k)); }
    for id in ids { thread_wait(id); }
}

fn main() {
    let reps = 200;
    for n in [1, 2, 4, 8, 16, 31] {
        round(n);                                    // warm up
        let t0 = clock();
        for r in reps { round(n); }
        let t1 = clock();
        var note: u8[>..] = [];
        format(note, n, " workers: ");
        format_flt(note, (t1 - t0) * 1.0e6 / reps, 1);
        format(note, " us per spawn+wait round, ");
        format_flt(note, (t1 - t0) * 1.0e6 / (reps * n), 1);
        format(note, " us per worker");
        print(note);
    }
    print("hardware_threads: ", hardware_threads());
}
