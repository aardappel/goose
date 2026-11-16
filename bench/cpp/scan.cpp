// Collecting the positions of every byte in a set: see bench/goose/scan.goose.
//   VARIANT 0: a scalar byte loop pushing positions.
//   VARIANT 1: a memchr loop, the usual fast answer in C++.
#include "bench.h"
#include <vector>
#include <string>
#include <cstring>

int main() {
    std::string text;
    uint64_t r = 12345;
    for (long long i = 0; i < BENCH_N; i++) {
        r = xs_next(r);
        long long n = 30 + (long long)xs_mod(r, 60);
        for (long long j = 0; j < n; j++) {
            r = xs_next(r);
            text.push_back((char)('a' + xs_mod(r, 26)));
        }
        text.push_back('\n');
    }
    std::vector<size_t> pos;
    long long total = 0;
    size_t npos = 0;
    for (int pass = 0; pass < 8; pass++) {
        pos.clear();
#if VARIANT == 1
        for (const char *q = text.data();;) {
            q = (const char *)memchr(q, '\n', text.data() + text.size() - q);
            if (!q) break;
            pos.push_back((size_t)(q - text.data()));
            q++;
        }
#else
        for (size_t i = 0; i < text.size(); i++)
            if (text[i] == '\n') pos.push_back(i);
#endif
        npos = pos.size();
        for (size_t p : pos) total += (long long)p;
    }
    emit((long long)npos);
    emit(total);
    return 0;
}
