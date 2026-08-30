// A list of strings: see bench/goose/strlist.goose.
//   VARIANT 0: vector<string> -- the idiomatic one, an allocation per string
//              that outgrows the small-string buffer.
//   VARIANT 1: vector<string_view> into the one text buffer.
//   VARIANT 2: flat offsets into the text buffer: no per-word object at all.
#include "bench.h"
#include <vector>
#include <string>
#include <string_view>

int main() {
    std::string text;
    uint64_t r = 12345;
    for (long long i = 0; i < BENCH_N; i++) {
        r = xs_next(r);
        long long wl = 3 + (long long)xs_mod(r, 18);
        for (long long j = 0; j < wl; j++) {
            r = xs_next(r);
            text.push_back((char)('a' + xs_mod(r, 26)));
        }
        text.push_back(' ');
    }
    long long total = 0, count = 0;
    const size_t tl = text.size();
#if VARIANT == 2
    std::vector<uint32_t> offs;                 // start of each word, plus a tail
    for (size_t i = 0; i < tl; ) {
        size_t start = i;
        while (i < tl && text[i] != ' ') i++;
        offs.push_back((uint32_t)start);
        offs.push_back((uint32_t)i);
        while (i < tl && text[i] == ' ') i++;
    }
    count = (long long)(offs.size() / 2);
    for (int p = 0; p < 4; p++)
        for (size_t k = 0; k < offs.size(); k += 2) {
            uint32_t s = offs[k], e = offs[k + 1];
            total += (long long)(e - s) + (unsigned char)text[s] + (unsigned char)text[e - 1];
        }
#else
  #if VARIANT == 1
    std::vector<std::string_view> words;
  #else
    std::vector<std::string> words;
  #endif
    for (size_t i = 0; i < tl; ) {
        size_t start = i;
        while (i < tl && text[i] != ' ') i++;
        words.emplace_back(text.data() + start, i - start);
        while (i < tl && text[i] == ' ') i++;
    }
    count = (long long)words.size();
    for (int p = 0; p < 4; p++)
        for (const auto &w : words)
            total += (long long)w.size() + (unsigned char)w[0] + (unsigned char)w[w.size() - 1];
#endif
    emit(count);
    emit(total);
    return 0;
}
