// Shared by every Goose benchmark. xorshift64 over the bit pattern in `int`,
// matching bench.h on the C++ side exactly, so both languages generate
// byte-identical input and must print identical checksums.

fn xs_next(x: int) -> int {
    var v = x;
    v ^= v << 13;
    v ^= unsigned_shr(v, 7);
    v ^= v << 17;
    v
}

// Unsigned reduction into [0, k), so negative bit patterns behave like C's
// uint64_t modulo rather than signed remainder.
fn xs_mod(x: int, k: int) -> int { unsigned_mod(x, k) }
