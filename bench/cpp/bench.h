// Shared scaffolding for the C++ benchmark implementations. The PRNG is the
// same xorshift64 the Goose versions use, so both languages generate
// byte-identical input and must print identical checksums -- the harness
// fails the run if they do not.
#pragma once
#include <cstdint>
#include <cstdio>

#ifndef BENCH_N
#define BENCH_N 1000
#endif
#ifndef VARIANT
#define VARIANT 0
#endif

static inline uint64_t xs_next(uint64_t x) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}

static inline uint64_t xs_mod(uint64_t x, uint64_t k) { return x % k; }

// Matches goose's print(int): decimal, one per line.
static inline void emit(long long v) { printf("%lld\n", v); }
