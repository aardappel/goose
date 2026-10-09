// Thread spawn baselines in idiomatic C++: std::thread with a lambda that
// captures its payload by value, joined. Same command line and output as
// spawn.c (bench/spawn/design.md):
//
//   spawn_cpp <mode> <a> <b> <target_ns> <batches>

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static std::atomic<int64_t> sink;
static int64_t A, B;

static int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static uint64_t xs_next(uint64_t x) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}

static int64_t work(int64_t k, int64_t seed) {
    uint64_t r = (uint64_t)seed * 2654435761u + 1;
    for (int64_t i = 0; i < k; i++) r = xs_next(r);
    return (int64_t)(r & 0xffff);
}

// K scalar arguments: a by-value std::array capture, no allocation, the
// shape a C++ programmer would write for a fixed argument list.
template <int K>
static void spawn_args(int64_t reps) {
    std::array<int64_t, K == 0 ? 1 : K> a{};
    for (int i = 0; i < K; i++) a[i] = i;
    for (int64_t r = 0; r < reps; r++) {
        a[0] = r;
        std::thread t([a] { sink.store(a[0] + a[a.size() - 1], std::memory_order_relaxed); });
        t.join();
    }
}

static bool spawn_args_k(int64_t k, int64_t reps) {
    switch (k) {
    case 0: spawn_args<0>(reps); return true;
    case 1: spawn_args<1>(reps); return true;
    case 2: spawn_args<2>(reps); return true;
    case 4: spawn_args<4>(reps); return true;
    case 8: spawn_args<8>(reps); return true;
    case 16: spawn_args<16>(reps); return true;
    case 32: spawn_args<32>(reps); return true;
    case 64: spawn_args<64>(reps); return true;
    }
    return false;
}

static std::vector<int64_t> vsrc;
static std::vector<std::string> ssrc;

// Idle workers for E2, released at the end.
static std::mutex idle_m;
static std::condition_variable idle_c;
static bool idle_release;

// The pool for E6: a mutex-guarded deque of seeds.
static std::mutex pool_m;
static std::condition_variable pool_c, pool_done;
static std::deque<int64_t> pool_q;
static int64_t pool_left;
static bool pool_quit;

static void pool_worker() {
    for (;;) {
        int64_t seed;
        {
            std::unique_lock<std::mutex> l(pool_m);
            pool_c.wait(l, [] { return pool_quit || !pool_q.empty(); });
            if (pool_quit) return;
            seed = pool_q.front();
            pool_q.pop_front();
        }
        int64_t r = work(A, seed);
        std::lock_guard<std::mutex> l(pool_m);
        sink.fetch_add(r, std::memory_order_relaxed);
        if (--pool_left == 0) pool_done.notify_one();
    }
}

static void tree(int64_t d) {
    if (d == 0) return;
    std::thread a(tree, d - 1), b(tree, d - 1);
    a.join();
    b.join();
}

