// Growth while holding element pointers: see bench/goose/push.goose. A
// vector's elements move on every reallocation, so the marks have to be
// indices unless the container is chosen for pointer stability.
//   VARIANT 0: vector, no reserve, marks as indices.
//   VARIANT 1: vector with an exact reserve, marks as indices.
//   VARIANT 2: deque (pointer-stable), marks as real pointers.
#include "bench.h"
#include <vector>
#include <deque>

struct Item { int32_t key, val; };

int main() {
    uint64_t r = 12345;
    long long total = 0, mt = 0, nitems = 0, nmarks = 0;
#if VARIANT == 2
    std::deque<Item> items;
    std::vector<Item *> marks;
    for (long long i = 0; i < BENCH_N; i++) {
        r = xs_next(r);
        items.push_back(Item { (int32_t)xs_mod(r, 1000000), (int32_t)(i % 97) });
        if (i % 64 == 0) marks.push_back(&items.back());
    }
    for (const auto &it : items) total += it.key;
    for (auto *m : marks) mt += m->val;
#else
    std::vector<Item> items;
    std::vector<size_t> marks;
  #if VARIANT == 1
    items.reserve(BENCH_N);
  #endif
    for (long long i = 0; i < BENCH_N; i++) {
        r = xs_next(r);
        items.push_back(Item { (int32_t)xs_mod(r, 1000000), (int32_t)(i % 97) });
        if (i % 64 == 0) marks.push_back(items.size() - 1);
    }
    for (const auto &it : items) total += it.key;
    for (size_t m : marks) mt += items[m].val;
#endif
    nitems = (long long)items.size();
    nmarks = (long long)marks.size();
    emit(nitems);
    emit(nmarks);
    emit(total);
    emit(mt);
    return 0;
}
