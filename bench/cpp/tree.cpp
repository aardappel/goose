// A pointer-linked binary tree: see bench/goose/tree.goose.
//   VARIANT 0: unique_ptr nodes -- an allocation each, and a full recursive
//              destructor walk before the process can exit.
//   VARIANT 1: raw new plus an explicit recursive delete.
// The pass index is folded into every node's contribution so the eight passes
// are eight different computations; otherwise clang hoists the whole pure call
// out of the loop (see bench/notes.md).
//   VARIANT 2: one arena vector with uint32 child indices, exactly reserved:
//              no per-node allocation and no teardown, which is what the
//              Goose version gets for free.
#include "bench.h"
#include <vector>
#include <memory>

#ifndef DEPTH
#define DEPTH BENCH_N
#endif

#if VARIANT == 2
struct TNode { int32_t v; uint32_t l, r; };   // 0 = null: node 0 is a leaf sentinel slot
static std::vector<TNode> pool;

static uint32_t build(int depth, uint64_t seed) {
    if (depth == 0) { pool.push_back(TNode { (int32_t)xs_mod(seed, 1000), 0, 0 }); return (uint32_t)pool.size() - 1; }
    uint32_t l = build(depth - 1, xs_next(seed));
    uint32_t r = build(depth - 1, xs_next(seed + 1));
    pool.push_back(TNode { (int32_t)xs_mod(seed, 1000), l, r });
    return (uint32_t)pool.size() - 1;
}
static long long sum_tree(uint32_t n, long long p) {
    const TNode &t = pool[n];
    long long s = t.v ^ p;
    if (t.l) s += sum_tree(t.l, p);
    if (t.r) s += sum_tree(t.r, p);
    return s;
}
#else
struct TNode {
    int32_t v;
  #if VARIANT == 0
    std::unique_ptr<TNode> l, r;
  #else
    TNode *l = nullptr, *r = nullptr;
  #endif
};
  #if VARIANT == 0
using Owned = std::unique_ptr<TNode>;
static Owned build(int depth, uint64_t seed) {
    auto n = std::make_unique<TNode>();
    n->v = (int32_t)xs_mod(seed, 1000);
    if (depth) { n->l = build(depth - 1, xs_next(seed)); n->r = build(depth - 1, xs_next(seed + 1)); }
    return n;
}
  #else
using Owned = TNode *;
static Owned build(int depth, uint64_t seed) {
    TNode *n = new TNode();
    n->v = (int32_t)xs_mod(seed, 1000);
    if (depth) { n->l = build(depth - 1, xs_next(seed)); n->r = build(depth - 1, xs_next(seed + 1)); }
    return n;
}
static void destroy(TNode *n) { if (!n) return; destroy(n->l); destroy(n->r); delete n; }
  #endif
static long long sum_tree(const TNode *n, long long p) {
    long long s = n->v ^ p;
    if (n->l) s += sum_tree(&*n->l, p);
    if (n->r) s += sum_tree(&*n->r, p);
    return s;
}
static long long node_count(int depth) { return (1LL << (depth + 1)) - 1; }
#endif

int main() {
    long long total = 0, count = 0;
#if VARIANT == 2
    pool.reserve((size_t)((1LL << (DEPTH + 1))));   // The node count is known here.
    pool.push_back(TNode { 0, 0, 0 });        // Index 0 is the null sentinel.
    uint32_t root = build(DEPTH, 12345);
    count = (long long)pool.size() - 1;
    for (long long p = 0; p < 8; p++) total += sum_tree(root, p);
#else
    Owned root = build(DEPTH, 12345);
    count = node_count(DEPTH);
    for (long long p = 0; p < 8; p++) total += sum_tree(&*root, p);
  #if VARIANT == 1
    destroy(root);
  #endif
#endif
    emit(count);
    emit(total);
    return 0;
}
