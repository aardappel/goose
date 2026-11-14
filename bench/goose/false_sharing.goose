// False sharing between the private chunks the runtime gives every
// worker: its globals instance (what the thread-local gs_gl points at)
// and its stack block (gs_stks). Threads share no data (goose_spec.md
// 11.2), but the allocator can place two chunks in one cache line, and
// each thread's writes invalidate the other's line anyway.
//
// Each iteration of the loop below touches both structures the way real
// code does. `counter += i` stores through GS_GL -- the worker's globals
// instance -- every iteration. `user()` is recursive, so the compiler
// cannot inline it away, and its resizable local makes every call load
// and store this worker's stack descriptor in gs_stks; two workers whose
// 16-byte stack blocks share a line pay for each other's calls. `sink`
// keeps a byte flowing per iteration.

import std;
import os;

let N = 8; // BENCH_N

var counter: i64 = 0;
var sink: u8[>..] = [];

recursive fn user(scratch: u8[>..]&, i: i64) -> i64 {
    var n = 0;
    {
        scratch.push((i & 15) as u8);
        n = scratch.len;
    }
    if i == 0 { return n; }
    return n + user(scratch, i - 1);
}

thread_fn worker(iters: i64) {
    var scratch: u8[>..] = [];
    for i in iters {
        counter += user(scratch, i & 3);
        sink.push((counter & 255) as u8);
        if sink.len == 4096 { sink.resize(0); }
    }
    qput(counter + sink.len);
}

fn round(w: i64, iters: i64) -> i64 {
    var ids: i64[>..] = [];
    for k in w { ids.push(thread_spawn(worker, iters)); }
    var sum = 0;
    for k in w { sum += qget<i64>(); }
    for id in ids { thread_wait(id); }
    return sum;
}

fn main() {
    let iters = 50000000;
    let sum = round(N, iters);
    print(sum);
}
