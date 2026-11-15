// The thread_local.goose workload in idiomatic Rust: the same loop, the
// same recursion, the same sink, on std::thread. Goose's thread_fn gives
// each worker its own copy of the program's globals on the heap;
// the idiomatic Rust translation of "a worker owns its globals" is locals
// moved into the thread closure, and Vec for the growable parts -- allocated
// once with capacity, as goose reserves its spans up front.

use std::cell::Cell;
use std::cell::RefCell;
use std::thread;

const N: usize = 8; // BENCH_N

thread_local! {
    static counter: Cell<i64> = const { Cell::new(0) };
    // Goose reserves this worker's whole span for sink's dedicated stack
    // up front, and growth never moves its base; reserve the same span
    // here so the vector pays no per-growth reallocation either.
    static sink: RefCell<Vec<u8>> = RefCell::new(Vec::with_capacity(4096));
}

fn user(scratch: &mut Vec<u8>, i: i64) -> i64 {
    let n: i64 = {
        scratch.push((i & 15) as u8);
        scratch.len() as i64
    };
    if i == 0 {
        return n;
    }
    n + user(scratch, i - 1)
}

fn worker(iters: i64) -> i64 {
    // The clears on the way out bound the deepest call chain's appends to
    // (3 & 3) + 1 = 4 bytes; sized once, so growth never moves the base.
    let mut scratch: Vec<u8> = Vec::with_capacity(4);
    for i in 0..iters {
        let res = user(&mut scratch, i & 3);
        counter.set(counter.get() + res);
        sink.with_borrow_mut(|v| {
            v.push((counter.get() & 255) as u8);
            scratch.clear();
            if v.len() == 4096 {
                v.clear();
            }
        });
    }
    sink.with_borrow(|v| counter.get() + v.len() as i64)
}

fn round(w: usize, iters: i64) -> i64 {
    let mut results: Vec<i64> = vec![0; w];
    thread::scope(|s| {
        let handles: Vec<_> = results
            .iter_mut()
            .map(|slot| s.spawn(move || *slot = worker(iters)))
            .collect();
        for h in handles {
            h.join().unwrap();
        }
    });
    results.iter().sum()
}

fn main() {
    let iters: i64 = 50_000_000;
    let sum = round(N, iters);
    println!("{}", sum);
}
