// A 3x3 Gaussian blur over a WxW 8-bit image, ping-ponged between two
// buffers for 16 passes: see bench/goose/blur.goose (and blur_rows.goose for
// the row-sliced form). Flat scalar data and nine loads per pixel, so there is
// nothing here for a memory model to win -- this row exists to measure what a
// checked language gives up on a stencil kernel.
//   VARIANT 0: two std::vector<uint8_t> indexed src[y * W + x] in the obvious
//              double loop, and the buffers grown by push_back. Idiomatic.
//   VARIANT 1: three source row pointers and one destination row pointer
//              hoisted per row, all __restrict, over exactly sized buffers.
//              Expert.
// The border pixels are never written, so they keep the values they were
// initialised with, in both buffers.
#include "bench.h"
#include <vector>

static const long long W = BENCH_N;
static const int PASSES = 16;

#if VARIANT == 0

static void blur(const std::vector<uint8_t> &src, std::vector<uint8_t> &dst) {
    for (long long y = 1; y < W - 1; y++) {
        for (long long x = 1; x < W - 1; x++) {
            long long i = y * W + x;
            int s = src[(size_t)(i - W - 1)]     + src[(size_t)(i - W)] * 2 + src[(size_t)(i - W + 1)]
                  + src[(size_t)(i - 1)] * 2     + src[(size_t)i] * 4       + src[(size_t)(i + 1)] * 2
                  + src[(size_t)(i + W - 1)]     + src[(size_t)(i + W)] * 2 + src[(size_t)(i + W + 1)];
            dst[(size_t)i] = (uint8_t)(s >> 4);
        }
    }
}

int main() {
    std::vector<uint8_t> a, b;
    uint64_t r = 12345;
    for (long long i = 0; i < W * W; i++) {
        r = xs_next(r);
        a.push_back((uint8_t)xs_mod(r, 256));
        b.push_back(0);
    }
    for (int p = 0; p < PASSES / 2; p++) { blur(a, b); blur(b, a); }
    long long total = 0;
    for (uint8_t v : a) total += v;
    emit(W);
    emit(total);
    return 0;
}

#else

static void blur(const uint8_t *__restrict src, uint8_t *__restrict dst) {
    for (long long y = 1; y < W - 1; y++) {
        const uint8_t *__restrict r0 = src + (y - 1) * W;
        const uint8_t *__restrict r1 = r0 + W;
        const uint8_t *__restrict r2 = r1 + W;
        uint8_t *__restrict d = dst + y * W;
        for (long long x = 1; x < W - 1; x++) {
            int s = r0[x - 1]     + r0[x] * 2 + r0[x + 1]
                  + r1[x - 1] * 2 + r1[x] * 4 + r1[x + 1] * 2
                  + r2[x - 1]     + r2[x] * 2 + r2[x + 1];
            d[x] = (uint8_t)(s >> 4);
        }
    }
}

int main() {
    std::vector<uint8_t> a((size_t)(W * W)), b((size_t)(W * W), 0);
    uint64_t r = 12345;
    for (long long i = 0; i < W * W; i++) {
        r = xs_next(r);
        a[(size_t)i] = (uint8_t)xs_mod(r, 256);
    }
    for (int p = 0; p < PASSES / 2; p++) { blur(a.data(), b.data()); blur(b.data(), a.data()); }
    long long total = 0;
    for (uint8_t v : a) total += v;
    emit(W);
    emit(total);
    return 0;
}

#endif
