// A scene graph: see bench/goose/scene.goose. A tree with 1..4 children per
// node, whose world transforms are recomposed from the parent's every frame
// after some of the nodes move. Every transform is an axis-aligned 90-degree
// rotation with an integer translation, so all the f32 math is exact and the
// checksum is bit-identical everywhere.
//   VARIANT 0: a vector<unique_ptr<Node>> of children per node -- an
//              allocation per child plus one per child vector, and no flat
//              array to sweep, so the animation and the summation are
//              recursive walks like the update is. Idiomatic.
//   VARIANT 1: one arena vector with a fixed uint32 child array inline, which
//              is the Goose layout; recursion is by index because the arena
//              moves while it is being built. Expert.
#include "bench.h"
#include <vector>
#include <memory>

#ifndef DEPTH
#define DEPTH BENCH_N
#endif
static const long long FRAMES = 16;
static const int MAXKIDS = 4;

struct F3 { float x, y, z; };
struct Xf { float m[9]; F3 t; };

static const Xf IDENT = { { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, 0.0f } };

// One of twelve axis-aligned rotations, from the seed.
static void rotation(uint64_t seed, float *m) {
    long long axis = (long long)xs_mod(seed, 3);
    long long quarter = (long long)xs_mod(seed >> 8, 4);
    float c = 1.0f, s = 0.0f;
    if (quarter == 1) { c = 0.0f; s = 1.0f; }
    else if (quarter == 2) { c = -1.0f; s = 0.0f; }
    else if (quarter == 3) { c = 0.0f; s = -1.0f; }
    if (axis == 0) {
        m[0] = 1.0f; m[1] = 0.0f; m[2] = 0.0f;
        m[3] = 0.0f; m[4] = c;    m[5] = -s;
        m[6] = 0.0f; m[7] = s;    m[8] = c;
    } else if (axis == 1) {
        m[0] = c;    m[1] = 0.0f; m[2] = s;
        m[3] = 0.0f; m[4] = 1.0f; m[5] = 0.0f;
        m[6] = -s;   m[7] = 0.0f; m[8] = c;
    } else {
        m[0] = c;    m[1] = -s;   m[2] = 0.0f;
        m[3] = s;    m[4] = c;    m[5] = 0.0f;
        m[6] = 0.0f; m[7] = 0.0f; m[8] = 1.0f;
    }
}

// world = parent o local: a 3x3 product and a transformed translation.
static void compose(const Xf &p, const Xf &l, Xf &w) {
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float s = 0.0f;
            for (int k = 0; k < 3; k++) s = s + p.m[i * 3 + k] * l.m[k * 3 + j];
            w.m[i * 3 + j] = s;
        }
    }
    w.t.x = p.m[0] * l.t.x + p.m[1] * l.t.y + p.m[2] * l.t.z + p.t.x;
    w.t.y = p.m[3] * l.t.x + p.m[4] * l.t.y + p.m[5] * l.t.z + p.t.y;
    w.t.z = p.m[6] * l.t.x + p.m[7] * l.t.y + p.m[8] * l.t.z + p.t.z;
}

#if VARIANT == 0

struct Node {
    int32_t id;
    Xf local, world;
    std::vector<std::unique_ptr<Node>> kids;
};

static long long node_count = 0;

// The node takes its id before its children are built, so ids are the
// pre-order creation order the Goose pool has.
static std::unique_ptr<Node> build(long long depth, uint64_t seed) {
    uint64_t r = xs_next(seed);
    auto nd = std::make_unique<Node>();
    nd->id = (int32_t)node_count++;
    rotation(r, nd->local.m);
    nd->local.t = F3 { (float)xs_mod(r >> 16, 9), (float)xs_mod(r >> 24, 9), (float)xs_mod(r >> 32, 9) };
    nd->world = IDENT;
    if (depth == 0) return nd;
    long long n = 1 + (long long)xs_mod(r >> 40, MAXKIDS);
    for (long long i = 0; i < n; i++) nd->kids.push_back(build(depth - 1, xs_next(r + (uint64_t)i)));
    return nd;
}

static void animate(Node *n, long long phase, float step) {
    if (n->id % 7 == phase) { n->local.t.x = step; n->local.t.z = 8.0f - step; }
    for (auto &k : n->kids) animate(k.get(), phase, step);
}

static void update(Node *n, const Xf &parent) {
    compose(parent, n->local, n->world);
    for (auto &k : n->kids) update(k.get(), n->world);
}

static long long gather(const Node *n) {
    long long t = (long long)(n->world.t.x + n->world.t.y + n->world.t.z);
    for (const auto &k : n->kids) t += gather(k.get());
    return t;
}

int main() {
    std::unique_ptr<Node> root = build(DEPTH, 12345);
    long long total = 0;
    for (long long f = 0; f < FRAMES; f++) {
        // A seventh of the nodes move each frame: the animation.
        animate(root.get(), f % 7, (float)(f % 5));
        update(root.get(), IDENT);
        // Where everything ended up, as an integer: exact in f32, summed in i64.
        total += gather(root.get());
    }
    emit(node_count);
    emit(total);
    return 0;
}

#else

struct Node {
    int32_t id;
    Xf local, world;
    uint32_t kids[MAXKIDS];
    uint8_t nkids;
};

static std::vector<Node> pool;

// The node is pushed before its children so its id is the pre-order creation
// index; the arena may move under the recursion, so it is re-indexed after
// every child.
static uint32_t build(long long depth, uint64_t seed) {
    uint64_t r = xs_next(seed);
    uint32_t self = (uint32_t)pool.size();
    pool.push_back(Node { });
    {
        Node &nd = pool[self];
        nd.id = (int32_t)self;
        rotation(r, nd.local.m);
        nd.local.t = F3 { (float)xs_mod(r >> 16, 9), (float)xs_mod(r >> 24, 9), (float)xs_mod(r >> 32, 9) };
        nd.world = IDENT;
        nd.nkids = 0;
    }
    if (depth == 0) return self;
    long long n = 1 + (long long)xs_mod(r >> 40, MAXKIDS);
    for (long long i = 0; i < n; i++) {
        uint32_t c = build(depth - 1, xs_next(r + (uint64_t)i));
        Node &nd = pool[self];
        nd.kids[nd.nkids++] = c;
    }
    return self;
}

static void update(uint32_t i, const Xf &parent) {
    Node &n = pool[i];
    compose(parent, n.local, n.world);
    for (uint8_t k = 0; k < n.nkids; k++) update(n.kids[k], n.world);
}

int main() {
    uint32_t root = build(DEPTH, 12345);
    long long total = 0;
    for (long long f = 0; f < FRAMES; f++) {
        // A seventh of the nodes move each frame: the animation.
        long long phase = f % 7;
        float step = (float)(f % 5);
        for (Node &n : pool)
            if (n.id % 7 == phase) { n.local.t.x = step; n.local.t.z = 8.0f - step; }
        update(root, IDENT);
        // Where everything ended up, as an integer: exact in f32, summed in i64.
        for (const Node &n : pool) total += (long long)(n.world.t.x + n.world.t.y + n.world.t.z);
    }
    emit((long long)pool.size());
    emit(total);
    return 0;
}

#endif
