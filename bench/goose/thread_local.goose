// From goose_spec.md 11.2
//
// A thread program is an instance of the program with globals of its own.
// At thread_spawn, every global the worker’s program uses is copied from
// the spawning instance, in the same way as the arguments, and the worker
// reads and writes its copies from then on. No global is shared: a worker
// that increments a global increments its own copy.
//
// Because each instance owns its globals as plain data, reading or writing
// one is a direct load or store — no TLS lookup, no lazy-init check — which
// is why they beat thread locals in languages such as C++ or Rust, where
// an access may pay for TLS resolution or initialization state.

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
        scratch.resize(0);
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
