// Generate an s-expression text, parse it into a tree, walk it: see
// bench/goose/sexp.goose. Children are linked first-child / previous-sibling
// exactly as the Goose version does, so the walk order (and the checksum)
// agree.
//   VARIANT 0: unique_ptr nodes with std::string symbols -- the idiomatic
//              shape, an allocation per node plus one per long symbol, and a
//              full recursive teardown at exit.
//   VARIANT 1: one arena with uint32 links and string_view symbols into the
//              source text: the expert tier, and the closest analogue of what
//              Goose does by default.
#include "bench.h"
#include <vector>
#include <string>
#include <string_view>
#include <memory>

#ifndef DEPTH
#define DEPTH 4
#endif
static const int PASSES = 4;

static std::string text;
static size_t pos = 0;

// --- generation --------------------------------------------------------------

static void emit_sym(uint64_t seed) {
    uint64_t r = seed;
    long long n = 3 + (long long)xs_mod(r, 12);
    for (long long i = 0; i < n; i++) { r = xs_next(r); text.push_back((char)('a' + xs_mod(r, 26))); }
}

static void emit_num(long long v) {
    if (v == 0) { text.push_back('0'); return; }
    char tmp[24];
    int k = 0;
    while (v > 0) { tmp[k++] = (char)('0' + v % 10); v /= 10; }
    while (k > 0) text.push_back(tmp[--k]);
}

static uint64_t gen(int depth, uint64_t seed) {
    uint64_t r = xs_next(seed);
    if (depth == 0 || xs_mod(r, 4) == 0) {
        if (xs_mod(r >> 8, 2) == 0) emit_sym(r);
        else emit_num((long long)xs_mod(r >> 16, 100000));
        text.push_back(' ');
        return r;
    }
    text.push_back('(');
    long long kids = 2 + (long long)xs_mod(r >> 24, 4);
    for (long long k = 0; k < kids; k++) r = gen(depth - 1, r);
    text.push_back(')');
    text.push_back(' ');
    return r;
}

static void skip_space() { while (pos < text.size() && text[pos] == ' ') pos++; }

// --- parse and walk ----------------------------------------------------------

#if VARIANT == 0
struct Node {
    int kind;                                  // 0 sym, 1 num, 2 list
    std::string name;
    int32_t v = 0;
    std::unique_ptr<Node> first, next;
};
static long long node_count = 0;
static std::vector<std::unique_ptr<Node>> roots;

static std::unique_ptr<Node> parse_node(std::unique_ptr<Node> prev) {
    skip_space();
    auto n = std::make_unique<Node>();
    node_count++;
    char c = text[pos];
    if (c == '(') {
        pos++;
        std::unique_ptr<Node> chain;
        for (;;) {
            skip_space();
            if (text[pos] == ')') { pos++; break; }
            chain = parse_node(std::move(chain));
        }
        n->kind = 2;
        n->first = std::move(chain);
        n->next = std::move(prev);
        return n;
    }
    size_t start = pos;
    if (c >= '0' && c <= '9') {
        long long v = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') { v = v * 10 + (text[pos] - '0'); pos++; }
        n->kind = 1;
        n->v = (int32_t)v;
        n->next = std::move(prev);
        return n;
    }
    while (pos < text.size() && text[pos] != ' ' && text[pos] != '(' && text[pos] != ')') pos++;
    n->kind = 0;
    n->name.assign(text, start, pos - start);
    n->next = std::move(prev);
    return n;
}

static long long walk(const Node *n);
static long long walk_chain(const Node *c) { return c ? walk(c) + walk_chain(c->next.get()) : 0; }
static long long walk(const Node *n) {
    switch (n->kind) {
        case 0: return (long long)n->name.size() + (unsigned char)n->name[0];
        case 1: return n->v;
        default: return 1 + walk_chain(n->first.get());
    }
}
#else
struct Node {
    uint8_t kind;
    int32_t v;
    uint32_t off, len;                         // symbol text, into `text`
    uint32_t first, next;
};
static std::vector<Node> pool;
static std::vector<uint32_t> roots;

static uint32_t parse_node(uint32_t prev) {
    skip_space();
    char c = text[pos];
    if (c == '(') {
        pos++;
        uint32_t chain = 0;
        for (;;) {
            skip_space();
            if (text[pos] == ')') { pos++; break; }
            chain = parse_node(chain);
        }
        pool.push_back(Node { 2, 0, 0, 0, chain, prev });
        return (uint32_t)pool.size() - 1;
    }
    size_t start = pos;
    if (c >= '0' && c <= '9') {
        long long v = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') { v = v * 10 + (text[pos] - '0'); pos++; }
        pool.push_back(Node { 1, (int32_t)v, 0, 0, 0, prev });
        return (uint32_t)pool.size() - 1;
    }
    while (pos < text.size() && text[pos] != ' ' && text[pos] != '(' && text[pos] != ')') pos++;
    pool.push_back(Node { 0, 0, (uint32_t)start, (uint32_t)(pos - start), 0, prev });
    return (uint32_t)pool.size() - 1;
}

static long long walk(uint32_t n);
static long long walk_chain(uint32_t c) { return c ? walk(c) + walk_chain(pool[c].next) : 0; }
static long long walk(uint32_t i) {
    const Node &n = pool[i];
    switch (n.kind) {
        case 0: return n.len + (unsigned char)text[n.off];
        case 1: return n.v;
        default: return 1 + walk_chain(n.first);
    }
}
#endif

int main() {
    uint64_t seed = 12345;
    for (long long i = 0; i < BENCH_N; i++) seed = gen(DEPTH, seed);
#if VARIANT != 0
    pool.push_back(Node { 1, 0, 0, 0, 0, 0 });    // index 0 is the null sentinel
#endif
    for (;;) {
        skip_space();
        if (pos >= text.size()) break;
#if VARIANT == 0
        roots.push_back(parse_node(nullptr));
#else
        roots.push_back(parse_node(0));
#endif
    }
    long long total = 0;
    for (int p = 0; p < PASSES; p++)
        for (const auto &r : roots)
#if VARIANT == 0
            total += walk(r.get());
#else
            total += walk(r);
#endif
    emit((long long)text.size());
#if VARIANT == 0
    emit(node_count);
#else
    emit((long long)pool.size() - 1);
#endif
    emit((long long)roots.size());
    emit(0);
    emit(total);
    return 0;
}
