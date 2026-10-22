// Goose compiler — flat_map: an open-addressing hash map from a pointer, or a
// pair of them, to a value, for the checker's tables keyed by node or type,
// which std::unordered_map gives a heap node and a rehash walk per entry.
// The table holds keys and value indices only, so probing and growing never
// touch the values; those live in fixed chunks, where a reference to one
// stays valid as the map grows, as std::unordered_map's do. An erased key's
// value cell goes to a key added later.
#pragma once

namespace goose {

// Multiplicative hashing: the table takes the high bits, which every bit of
// the key reaches, where a pointer's low bits are all alignment.
inline uint64_t FlatHash(const void *p) {
    return (uint64_t)(uintptr_t)p * 0x9E3779B97F4A7C15ull;
}
template<typename A, typename B> uint64_t FlatHash(const pair<A *, B *> &k) {
    return FlatHash(k.first) + (uint64_t)(uintptr_t)k.second * 0xC2B2AE3D27D4EB4Full;
}

// A null key (or pair of nulls) marks an empty slot, so it is never a key.
template<typename K, typename V> class flat_map {
    static constexpr uint32_t CHUNK = 256;
    struct Slot {
        K key {};
        uint32_t index = 0;
    };
    vector<Slot> slots;            // A power of two in size, at most half full.
    vector<unique_ptr<V[]>> chunks;
    vector<uint32_t> freecells;    // Erased keys' value cells, reset.
    uint32_t count = 0;
    uint32_t used = 0;             // Value cells handed out, the free ones included.
    int shift = 64;                // 64 - log2(slots.size()).

    size_t Home(const K &k) const { return (size_t)(FlatHash(k) >> shift); }
    V &Cell(uint32_t i) const { return chunks[i / CHUNK][i % CHUNK]; }

    // The slot holding k, or the empty one where it would go.
    size_t Probe(const K &k) const {
        auto mask = slots.size() - 1;
        for (auto i = Home(k);; i = (i + 1) & mask)
            if (slots[i].key == k || slots[i].key == K {}) return i;
    }
    void Grow() {
        vector<Slot> old(std::max<size_t>(16, slots.size() * 2));
        old.swap(slots);
        shift = 64 - std::countr_zero(slots.size());
        for (auto &s : old)
            if (s.key != K {}) slots[Probe(s.key)] = s;
    }

  public:
    flat_map() = default;
    // Nothing moves a map, whose count and cells a defaulted move would leave
    // behind with no table or chunks.
    flat_map(flat_map &&) = delete;
    flat_map &operator=(flat_map &&) = delete;

    // Room for n keys without growing.
    void reserve(size_t n) {
        while (slots.size() < n * 2) Grow();
    }
    size_t size() const { return count; }
    bool empty() const { return !count; }

    V *find(const K &k) {
        assert(k != K {});
        if (!count) return nullptr;
        auto &s = slots[Probe(k)];
        return s.key == k ? &Cell(s.index) : nullptr;
    }

    V &operator[](const K &k) {
        assert(k != K {});
        if ((count + 1) * 2 > slots.size()) Grow();
        auto &s = slots[Probe(k)];
        if (s.key == k) return Cell(s.index);
        if (!freecells.empty()) {
            s.index = freecells.back();
            freecells.pop_back();
        } else {
            if (used % CHUNK == 0) chunks.push_back(make_unique<V[]>(CHUNK));
            s.index = used++;
        }
        s.key = k;
        count++;
        return Cell(s.index);
    }

    // Its value's cell is reset, for a key added later.
    void erase(const K &k) {
        assert(k != K {});
        if (!count) return;
        auto mask = slots.size() - 1;
        auto i = Probe(k);
        if (slots[i].key != k) return;
        Cell(slots[i].index) = V();
        freecells.push_back(slots[i].index);
        count--;
        // Backward-shift deletion: move up each later key of the run that
        // the hole now separates from its home slot.
        for (auto j = (i + 1) & mask; slots[j].key != K {}; j = (j + 1) & mask) {
            auto h = Home(slots[j].key);
            if (((j - h) & mask) >= ((j - i) & mask)) {
                slots[i] = slots[j];
                i = j;
            }
        }
        slots[i] = Slot();
    }
};

}  // namespace goose
