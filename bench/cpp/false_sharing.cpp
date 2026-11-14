// The false_sharing.goose workload in idiomatic C++: the same loop, the
// same recursion, the same sink, on std::thread. Goose's thread_fn gives
// each worker its own copy of the program's globals on the heap;
// the idiomatic C++ translation of "a worker owns its globals" is locals
// in the thread function, which live on the thread's own C stack, and
// std::vector for the growable parts -- allocated once with capacity,
// as goose reserves its spans up front.

#include <cstdint>
#include <cstdio>
#include <thread>
#include <threads.h>
#include <vector>
#include <iostream>
#include "bench.h"

static const long long N = BENCH_N;

static thread_local int64_t counter = 0;
static thread_local std::vector<uint8_t> sink;

static int64_t user(std::vector<uint8_t>& scratch, int64_t i) {
    int64_t n = 0;
    {
        scratch.push_back(static_cast<uint8_t>(i & 15));
        n = scratch.size();
    }
    if (i == 0) { return n; }
    return n + user(scratch, i - 1);
}

static int64_t worker(int64_t iters) {
    // Goose reserves this worker's whole span for sink's dedicated stack
    // up front, and growth never moves its base; reserve the same span
    // here so the vector pays no per-growth reallocation either.
    sink.reserve(4096);
    // The clears on the way out bound the deepest call chain's appends to
    // (3 & 3) + 1 = 4 bytes; sized once, so growth never moves the base.
    std::vector<uint8_t> scratch;
    scratch.reserve(4);
    for (int64_t i = 0; i < iters; i++) {
        counter += user(scratch, i & 3);
        sink.push_back(static_cast<uint8_t>(counter & 255));
        if (sink.size() == 4096) sink.clear();
    }
    return counter + static_cast<int64_t>(sink.size());
}

static int64_t round(size_t w, int64_t iters) {
    std::vector<int64_t> results(w, 0);
    std::vector<std::thread> threads;
    threads.reserve(w);
    for (int k = 0; k < w; k++)
        threads.emplace_back([k, iters, &results] { results[k] = worker(iters); });
    for (auto &t : threads) t.join();
    int64_t sum = 0;
    for (int k = 0; k < w; k++) sum += results[k];
    return sum;
}

int main() {
    const size_t iters = 50000000;
    int64_t sum = round(N, iters);
    std::cout << sum << std::endl;
}
