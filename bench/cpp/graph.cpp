// Incremental adjacency build plus BFS: see bench/goose/graph.goose.
//   VARIANT 0: vector<vector<int32_t>> -- one container, one allocation and a
//              24-byte header per vertex.
//   VARIANT 1: CSR, built with the two-pass count-then-fill: the fastest and
//              most compact thing a C++ programmer writes, and the most code.
//   VARIANT 2: one arena of edges with uint32 next-links: the direct analogue
//              of the Goose version, but linked by index rather than offset.
//
// BFS distances are shortest paths, so the checksum does not depend on the
// order edges come out of the adjacency structure.
#include "bench.h"
#include <vector>

static const long long DEG = 8;
static const int SOURCES = 4;

int main() {
    const long long nv = BENCH_N, ne = nv * DEG;
    uint64_t r = 12345;

#if VARIANT == 0
    std::vector<std::vector<int32_t>> adj((size_t)nv);
    for (long long e = 0; e < ne; e++) {
        r = xs_next(r);
        adj[(size_t)xs_mod(r, (uint64_t)nv)].push_back((int32_t)xs_mod(r >> 24, (uint64_t)nv));
    }
    auto for_each_edge = [&](long long u, auto &&f) {
        for (int32_t w : adj[(size_t)u]) f(w);
    };
#elif VARIANT == 1
    std::vector<int32_t> esrc((size_t)ne), edst((size_t)ne);
    for (long long e = 0; e < ne; e++) {
        r = xs_next(r);
        esrc[(size_t)e] = (int32_t)xs_mod(r, (uint64_t)nv);
        edst[(size_t)e] = (int32_t)xs_mod(r >> 24, (uint64_t)nv);
    }
    std::vector<int32_t> start((size_t)nv + 1, 0);
    for (long long e = 0; e < ne; e++) start[(size_t)esrc[(size_t)e] + 1]++;
    for (long long v = 0; v < nv; v++) start[(size_t)v + 1] += start[(size_t)v];
    std::vector<int32_t> fill = start, out((size_t)ne);
    for (long long e = 0; e < ne; e++) out[(size_t)fill[(size_t)esrc[(size_t)e]]++] = edst[(size_t)e];
    auto for_each_edge = [&](long long u, auto &&f) {
        for (int32_t k = start[(size_t)u]; k < start[(size_t)u + 1]; k++) f(out[(size_t)k]);
    };
#else
    struct Edge { int32_t to; uint32_t next; };
    std::vector<Edge> pool;
    pool.reserve((size_t)(nv + ne));
    for (long long v = 0; v < nv; v++) pool.push_back(Edge { -1, 0 });      // list heads
    for (long long e = 0; e < ne; e++) {
        r = xs_next(r);
        size_t u = (size_t)xs_mod(r, (uint64_t)nv);
        pool.push_back(Edge { (int32_t)xs_mod(r >> 24, (uint64_t)nv), pool[u].next });
        pool[u].next = (uint32_t)pool.size() - 1;
    }
    auto for_each_edge = [&](long long u, auto &&f) {
        for (uint32_t c = pool[(size_t)u].next; c; c = pool[(size_t)c].next) f(pool[(size_t)c].to);
    };
#endif

    std::vector<int32_t> dist((size_t)nv);
    std::vector<int32_t> q;
    q.reserve((size_t)nv);
    long long total = 0;
    uint64_t seed = 999;
    for (int s = 0; s < SOURCES; s++) {
        seed = xs_next(seed);
        long long srcv = (long long)xs_mod(seed, (uint64_t)nv);
        for (long long v = 0; v < nv; v++) dist[(size_t)v] = -1;
        q.clear();
        dist[(size_t)srcv] = 0;
        q.push_back((int32_t)srcv);
        for (size_t qh = 0; qh < q.size(); qh++) {
            long long u = q[qh];
            long long d = dist[(size_t)u];
            total += d;
            for_each_edge(u, [&](int32_t w) {
                if (dist[(size_t)w] < 0) { dist[(size_t)w] = (int32_t)(d + 1); q.push_back(w); }
            });
        }
    }
    emit(nv + ne);
    emit(total);
    return 0;
}
