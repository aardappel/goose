// Many small expression parses with real error paths: see
// bench/goose/calc.goose. N arithmetic expressions are generated, one in four
// is damaged, and each is parsed by recursive descent into its own short-lived
// AST and evaluated in wrapping u64. What is on trial is the cost of the tree
// per input -- built and torn down N times -- and the cost of carrying an
// error out of the middle of the recursion.
//   VARIANT 0: unique_ptr nodes; parse and eval errors are thrown and caught
//              once per input, so the failing quarter unwinds through the
//              destructors of the half-built tree. Idiomatic.
//   VARIANT 1: one arena vector cleared per input (its capacity retained),
//              nodes linked by uint32 index, and an error code returned
//              through every level with no exceptions anywhere. Expert.
#include "bench.h"
#include <string>
#include <vector>
#include <memory>

static const int E_EOF = 1;
static const int E_PAREN = 2;
static const int E_TOKEN = 3;
static const int E_DIV0 = 4;

static std::string text;                   // Reused across inputs: it is the input, not the subject.
static size_t pos = 0;
static long long nodes = 0;                // Every node ever built, bad inputs included.


// --- generation --------------------------------------------------------------

static void emit_num(long long v) {
    if (v >= 10) text.push_back((char)('0' + v / 10));
    text.push_back((char)('0' + v % 10));
}

static uint64_t gen(long long depth, uint64_t seed) {
    uint64_t r = xs_next(seed);
    long long k = (long long)xs_mod(r, 8);
    if (depth == 0 || k == 0) { emit_num(1 + (long long)xs_mod(r >> 8, 99)); return r; }
    if (k == 1) { text.push_back('-'); return gen(depth - 1, r); }
    if (k == 2) {
        text.push_back('(');
        r = gen(depth - 1, r);
        text.push_back(')');
        return r;
    }
    r = gen(depth - 1, r);
    long long op = (long long)xs_mod(r >> 16, 4);
    text.push_back(' ');
    text.push_back(op == 0 ? '+' : op == 1 ? '-' : op == 2 ? '*' : '/');
    text.push_back(' ');
    return gen(depth - 1, xs_next(r));
}

// One in four inputs is damaged after generation.
static void corrupt(uint64_t seed) {
    long long kind = (long long)xs_mod(seed, 16);
    if (kind == 0) {
        size_t i = text.size();
        while (i > 0) {
            i--;
            if (text[i] == ')') { text[i] = ' '; return; }
        }
        text[0] = '#';
        return;
    }
    if (kind == 1) { text[(size_t)xs_mod(seed >> 8, (uint64_t)text.size())] = '#'; return; }
    if (kind == 2) { text.resize(text.size() / 2); return; }
    if (kind == 3) { text += " / 0"; }
}

static void skip() { while (pos < text.size() && text[pos] == ' ') pos++; }

// --- parse and evaluate ------------------------------------------------------

#if VARIANT == 0

struct Err { int code; };

struct Expr {
    uint8_t kind;                          // 0 Num, 1 Neg, 2 Paren, 3 Bin
    char op = 0;
    int64_t v = 0;
    std::unique_ptr<Expr> l, r;
};

static std::unique_ptr<Expr> parse_expr();
static std::unique_ptr<Expr> parse_term();

static std::unique_ptr<Expr> parse_factor() {
    skip();
    if (pos >= text.size()) throw Err { E_EOF };
    char c = text[pos];
    if (c >= '0' && c <= '9') {
        int64_t v = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') { v = v * 10 + (text[pos] - '0'); pos++; }
        nodes++;
        auto n = std::make_unique<Expr>();
        n->kind = 0;
        n->v = v;
        return n;
    }
    if (c == '-') {
        pos++;
        auto f = parse_factor();
        nodes++;
        auto n = std::make_unique<Expr>();
        n->kind = 1;
        n->l = std::move(f);
        return n;
    }
    if (c != '(') throw Err { E_TOKEN };
    pos++;
    auto e = parse_expr();
    skip();
    if (pos >= text.size() || text[pos] != ')') throw Err { E_PAREN };
    pos++;
    nodes++;
    auto n = std::make_unique<Expr>();
    n->kind = 2;
    n->l = std::move(e);
    return n;
}

static std::unique_ptr<Expr> parse_term() {
    auto lhs = parse_factor();
    for (;;) {
        skip();
        if (pos >= text.size() || (text[pos] != '*' && text[pos] != '/')) break;
        char op = text[pos];
        pos++;
        auto rhs = parse_factor();
        nodes++;
        auto n = std::make_unique<Expr>();
        n->kind = 3;
        n->op = op;
        n->l = std::move(lhs);
        n->r = std::move(rhs);
        lhs = std::move(n);
    }
    return lhs;
}

static std::unique_ptr<Expr> parse_expr() {
    auto lhs = parse_term();
    for (;;) {
        skip();
        if (pos >= text.size() || (text[pos] != '+' && text[pos] != '-')) break;
        char op = text[pos];
        pos++;
        auto rhs = parse_term();
        nodes++;
        auto n = std::make_unique<Expr>();
        n->kind = 3;
        n->op = op;
        n->l = std::move(lhs);
        n->r = std::move(rhs);
        lhs = std::move(n);
    }
    return lhs;
}

