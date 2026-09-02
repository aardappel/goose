// A web service's hot loop: see bench/goose/respond.goose (and
// respond_stream.goose for the streaming shape). Per request, build a
// response -- an id, a user name, a list of line items -- render it as JSON,
// checksum the bytes with FNV-1a and throw everything away. One name in eight
// carries a quote or a backslash, so the escaping path is real.
//   VARIANT 0: the typed DTO, a std::string per name and per sku, a vector of
//              items, and a render function returning a fresh std::string:
//              an allocation per string and per list, all freed per request.
//              Idiomatic.
//   VARIANT 1: no DTO at all. The JSON is written straight into one
//              std::string that is cleared per request, numbers formatted
//              with std::to_chars into a stack buffer. The RNG is consumed in
//              exactly the same order, so the bytes are identical. Expert.
#include "bench.h"
#include <string>
#include <vector>
#include <charconv>

static uint64_t fnv(const std::string &s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (unsigned char c : s) h = (h ^ c) * 0x100000001b3ull;
    return h;
}

#if VARIANT == 0

struct Item { std::string sku; int qty, price; };
struct Resp { int64_t id; std::string user; std::vector<Item> items; };

// A user name of 3..10 letters; one in eight carries a quote or a backslash.
static std::string name_of(uint64_t seed) {
    std::string s;
    uint64_t r = xs_next(seed);
    long long n = 3 + (long long)xs_mod(r, 8);
    bool odd = xs_mod(r >> 8, 8) == 0;
    for (long long i = 0; i < n; i++) {
        r = xs_next(r);
        if (odd && i == 1) s.push_back(xs_mod(r, 2) == 0 ? '"' : '\\');
        else s.push_back((char)('a' + xs_mod(r, 26)));
    }
    return s;
}

// A stock keeping unit: two letters and four digits.
static std::string sku_of(uint64_t seed) {
    std::string s;
    uint64_t r = xs_next(seed);
    s.push_back((char)('A' + xs_mod(r, 26)));
    s.push_back((char)('A' + xs_mod(r >> 8, 26)));
    for (int i = 0; i < 4; i++) { r = xs_next(r); s.push_back((char)('0' + xs_mod(r, 10))); }
    return s;
}

static std::vector<Item> make_items(uint64_t seed, long long n) {
    std::vector<Item> t;
    uint64_t r = seed;
    for (long long i = 0; i < n; i++) {
        r = xs_next(r);
        t.push_back(Item { sku_of(r), (int)(1 + xs_mod(r >> 8, 5)), (int)(100 + xs_mod(r >> 16, 99900)) });
    }
    return t;
}

static void emit_str(std::string &out, const std::string &s) {
    out.push_back('"');
    for (char c : s) {
        if (c == '"' || c == '\\') out.push_back('\\');
        out.push_back(c);
    }
    out.push_back('"');
}

static std::string render(const Resp &resp) {
    std::string out;
    out += "{\"id\":";
    out += std::to_string(resp.id);
    out += ",\"user\":";
    emit_str(out, resp.user);
    out += ",\"items\":[";
    long long total = 0;
    bool first = true;
    for (const Item &it : resp.items) {
        if (!first) out.push_back(',');
        first = false;
        out += "{\"sku\":";
        emit_str(out, it.sku);
        out += ",\"qty\":";
        out += std::to_string(it.qty);
        out += ",\"price\":";
        out += std::to_string(it.price);
        out.push_back('}');
        total += (long long)it.qty * it.price;
    }
    out += "],\"total\":";
    out += std::to_string(total);
    out.push_back('}');
    return out;
}

// One request: the response object and its rendering are both temporaries.
static uint64_t handle(uint64_t seed, long long &len) {
    uint64_t r = xs_next(seed);
    long long n = 1 + (long long)xs_mod(r, 8);
    Resp resp { (int64_t)xs_mod(r >> 4, 1000000), name_of(r >> 8), make_items(r >> 16, n) };
    std::string out = render(resp);
    len = (long long)out.size();
    return fnv(out);
}

#else

static std::string out;                    // One buffer for the whole run.

static void emit_int(std::string &o, long long v) {
    char buf[24];
    std::to_chars_result res = std::to_chars(buf, buf + sizeof(buf), v);
    o.append(buf, res.ptr);
}

static void emit_name(std::string &o, uint64_t seed) {
    uint64_t r = xs_next(seed);
    long long n = 3 + (long long)xs_mod(r, 8);
    bool odd = xs_mod(r >> 8, 8) == 0;
    o.push_back('"');
    for (long long i = 0; i < n; i++) {
        r = xs_next(r);
        if (odd && i == 1) {
            o.push_back('\\');
            o.push_back(xs_mod(r, 2) == 0 ? '"' : '\\');
        } else {
            o.push_back((char)('a' + xs_mod(r, 26)));
        }
    }
    o.push_back('"');
}

static void emit_sku(std::string &o, uint64_t seed) {
    uint64_t r = xs_next(seed);
    o.push_back('"');
    o.push_back((char)('A' + xs_mod(r, 26)));
    o.push_back((char)('A' + xs_mod(r >> 8, 26)));
    for (int i = 0; i < 4; i++) { r = xs_next(r); o.push_back((char)('0' + xs_mod(r, 10))); }
    o.push_back('"');
}

// One request: rendered as it is generated, into the buffer of the last one.
static uint64_t handle(uint64_t seed, long long &len) {
    uint64_t r0 = xs_next(seed);
    long long n = 1 + (long long)xs_mod(r0, 8);
    out.clear();
    out += "{\"id\":";
    emit_int(out, (long long)xs_mod(r0 >> 4, 1000000));
    out += ",\"user\":";
    emit_name(out, r0 >> 8);
    out += ",\"items\":[";
    long long total = 0;
    uint64_t r = r0 >> 16;
    for (long long i = 0; i < n; i++) {
        r = xs_next(r);
        if (i > 0) out.push_back(',');
        long long qty = 1 + (long long)xs_mod(r >> 8, 5);
        long long price = 100 + (long long)xs_mod(r >> 16, 99900);
        out += "{\"sku\":";
        emit_sku(out, r);
        out += ",\"qty\":";
        emit_int(out, qty);
        out += ",\"price\":";
        emit_int(out, price);
        out.push_back('}');
        total += qty * price;
    }
    out += "],\"total\":";
    emit_int(out, total);
    out.push_back('}');
    len = (long long)out.size();
    return fnv(out);
}

#endif

int main() {
    uint64_t seed = 12345, h = 0;
    long long bytes = 0;
    for (long long i = 0; i < BENCH_N; i++) {
        seed = xs_next(seed);
        long long l = 0;
        uint64_t hh = handle(seed, l);
        h = h ^ (hh * 31 + (uint64_t)i);
        bytes += l;
    }
    emit(BENCH_N);
    emit(bytes);
    emit_u(h);
    return 0;
}
