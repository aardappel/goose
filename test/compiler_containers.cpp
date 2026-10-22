// The compiler's own containers (small_vector.h, flat_map.h) against the
// standard ones they stand in for, across their in-place/heap and growth
// boundaries, and flat_map's reuse of erased cells. Keep assertions active
// even in an optimized test build.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/includes.h"
#include "../src/small_vector.h"
#include "../src/flat_map.h"

using namespace goose;

struct Elem {
    int a = 0;
    void *p = nullptr;
    bool operator==(const Elem &) const = default;
};

template<int N> static bool Same(const small_vector<Elem, N> &s, const vector<Elem> &v) {
    return s.size() == v.size() && std::equal(s.begin(), s.end(), v.begin());
}

template<int N> static void SmallVector() {
    uint32_t seed = 1;
    auto rnd = [&](int n) {
        seed = seed * 1664525 + 1013904223;
        return (int)((seed >> 8) % (uint32_t)n);
    };
    for (int round = 0; round < 2000; round++) {
        small_vector<Elem, N> s;
        vector<Elem> v;
        for (int op = 0; op < 40; op++) {
            switch (rnd(10)) {
                case 0: case 1: case 2: {
                    Elem e { rnd(1000), &seed };
                    s.push_back(e);
                    v.push_back(e);
                    break;
                }
                case 3:
                    // An element of its own, which growing moves.
                    if (!v.empty()) {
                        auto i = rnd((int)v.size());
                        s.push_back(s[i]);
                        v.push_back(v[i]);
                    }
                    break;
                case 4:
                    if (!v.empty()) {
                        auto i = rnd((int)v.size()), n = rnd((int)v.size() - i + 1);
                        s.erase(s.begin() + i, s.begin() + i + n);
                        v.erase(v.begin() + i, v.begin() + i + n);
                    }
                    break;
                case 5: {
                    auto k = rnd(4);
                    erase_if(s, [&](const Elem &e) { return e.a % 4 == k; });
                    std::erase_if(v, [&](const Elem &e) { return e.a % 4 == k; });
                    break;
                }
                case 6: {
                    // Copies and moves, into in-place and heap storage both.
                    small_vector<Elem, N> c = s;
                    assert(c == s && Same(c, v));
                    small_vector<Elem, N> m = std::move(c);
                    assert(c.empty() && Same(m, v));
                    small_vector<Elem, N> big;
                    for (int i = 0; i < 3 * N; i++) big.push_back({ i, nullptr });
                    big = m;
                    assert(Same(big, v));
                    big = small_vector<Elem, N> {};
                    big = std::move(m);
                    assert(Same(big, v));
                    auto &alias = s;
                    s = alias;
                    assert(Same(s, v));
                    s = std::move(big);
                    break;
                }
                case 8: {
                    // At any position, of an element of its own too.
                    auto i = rnd((int)v.size() + 1);
                    if (!v.empty() && rnd(2)) {
                        auto k = rnd((int)v.size());
                        s.insert(s.begin() + i, s[k]);
                        v.insert(v.begin() + i, Elem(v[k]));
                    } else {
                        Elem e { rnd(1000), nullptr };
                        s.insert(s.begin() + i, e);
                        v.insert(v.begin() + i, e);
                    }
                    break;
                }
                case 9: {
                    auto n = (size_t)rnd(3 * N + 2);
                    Elem fill { rnd(1000), nullptr };
                    s.resize(n, fill);
                    v.resize(n, fill);
                    break;
                }
                case 7:
                    if (rnd(4) == 0) {
                        s.clear();
                        v.clear();
                    } else if (!v.empty()) {
                        s.pop_back();
                        v.pop_back();
                    }
                    break;
            }
            assert(Same(s, v));
            assert(s.capacity() >= s.size());
        }
    }
    small_vector<Elem, N> l = { { 1, nullptr }, { 2, nullptr } };
    assert(l.size() == 2 && l[1].a == 2 && l.back().a == 2);
    l = { { 3, nullptr } };
    assert(l.size() == 1 && l.front().a == 3);
}

static void FlatMap() {
    // Keys spaced as allocations are, which leaves the low bits all zero.
    vector<long long> storage(5001);
    auto key = [&](int i) { return &storage[i]; };
    flat_map<long long *, int> m;
    unordered_map<long long *, int> ref;
    // A reference to a value stays valid as the map grows.
    auto held = &m[key(5000)];
    *held = -1;
    uint32_t seed = 7;
    auto rnd = [&](int n) {
        seed = seed * 1664525 + 1013904223;
        return (int)((seed >> 8) % (uint32_t)n);
    };
    for (int op = 0; op < 200000; op++) {
        auto k = key(rnd(op < 100000 ? 5000 : 300));
        switch (rnd(4)) {
            case 0: case 1:
                m[k] = op;
                ref[k] = op;
                break;
            case 2:
                m.erase(k);
                ref.erase(k);
                break;
            case 3: {
                auto f = m.find(k);
                auto it = ref.find(k);
                assert((f != nullptr) == (it != ref.end()));
                if (f) assert(*f == it->second);
                break;
            }
        }
        assert(m.size() == ref.size() + 1);
    }
    for (auto &[k, v] : ref) assert(m.find(k) && *m.find(k) == v);
    for (int i = 0; i < 5000; i++) assert((m.find(key(i)) != nullptr) == (ref.count(key(i)) == 1));
    assert(m.find(key(5000)) == held && *held == -1);

    flat_map<pair<long long *, long long *>, bool> pm;
    for (int i = 0; i < 100; i++)
        for (int j = 0; j < 100; j++) pm[{ key(i), key(j) }] = (i + j) % 3 == 0;
    assert(pm.size() == 10000);
    for (int i = 0; i < 100; i++)
        for (int j = 0; j < 100; j++) assert(*pm.find({ key(i), key(j) }) == ((i + j) % 3 == 0));
    assert(!pm.find({ key(100), key(0) }));

    // An erased key's value cell goes to a key added later, reset: keys
    // erased and added in turn keep to one cell, and at random take no more
    // cells than were ever live at once, while a key never erased keeps its
    // cell and its value.
    flat_map<long long *, vector<int>> cm;
    auto kept = &cm[key(5000)];
    kept->push_back(-1);
    auto cell = &cm[key(0)];
    for (int i = 1; i <= 1000; i++) {
        cm.erase(key(i - 1));
        auto &v = cm[key(i)];
        assert(&v == cell && v.empty());
        v.push_back(i);
    }
    set<vector<int> *> cells;
    auto most = cm.size();
    for (int op = 0; op < 20000; op++) {
        auto k = key(rnd(64));
        if (rnd(2)) {
            auto added = !cm.find(k);
            auto &v = cm[k];
            assert(added == v.empty());
            if (added) v.push_back(op);
            cells.insert(&v);
        } else {
            cm.erase(k);
        }
        most = std::max(most, cm.size());
    }
    assert(cells.size() <= most);
    assert(cm.find(key(5000)) == kept && *kept == vector<int> { -1 });
    assert(*cm.find(key(1000)) == vector<int> { 1000 });
}

int main() {
    SmallVector<1>();
    SmallVector<2>();
    SmallVector<8>();
    FlatMap();
    printf("compiler containers ok\n");
    return 0;
}
