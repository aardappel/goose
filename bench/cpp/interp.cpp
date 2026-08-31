// Expression-tree evaluation: see bench/goose/interp.goose. The dispatch
// mechanism is the point. The pass index enters at the leaves so the eight
// evaluations differ; otherwise clang hoists the pure call out of the loop.
//   VARIANT 0: class hierarchy with virtual eval and unique_ptr children --
//              a vtable pointer per node and an allocation per node.
//   VARIANT 1: std::variant in an arena, evaluated with std::visit.
//   VARIANT 2: tagged union in an arena with a switch (the expert tier).
#include "bench.h"
#include <vector>
#include <memory>
#include <variant>

#ifndef DEPTH
#define DEPTH BENCH_N
#endif
static const long long M = 1000003;

#if VARIANT == 0
struct Expr { virtual ~Expr() = default; virtual long long eval(long long p) const = 0; };
struct Num : Expr { int32_t v; long long eval(long long p) const override { return (v + p) % M; } };
struct Add : Expr { std::unique_ptr<Expr> l, r;
    long long eval(long long p) const override { return (l->eval(p) + r->eval(p)) % M; } };
struct Mul : Expr { std::unique_ptr<Expr> l, r;
    long long eval(long long p) const override { return (l->eval(p) * r->eval(p)) % M; } };
struct Neg : Expr { std::unique_ptr<Expr> of;
    long long eval(long long p) const override { return (M - of->eval(p)) % M; } };
using Ref = std::unique_ptr<Expr>;
static long long count = 0;

static Ref build(int depth, uint64_t seed) {
    uint64_t s = xs_next(seed);
    count++;
    if (depth == 0) { auto n = std::make_unique<Num>(); n->v = (int32_t)xs_mod(s, 1000); return n; }
    uint64_t k = xs_mod(s, 8);
    if (k < 1) { auto n = std::make_unique<Neg>(); n->of = build(depth - 1, s); return n; }
    auto a = build(depth - 1, s);
    auto b = build(depth - 1, xs_next(s));
    if (k < 5) { auto n = std::make_unique<Add>(); n->l = std::move(a); n->r = std::move(b); return n; }
    auto n = std::make_unique<Mul>(); n->l = std::move(a); n->r = std::move(b); return n;
}
#else
struct ENum { int32_t v; };
struct EAdd { uint32_t l, r; };
struct EMul { uint32_t l, r; };
struct ENeg { uint32_t of; };
  #if VARIANT == 1
using Node = std::variant<ENum, EAdd, EMul, ENeg>;
  #else
struct Node { uint8_t tag; union { int32_t v; struct { uint32_t l, r; } b; uint32_t of; }; };
  #endif
static std::vector<Node> pool;

static uint32_t build(int depth, uint64_t seed) {
    uint64_t s = xs_next(seed);
    if (depth == 0) {
  #if VARIANT == 1
        pool.push_back(ENum { (int32_t)xs_mod(s, 1000) });
  #else
        Node n; n.tag = 0; n.v = (int32_t)xs_mod(s, 1000); pool.push_back(n);
  #endif
        return (uint32_t)pool.size() - 1;
    }
    uint64_t k = xs_mod(s, 8);
    if (k < 1) {
        uint32_t a = build(depth - 1, s);
  #if VARIANT == 1
        pool.push_back(ENeg { a });
  #else
        Node n; n.tag = 3; n.of = a; pool.push_back(n);
  #endif
        return (uint32_t)pool.size() - 1;
    }
    uint32_t a = build(depth - 1, s);
    uint32_t b = build(depth - 1, xs_next(s));
  #if VARIANT == 1
    if (k < 5) pool.push_back(EAdd { a, b }); else pool.push_back(EMul { a, b });
  #else
    Node n; n.tag = (uint8_t)(k < 5 ? 1 : 2); n.b.l = a; n.b.r = b; pool.push_back(n);
  #endif
    return (uint32_t)pool.size() - 1;
}

static long long eval(uint32_t i, long long p) {
  #if VARIANT == 1
    const Node &n = pool[i];
    switch (n.index()) {
        case 0: return (std::get<ENum>(n).v + p) % M;
        case 1: { auto &e = std::get<EAdd>(n); return (eval(e.l, p) + eval(e.r, p)) % M; }
        case 2: { auto &e = std::get<EMul>(n); return (eval(e.l, p) * eval(e.r, p)) % M; }
        default: { auto &e = std::get<ENeg>(n); return (M - eval(e.of, p)) % M; }
    }
  #else
    const Node &n = pool[i];
    switch (n.tag) {
        case 0: return (n.v + p) % M;
        case 1: return (eval(n.b.l, p) + eval(n.b.r, p)) % M;
        case 2: return (eval(n.b.l, p) * eval(n.b.r, p)) % M;
        default: return (M - eval(n.of, p)) % M;
    }
  #endif
}
#endif

int main() {
    long long total = 0, count_out = 0;
#if VARIANT == 0
    Ref root = build(DEPTH, 12345);
    count_out = count;
    for (long long p = 0; p < 8; p++) total = (total + root->eval(p)) % M;
#else
    uint32_t root = build(DEPTH, 12345);
    count_out = (long long)pool.size();
    for (long long p = 0; p < 8; p++) total = (total + eval(root, p)) % M;
#endif
    emit(count_out);
    emit(total);
    return 0;
}