static int64_t run(const std::string &mode, int64_t reps) {
    if (mode == "empty" || mode == "live") {
        for (int64_t i = 0; i < reps; i++) {
            std::thread t([i] { sink.store(i, std::memory_order_relaxed); });
            t.join();
        }
        return reps;
    }
    if (mode == "burst") {
        std::vector<std::thread> ts;
        ts.reserve((size_t)A);
        for (int64_t r = 0; r < reps; r++) {
            for (int64_t i = 0; i < A; i++)
                ts.emplace_back([i] { sink.store(i, std::memory_order_relaxed); });
            for (auto &t : ts) t.join();
            ts.clear();
        }
        return reps * A;
    }
    if (mode == "args") return spawn_args_k(A, reps) ? reps : -1;
    if (mode == "bytes") {
        for (int64_t r = 0; r < reps; r++) {
            std::thread t([v = vsrc] {
                sink.store(v.empty() ? 0 : v.back() + (int64_t)v.size(), std::memory_order_relaxed);
            });
            t.join();
        }
        return reps;
    }
    if (mode == "strings") {
        for (int64_t r = 0; r < reps; r++) {
            std::thread t([v = ssrc] { sink.store((int64_t)v.size(), std::memory_order_relaxed); });
            t.join();
        }
        return reps;
    }
    if (mode == "parallel") {
        std::vector<std::thread> ts;
        for (int64_t p = 0; p < A; p++)
            ts.emplace_back([reps] {
                for (int64_t i = 0; i < reps; i++) {
                    std::thread t([i] { sink.store(i, std::memory_order_relaxed); });
                    t.join();
                }
            });
        for (auto &t : ts) t.join();
        return reps * A;
    }
    if (mode == "tree") {
        for (int64_t i = 0; i < reps; i++) {
            std::thread t(tree, A);
            t.join();
        }
        return reps * ((int64_t)2 << A) - reps;
    }
    if (mode == "task_spawn") {
        std::vector<std::thread> ts;
        for (int64_t i = 0; i < reps; i += 256) {
            int64_t n = std::min<int64_t>(256, reps - i);
            for (int64_t j = 0; j < n; j++)
                ts.emplace_back([s = i + j] { sink.fetch_add(work(A, s), std::memory_order_relaxed); });
            for (auto &t : ts) t.join();
            ts.clear();
        }
        return reps;
    }
    if (mode == "task_pool") {
        std::unique_lock<std::mutex> l(pool_m);
        pool_left = reps;
        for (int64_t i = 0; i < reps; i++) pool_q.push_back(i);
        pool_c.notify_all();
        pool_done.wait(l, [] { return pool_left == 0; });
        return reps;
    }
    if (mode == "task_inline") {
        for (int64_t i = 0; i < reps; i++) sink.fetch_add(work(A, i), std::memory_order_relaxed);
        return reps;
    }
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        std::fprintf(stderr, "usage: spawn_cpp <mode> <a> <b> <target_ns> <batches>\n");
        return 2;
    }
    std::string mode = argv[1];
    A = std::atoll(argv[2]);
    B = std::atoll(argv[3]);
    int64_t target = std::atoll(argv[4]), batches = std::atoll(argv[5]);
    if (mode == "bytes") {
        vsrc.resize((size_t)(A / 8));
        for (size_t i = 0; i < vsrc.size(); i++) vsrc[i] = (int64_t)i;
    }
    if (mode == "strings") {
        // A strings of B bytes each.
        for (int64_t i = 0; i < A; i++) ssrc.emplace_back((size_t)B, (char)('a' + i % 26));
    }

    std::vector<std::thread> idle;
    if (mode == "live")
        for (int64_t i = 0; i < B; i++)
            idle.emplace_back([] {
                std::unique_lock<std::mutex> l(idle_m);
                idle_c.wait(l, [] { return idle_release; });
            });
    if (mode == "task_pool")
        for (int64_t i = 0; i < B; i++) idle.emplace_back(pool_worker);

    if (run(mode, 1) < 0) {
        std::printf("unsupported %s\n", mode.c_str());
        return 3;
    }
    int64_t w0 = now_ns();
    for (int i = 0; i < 50 && now_ns() - w0 < target / 4; i++) run(mode, 1);
    int64_t reps = 1;
    for (;;) {
        int64_t t = now_ns();
        run(mode, reps);
        if (now_ns() - t >= target) break;
        reps *= 2;
    }
    for (int64_t b = 0; b < batches; b++) {
        int64_t t = now_ns();
        int64_t units = run(mode, reps);
        std::printf("batch units=%lld ns=%lld\n", (long long)units, (long long)(now_ns() - t));
    }

    {
        std::lock_guard<std::mutex> l(idle_m);
        idle_release = true;
    }
    idle_c.notify_all();
    {
        std::lock_guard<std::mutex> l(pool_m);
        pool_quit = true;
    }
    pool_c.notify_all();
    for (auto &t : idle) t.join();
    return 0;
}
