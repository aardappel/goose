// Control: see bench/goose/sum.goose. Collecting a mapped range is the way a
// Rust programmer writes "build a vector of N values", and because `Range` is
// `TrustedLen` the allocation is exact -- so Rust gets the C++ `reserve` row's
// behaviour from the idiomatic spelling, with nothing to remember.
mod bench;
use bench::*;

const N: i64 = 1000000;                  // BENCH_N
const PASSES: i64 = 8;

fn main() {
    let mut r: u64 = 12345;
    let data: Vec<i32> = (0..N).map(|_| { r = xs_next(r); xs_mod(r, 1000) as i32 }).collect();
    let mut total: i64 = 0;
    for p in 0..PASSES {
        // The `^ p` keeps the passes from collapsing into one multiply.
        for &x in &data { total += (x as i64) ^ p; }
    }
    emit(data.len() as i64);
    emit(total);
}
