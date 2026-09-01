// Shared scaffolding for the Rust benchmark implementations. The PRNG is the
// same xorshift64 the Goose and C++ versions use, so all three languages
// generate byte-identical input and must print identical checksums -- the
// harness fails the run if they do not.
#![allow(dead_code)]

pub fn xs_next(x: u64) -> u64 {
    let mut v = x;
    v ^= v << 13;
    v ^= v >> 7;
    v ^= v << 17;
    v
}

// Reduction into [0, k), as C's uint64_t modulo; the result fits an i64.
pub fn xs_mod(x: u64, k: i64) -> i64 { (x % k as u64) as i64 }

// Matches goose's print(i64) and bench.h's emit(): decimal, one per line.
pub fn emit(v: i64) { println!("{}", v); }
