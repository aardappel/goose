// Word frequency counting: see bench/goose/words.goose.
//   VARIANT 0: unordered_map<string,int> -- the idiomatic one; every distinct
//              key is copied into a separately allocated node.
//   VARIANT 1: unordered_map<string_view,int> -- keys borrow the text, but the
//              node-per-entry allocation remains.
//   VARIANT 2: a hand-rolled open-addressed table over string_view keys, the
//              direct analogue of the Goose version.
#include "bench.h"
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map>

static const int MAXW = 20;

static long long fnv(std::string_view s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) h = (h ^ c) * 0x100000001b3ull;
    return (long long)h;
}

int main() {
    const long long n = BENCH_N;
    const long long vocab = n / 10 + 16;
    uint64_t r = 12345;

    std::vector<std::string> dict;
    dict.reserve((size_t)vocab);
    for (long long i = 0; i < vocab; i++) {
        std::string w;
        r = xs_next(r);
        long long len = 3 + (long long)xs_mod(r, 12);
        for (long long j = 0; j < len; j++) { r = xs_next(r); w.push_back((char)('a' + xs_mod(r, 26))); }
        dict.push_back(w);
    }
    std::string text;
    for (long long i = 0; i < n; i++) {
        r = xs_next(r);
        uint64_t a = xs_mod(r, (uint64_t)vocab);
        text += dict[(size_t)((a * a) / (uint64_t)vocab)];
        text.push_back(' ');
    }
    const size_t tl = text.size();
    std::vector<std::string_view> words;
    for (size_t i = 0; i < tl; ) {
        size_t start = i;
        while (i < tl && text[i] != ' ') i++;
        words.emplace_back(text.data() + start, i - start);
        while (i < tl && text[i] == ' ') i++;
    }

    long long distinct = 0, total = 0;
#if VARIANT == 2
    long long nslots = 16;
    while (nslots < vocab * 4) nslots *= 2;
    const long long mask = nslots - 1;
    struct Slot { std::string_view key; int32_t count = 0; };
    std::vector<Slot> slots((size_t)nslots);
    for (std::string_view w : words) {
        long long idx = fnv(w) & mask;
        for (;;) {
            Slot &s = slots[(size_t)idx];
            if (s.count == 0) { s.key = w; s.count = 1; distinct++; break; }
            if (s.key == w) { s.count++; break; }
            idx = (idx + 1) & mask;
        }
    }
    for (const Slot &s : slots)
        if (s.count > 0) total += (long long)s.count * ((long long)s.key.size() + (unsigned char)s.key[0]);
#else
  #if VARIANT == 1
    std::unordered_map<std::string_view, int32_t> map;
    for (std::string_view w : words) map[w]++;
  #else
    std::unordered_map<std::string, int32_t> map;
    for (std::string_view w : words) map[std::string(w)]++;
  #endif
    distinct = (long long)map.size();
    for (const auto &kv : map)
        total += (long long)kv.second * ((long long)kv.first.size() + (unsigned char)kv.first[0]);
#endif
    emit((long long)words.size());
    emit(distinct);
    emit(total);
    return 0;
}
