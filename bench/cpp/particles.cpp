// A float kernel over particles: see bench/goose/particles.goose. All the
// arithmetic is exact in binary32, so every row must produce a bit-identical
// checksum however the backend reassociates.
//   VARIANT 0: array of structs with the update written out per component,
//              which is what a C++ programmer writes.
//   VARIANT 1: struct of arrays, the shape reached for when vectorization
//              matters.
//   VARIANT 2: array of structs with a whole-vector add, mirroring the
//              elementwise style of particles.goose so the two languages can
//              be compared at the same expression shape.
#include "bench.h"
#include <vector>

static const int STEPS = 200;

int main() {
    const long long n = BENCH_N;
    uint64_t r = 12345;
#if VARIANT == 1
    std::vector<float> px((size_t)n), py((size_t)n), pz((size_t)n),
                       vx((size_t)n), vy((size_t)n), vz((size_t)n);
    for (long long i = 0; i < n; i++) {
        r = xs_next(r);
        px[(size_t)i] = (float)xs_mod(r, 1024);
        py[(size_t)i] = (float)xs_mod(r >> 12, 1024);
        pz[(size_t)i] = (float)xs_mod(r >> 24, 1024);
        vx[(size_t)i] = (float)((double)((long long)xs_mod(r >> 36, 256) - 128) / 16.0);
        vy[(size_t)i] = (float)((double)((long long)xs_mod(r >> 44, 256) - 128) / 16.0);
        vz[(size_t)i] = (float)((double)((long long)xs_mod(r >> 52, 256) - 128) / 16.0);
    }
    for (int s = 0; s < STEPS; s++) {
        for (long long i = 0; i < n; i++) {
            vy[(size_t)i] += -0.0625f;
            px[(size_t)i] += vx[(size_t)i];
            py[(size_t)i] += vy[(size_t)i];
            pz[(size_t)i] += vz[(size_t)i];
            if (py[(size_t)i] < 0.0f) { py[(size_t)i] = -py[(size_t)i]; vy[(size_t)i] = -(vy[(size_t)i] * 0.5f); }
        }
    }
    long long total = 0;
    for (long long i = 0; i < n; i++)
        total += (long long)(px[(size_t)i] * 16.0f) + (long long)(py[(size_t)i] * 16.0f)
               + (long long)(pz[(size_t)i] * 16.0f);
#else
    struct F3 { float x, y, z; };
    struct P { F3 pos, vel; };
    static const F3 G { 0.0f, -0.0625f, 0.0f };
    std::vector<P> ps;
    ps.reserve((size_t)n);
    for (long long i = 0; i < n; i++) {
        r = xs_next(r);
        P p;
        p.pos.x = (float)xs_mod(r, 1024);
        p.pos.y = (float)xs_mod(r >> 12, 1024);
        p.pos.z = (float)xs_mod(r >> 24, 1024);
        p.vel.x = (float)((double)((long long)xs_mod(r >> 36, 256) - 128) / 16.0);
        p.vel.y = (float)((double)((long long)xs_mod(r >> 44, 256) - 128) / 16.0);
        p.vel.z = (float)((double)((long long)xs_mod(r >> 52, 256) - 128) / 16.0);
        ps.push_back(p);
    }
    for (int s = 0; s < STEPS; s++) {
        for (P &p : ps) {
  #if VARIANT == 2
            p.vel.x += G.x; p.vel.y += G.y; p.vel.z += G.z;
            p.pos.x += p.vel.x; p.pos.y += p.vel.y; p.pos.z += p.vel.z;
  #else
            p.vel.y += -0.0625f;
            p.pos.x += p.vel.x;
            p.pos.y += p.vel.y;
            p.pos.z += p.vel.z;
  #endif
            if (p.pos.y < 0.0f) { p.pos.y = -p.pos.y; p.vel.y = -(p.vel.y * 0.5f); }
        }
    }
    long long total = 0;
    for (const P &p : ps)
        total += (long long)(p.pos.x * 16.0f) + (long long)(p.pos.y * 16.0f) + (long long)(p.pos.z * 16.0f);
#endif
    emit(n);
    emit(total);
    return 0;
}
