// Control: see bench/goose/sum.goose.
//   VARIANT 0: vector, no reserve.
//   VARIANT 1: vector with an exact reserve.
#include "bench.h"
#include <vector>

int main() {
    std::vector<int32_t> data;
#if VARIANT == 1
    data.reserve(BENCH_N);
#endif
    uint64_t r = 12345;
    for (long long i = 0; i < BENCH_N; i++) {
        r = xs_next(r);
        data.push_back((int32_t)xs_mod(r, 1000));
    }
    long long total = 0;
    for (long long p = 0; p < 8; p++)
        for (int32_t x : data) total += x ^ p;
    emit((long long)data.size());
    emit(total);
    return 0;
}
