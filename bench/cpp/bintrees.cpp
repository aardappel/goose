// The Benchmarks Game's binary-trees: see bench/goose/bintrees.goose. A great
// many short-lived complete binary trees are built, checked and thrown away,
// with one long-lived tree kept across the run. It is famously an allocator
// benchmark, which is exactly why it is here: the whole cost is allocation and
// teardown of 8-byte nodes.
//   VARIANT 0: raw new per node and a recursive delete per tree -- the
//              std-only idiomatic shape (unique_ptr would be the same
//              allocations plus the same recursive teardown).
//   VARIANT 1: one vector arena per tree-building scope, cleared and reused
//              between trees so its capacity is paid for once, with uint32
//              child indices and index 0 as the null sentinel. Expert.
#include "bench.h"
#include <vector>

#ifndef MAXDEPTH
#define MAXDEPTH BENCH_N
#endif
static const int MINDEPTH = 4;

#if VARIANT == 0

struct Node { Node *l = nullptr, *r = nullptr; };

static Node *make(int depth) {
    if (depth == 0) return new Node();
    Node *l = make(depth - 1);
    Node *r = make(depth - 1);
    Node *n = new Node();
    n->l = l;
    n->r = r;
    return n;
}

static long long check(const Node *n) {
    if (!n->l) return 1;
    if (!n->r) return 1;
    return 1 + check(n->l) + check(n->r);
}

static void destroy(Node *n) {
    if (!n) return;
    destroy(n->l);
    destroy(n->r);
    delete n;
}

// Build, check, discard: the discard is a free per node.
static long long tree_check(int depth) {
    Node *root = make(depth);
    long long c = check(root);
    destroy(root);
    return c;
}

int main() {
    emit(tree_check(MAXDEPTH + 1));        // The stretch tree.
    Node *lroot = make(MAXDEPTH);
    for (int d = MINDEPTH; d <= MAXDEPTH; d += 2) {
        long long iters = 1LL << (MAXDEPTH - d + MINDEPTH);
        long long sum = 0;
        for (long long i = 0; i < iters; i++) sum += tree_check(d);
        emit(iters);
        emit(d);
        emit(sum);
    }
    emit(check(lroot));
    destroy(lroot);
    return 0;
}

#else

struct Node { uint32_t l, r; };             // 0 = null: index 0 is the sentinel slot.

static std::vector<Node> scratch;           // Reused by every short-lived tree.

static uint32_t make(std::vector<Node> &p, int depth) {
    if (depth == 0) { p.push_back(Node { 0, 0 }); return (uint32_t)p.size() - 1; }
    uint32_t l = make(p, depth - 1);
    uint32_t r = make(p, depth - 1);
    p.push_back(Node { l, r });
    return (uint32_t)p.size() - 1;
}

static long long check(const std::vector<Node> &p, uint32_t i) {
    uint32_t l = p[i].l;
    if (!l) return 1;
    uint32_t r = p[i].r;
    if (!r) return 1;
    return 1 + check(p, l) + check(p, r);
}

// Build, check, discard: the discard is resetting one length.
static long long tree_check(int depth) {
    scratch.clear();
    scratch.push_back(Node { 0, 0 });
    uint32_t root = make(scratch, depth);
    return check(scratch, root);
}

int main() {
    emit(tree_check(MAXDEPTH + 1));        // The stretch tree.
    std::vector<Node> lived;
    lived.push_back(Node { 0, 0 });
    uint32_t lroot = make(lived, MAXDEPTH);
    for (int d = MINDEPTH; d <= MAXDEPTH; d += 2) {
        long long iters = 1LL << (MAXDEPTH - d + MINDEPTH);
        long long sum = 0;
        for (long long i = 0; i < iters; i++) sum += tree_check(d);
        emit(iters);
        emit(d);
        emit(sum);
    }
    emit(check(lived, lroot));
    return 0;
}

#endif
