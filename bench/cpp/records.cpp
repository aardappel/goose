// Variant records: see bench/goose/records_var.goose. Every row here has to
// pad each element out to the largest variant; the string payload either adds
// an allocation (std::string) or forces a fixed-capacity buffer on everyone.
//   VARIANT 0: vector<unique_ptr<Base>> with virtual dispatch (idiomatic OO).
//   VARIANT 1: vector<variant<...>> with a std::string payload.
//   VARIANT 2: vector<variant<...>> with a fixed 15-byte payload buffer.
#include "bench.h"
#include <vector>
#include <memory>
#include <string>
#include <variant>

static std::string word(uint64_t seed) {
    std::string w;
    uint64_t r = xs_next(seed);
    long long n = 4 + (long long)xs_mod(r, 12);
    for (long long i = 0; i < n; i++) { r = xs_next(r); w.push_back((char)('a' + xs_mod(r, 26))); }
    return w;
}

struct Buf {                                  // The fixed-capacity stand-in.
    unsigned char len = 0;
    char b[15];
    void set(const std::string &s) { len = (unsigned char)s.size(); for (size_t i = 0; i < s.size(); i++) b[i] = s[i]; }
};

#if VARIANT == 0
struct Base { virtual ~Base() = default; virtual long long contrib() const = 0; };
struct Tick : Base { long long contrib() const override { return 1; } };
struct Move : Base { int32_t id; int16_t dx, dy;
    long long contrib() const override { return id + dx + dy; } };
struct Say : Base { int32_t id; std::string text;
    long long contrib() const override { return id + (long long)text.size() + (unsigned char)text[0]; } };
struct Quit : Base { int32_t id; uint8_t code;
    long long contrib() const override { return id + code; } };
#else
struct Tick { };
struct Move { int32_t id; int16_t dx, dy; };
  #if VARIANT == 1
struct Say { int32_t id; std::string text; };
  #else
struct Say { int32_t id; Buf text; };
  #endif
struct Quit { int32_t id; uint8_t code; };
using Event = std::variant<Tick, Move, Say, Quit>;
#endif

int main() {
#if VARIANT == 0
    std::vector<std::unique_ptr<Base>> log;
#else
    std::vector<Event> log;
#endif
    uint64_t r = 12345;
    for (long long i = 0; i < BENCH_N; i++) {
        r = xs_next(r);
        uint64_t k = xs_mod(r, 8);
        int32_t id = (int32_t)xs_mod(r >> 3, 100000);
        if (k < 4) {
            int16_t dx = (int16_t)((long long)xs_mod(r >> 20, 201) - 100);
            int16_t dy = (int16_t)((long long)xs_mod(r >> 40, 201) - 100);
#if VARIANT == 0
            auto m = std::make_unique<Move>(); m->id = id; m->dx = dx; m->dy = dy;
            log.push_back(std::move(m));
#else
            log.push_back(Move { id, dx, dy });
#endif
        } else if (k < 6) {
            std::string t = word(r);
#if VARIANT == 0
            auto s = std::make_unique<Say>(); s->id = id; s->text = t;
            log.push_back(std::move(s));
#elif VARIANT == 1
            log.push_back(Say { id, t });
#else
            Say s; s.id = id; s.text.set(t);
            log.push_back(s);
#endif
        } else if (k < 7) {
            uint8_t code = (uint8_t)xs_mod(r >> 20, 256);
#if VARIANT == 0
            auto q = std::make_unique<Quit>(); q->id = id; q->code = code;
            log.push_back(std::move(q));
#else
            log.push_back(Quit { id, code });
#endif
        } else {
#if VARIANT == 0
            log.push_back(std::make_unique<Tick>());
#else
            log.push_back(Tick { });
#endif
        }
    }
    long long total = 0;
    for (int p = 0; p < 4; p++) {
#if VARIANT == 0
        for (const auto &e : log) total += e->contrib();
#else
        for (const auto &e : log) {
            switch (e.index()) {
                case 0: total += 1; break;
                case 1: { const Move &m = std::get<Move>(e); total += m.id + m.dx + m.dy; break; }
                case 2: { const Say &s = std::get<Say>(e);
  #if VARIANT == 1
                          total += s.id + (long long)s.text.size() + (unsigned char)s.text[0];
  #else
                          total += s.id + s.text.len + (unsigned char)s.text.b[0];
  #endif
                          break; }
                default: { const Quit &q = std::get<Quit>(e); total += q.id + q.code; break; }
            }
        }
#endif
    }
    emit((long long)log.size());
    emit(total);
    return 0;
}