// Unsigned, so overflow wraps identically in every language.
static uint64_t eval(const Expr *e) {
    switch (e->kind) {
        case 0: return (uint64_t)e->v;
        case 1: return 0ull - eval(e->l.get());
        case 2: return eval(e->l.get());
        default: {
            uint64_t a = eval(e->l.get());
            uint64_t b = eval(e->r.get());
            if (e->op == '+') return a + b;
            if (e->op == '-') return a - b;
            if (e->op == '*') return a * b;
            if (b == 0) throw Err { E_DIV0 };
            return a / b;
        }
    }
}

// One input: its tree is destroyed on the way out however the input ends.
static int run_one(uint64_t &out) {
    try {
        pos = 0;
        std::unique_ptr<Expr> root = parse_expr();
        skip();
        if (pos != text.size()) return E_TOKEN;
        out = eval(root.get());
        return 0;
    } catch (const Err &e) {
        return e.code;
    }
}

#else

struct Expr {
    uint8_t kind;                          // 0 Num, 1 Neg, 2 Paren, 3 Bin
    char op;
    int32_t v;
    uint32_t l, r;
};

static std::vector<Expr> pool;             // Cleared per input, capacity retained.

static uint32_t push(Expr e) { pool.push_back(e); return (uint32_t)pool.size() - 1; }

static int parse_expr(uint32_t &out);
static int parse_term(uint32_t &out);

static int parse_factor(uint32_t &out) {
    skip();
    if (pos >= text.size()) return E_EOF;
    char c = text[pos];
    if (c >= '0' && c <= '9') {
        int64_t v = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') { v = v * 10 + (text[pos] - '0'); pos++; }
        nodes++;
        out = push(Expr { 0, 0, (int32_t)v, 0, 0 });
        return 0;
    }
    if (c == '-') {
        pos++;
        uint32_t f;
        int e = parse_factor(f);
        if (e) return e;
        nodes++;
        out = push(Expr { 1, 0, 0, f, 0 });
        return 0;
    }
    if (c != '(') return E_TOKEN;
    pos++;
    uint32_t inner;
    int e = parse_expr(inner);
    if (e) return e;
    skip();
    if (pos >= text.size() || text[pos] != ')') return E_PAREN;
    pos++;
    nodes++;
    out = push(Expr { 2, 0, 0, inner, 0 });
    return 0;
}

static int parse_term(uint32_t &out) {
    uint32_t lhs;
    int e = parse_factor(lhs);
    if (e) return e;
    for (;;) {
        skip();
        if (pos >= text.size() || (text[pos] != '*' && text[pos] != '/')) break;
        char op = text[pos];
        pos++;
        uint32_t rhs;
        e = parse_factor(rhs);
        if (e) return e;
        nodes++;
        lhs = push(Expr { 3, op, 0, lhs, rhs });
    }
    out = lhs;
    return 0;
}

static int parse_expr(uint32_t &out) {
    uint32_t lhs;
    int e = parse_term(lhs);
    if (e) return e;
    for (;;) {
        skip();
        if (pos >= text.size() || (text[pos] != '+' && text[pos] != '-')) break;
        char op = text[pos];
        pos++;
        uint32_t rhs;
        e = parse_term(rhs);
        if (e) return e;
        nodes++;
        lhs = push(Expr { 3, op, 0, lhs, rhs });
    }
    out = lhs;
    return 0;
}

// Unsigned, so overflow wraps identically in every language.
static int eval(uint32_t i, uint64_t &out) {
    const Expr &e = pool[i];
    if (e.kind == 0) { out = (uint64_t)(int64_t)e.v; return 0; }
    if (e.kind == 1) {
        uint64_t a;
        int r = eval(e.l, a);
        if (r) return r;
        out = 0ull - a;
        return 0;
    }
    if (e.kind == 2) return eval(e.l, out);
    uint64_t a, b;
    int r = eval(e.l, a);
    if (r) return r;
    r = eval(e.r, b);
    if (r) return r;
    if (e.op == '+') { out = a + b; return 0; }
    if (e.op == '-') { out = a - b; return 0; }
    if (e.op == '*') { out = a * b; return 0; }
    if (b == 0) return E_DIV0;
    out = a / b;
    return 0;
}

// One input: the whole tree is released by resetting the arena's length.
static int run_one(uint64_t &out) {
    pool.clear();
    pos = 0;
    uint32_t root;
    int e = parse_expr(root);
    if (e) return e;
    skip();
    if (pos != text.size()) return E_TOKEN;
    return eval(root, out);
}

#endif

int main() {
    uint64_t seed = 12345;
    long long ok = 0, chars = 0;
    long long errs[5] = { 0, 0, 0, 0, 0 };
    uint64_t sum = 0;
    for (long long i = 0; i < BENCH_N; i++) {
        seed = xs_next(seed);
        text.clear();
        long long depth = 3 + (long long)xs_mod(seed, 4);
        uint64_t s = gen(depth, seed >> 8);
        corrupt(s);
        chars += (long long)text.size();
        uint64_t v = 0;
        int err = run_one(v);
        if (err == 0) { ok++; sum = sum * 31 + v; } else { errs[err] += 1; }
    }
    emit(chars);
    emit(nodes);
    emit(ok);
    emit(errs[E_EOF]);
    emit(errs[E_PAREN]);
    emit(errs[E_TOKEN]);
    emit(errs[E_DIV0]);
    emit_u(sum);
    return 0;
}
