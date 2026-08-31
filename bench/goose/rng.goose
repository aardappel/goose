// Shared by every Goose benchmark. xorshift64 over u64, matching bench.h on
// the C++ side exactly, so both languages generate byte-identical input and
// must print identical checksums.

fn xs_next(x: u64) -> u64 {
    var v = x;
    v ^= v << 13;
    v ^= v >> 7;
    v ^= v << 17;
    v
}

// Reduction into [0, k), as C's uint64_t modulo; the result fits an i64.
fn xs_mod(x: u64, k: i64) -> i64 { (x % k as u64) as! i64 }
