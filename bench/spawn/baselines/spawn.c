// Thread spawn baselines in C: raw pthread_create / CreateThread, the floor
// a Goose spawn is measured against (bench/spawn/design.md). One program, a
// mode per benchmark row:
//
//   spawn <mode> <a> <b> <target_ns> <batches>
//
// It prints one `batch units=U ns=T` line per batch, as the generated Goose
// programs do, after the same warm-up and the same doubling calibration.
// Payload rows copy the payload once into a malloc'd block the thread owns
// and frees: the least work a hand-off without shared memory can do.

#define _GNU_SOURCE
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HANDLE thr_t;
typedef DWORD thr_ret;
#define THR_CALL WINAPI
static void thr_start(thr_t *t, thr_ret (THR_CALL *f)(void *), void *p) {
    *t = CreateThread(NULL, 0, f, p, 0, NULL);
    if (!*t) abort();
}
static void thr_join(thr_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
typedef SRWLOCK mtx_t_;
typedef CONDITION_VARIABLE cnd_t_;
#define MTX_INIT SRWLOCK_INIT
#define CND_INIT CONDITION_VARIABLE_INIT
#define mtx_lock_(m) AcquireSRWLockExclusive(m)
#define mtx_unlock_(m) ReleaseSRWLockExclusive(m)
#define cnd_wait_(c, m) SleepConditionVariableSRW((c), (m), INFINITE, 0)
#define cnd_broadcast_(c) WakeAllConditionVariable(c)
#define cnd_signal_(c) WakeConditionVariable(c)
static int64_t now_ns(void) {
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (int64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
}
#else
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
typedef pthread_t thr_t;
typedef void *thr_ret;
#define THR_CALL
static void thr_start(thr_t *t, thr_ret (*f)(void *), void *p) {
    if (pthread_create(t, NULL, f, p)) abort();
}
static void thr_join(thr_t t) { pthread_join(t, NULL); }
typedef pthread_mutex_t mtx_t_;
typedef pthread_cond_t cnd_t_;
#define MTX_INIT PTHREAD_MUTEX_INITIALIZER
#define CND_INIT PTHREAD_COND_INITIALIZER
#define mtx_lock_(m) pthread_mutex_lock(m)
#define mtx_unlock_(m) pthread_mutex_unlock(m)
#define cnd_wait_(c, m) pthread_cond_wait((c), (m))
#define cnd_broadcast_(c) pthread_cond_broadcast(c)
#define cnd_signal_(c) pthread_cond_signal(c)
static int64_t now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
#endif

static _Atomic int64_t sink;
static int64_t A, B;

// --- workers -----------------------------------------------------------------

typedef struct { int64_t n; int64_t *data; } payload;

static thr_ret THR_CALL w_empty(void *p) {
    atomic_store_explicit(&sink, (int64_t)(intptr_t)p, memory_order_relaxed);
    return 0;
}

// Owns and frees its copy, as a Goose worker releases its packet.
static thr_ret THR_CALL w_payload(void *p) {
    payload *pl = (payload *)p;
    int64_t v = pl->n ? pl->data[pl->n - 1] + pl->n : 0;
    atomic_store_explicit(&sink, v, memory_order_relaxed);
    free(pl);
    return 0;
}

static payload *make_payload(const int64_t *src, int64_t n) {
    payload *pl = (payload *)malloc(sizeof(payload) + (size_t)n * 8);
    if (!pl) abort();
    pl->n = n;
    pl->data = (int64_t *)(pl + 1);
    memcpy(pl->data, src, (size_t)n * 8);
    return pl;
}

static uint64_t xs_next(uint64_t x) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    return x;
}

// The task of E6: k rounds of xorshift, as the Goose task does.
static int64_t work(int64_t k, int64_t seed) {
    uint64_t r = (uint64_t)seed * 2654435761u + 1;
    for (int64_t i = 0; i < k; i++) r = xs_next(r);
    return (int64_t)(r & 0xffff);
}

static thr_ret THR_CALL w_task(void *p) {
    int64_t seed = (int64_t)(intptr_t)p;
    atomic_fetch_add_explicit(&sink, work(A, seed), memory_order_relaxed);
    return 0;
}

// Blocks until released: the idle population of E2.
static mtx_t_ idle_m = MTX_INIT;
static cnd_t_ idle_c = CND_INIT;
static int idle_release;
static thr_ret THR_CALL w_idle(void *p) {
    (void)p;
    mtx_lock_(&idle_m);
    while (!idle_release) cnd_wait_(&idle_c, &idle_m);
    mtx_unlock_(&idle_m);
    return 0;
}

static thr_ret THR_CALL w_spawner(void *p) {
    int64_t m = (int64_t)(intptr_t)p;
    for (int64_t i = 0; i < m; i++) {
        thr_t t;
        thr_start(&t, w_empty, (void *)(intptr_t)i);
        thr_join(t);
    }
    return 0;
}

static thr_ret THR_CALL w_tree(void *p) {
    int64_t d = (int64_t)(intptr_t)p;
    if (d > 0) {
        thr_t a, b;
        thr_start(&a, w_tree, (void *)(intptr_t)(d - 1));
        thr_start(&b, w_tree, (void *)(intptr_t)(d - 1));
        thr_join(a);
        thr_join(b);
    }
    return 0;
}

// --- the pool of E6 --------------------------------------------------------

static mtx_t_ pool_m = MTX_INIT;
static cnd_t_ pool_c = CND_INIT, pool_done = CND_INIT;
static int64_t pool_next, pool_end, pool_finished, pool_quit;
static thr_ret THR_CALL w_pool(void *p) {
    (void)p;
    for (;;) {
        mtx_lock_(&pool_m);
        while (pool_next == pool_end && !pool_quit) cnd_wait_(&pool_c, &pool_m);
        if (pool_quit) { mtx_unlock_(&pool_m); return 0; }
        int64_t seed = pool_next++;
        mtx_unlock_(&pool_m);
        int64_t r = work(A, seed);
        mtx_lock_(&pool_m);
        atomic_fetch_add_explicit(&sink, r, memory_order_relaxed);
        if (++pool_finished == pool_end) cnd_signal_(&pool_done);
        mtx_unlock_(&pool_m);
    }
}

// --- rows ------------------------------------------------------------------

static int64_t *src;
static thr_t *ids;

static int64_t run(const char *mode, int64_t reps) {
    if (!strcmp(mode, "empty")) {
        for (int64_t i = 0; i < reps; i++) {
            thr_t t;
            thr_start(&t, w_empty, (void *)(intptr_t)i);
            thr_join(t);
        }
        return reps;
    }
    if (!strcmp(mode, "burst")) {
        for (int64_t r = 0; r < reps; r++) {
            for (int64_t i = 0; i < A; i++) thr_start(&ids[i], w_empty, (void *)(intptr_t)i);
            for (int64_t i = 0; i < A; i++) thr_join(ids[i]);
        }
        return reps * A;
    }
    if (!strcmp(mode, "args") || !strcmp(mode, "bytes")) {
        // args: A scalars; bytes: A bytes of i64 elements.
        int64_t n = !strcmp(mode, "args") ? A : A / 8;
        for (int64_t i = 0; i < reps; i++) {
            thr_t t;
            thr_start(&t, w_payload, make_payload(src, n));
            thr_join(t);
        }
        return reps;
    }
    if (!strcmp(mode, "live")) {
        for (int64_t i = 0; i < reps; i++) {
            thr_t t;
            thr_start(&t, w_empty, (void *)(intptr_t)i);
            thr_join(t);
        }
        return reps;
    }
    if (!strcmp(mode, "parallel")) {
        for (int64_t i = 0; i < A; i++) thr_start(&ids[i], w_spawner, (void *)(intptr_t)reps);
        for (int64_t i = 0; i < A; i++) thr_join(ids[i]);
        return reps * A;
    }
    if (!strcmp(mode, "tree")) {
        for (int64_t i = 0; i < reps; i++) {
            thr_t t;
            thr_start(&t, w_tree, (void *)(intptr_t)A);
            thr_join(t);
        }
        // Every node but the root it was started from, plus that root.
        return reps * ((int64_t)2 << A) - reps;
    }
    if (!strcmp(mode, "task_spawn")) {
        // Waves of at most 256 live tasks, as the Goose row does.
        for (int64_t i = 0; i < reps; i += 256) {
            int64_t n = reps - i < 256 ? reps - i : 256;
            for (int64_t j = 0; j < n; j++) thr_start(&ids[j], w_task, (void *)(intptr_t)(i + j));
            for (int64_t j = 0; j < n; j++) thr_join(ids[j]);
        }
        return reps;
    }
    if (!strcmp(mode, "task_pool")) {
        mtx_lock_(&pool_m);
        pool_finished = pool_next = 0;
        pool_end = reps;
        cnd_broadcast_(&pool_c);
        while (pool_finished != pool_end) cnd_wait_(&pool_done, &pool_m);
        pool_next = pool_end = pool_finished = 0;
        mtx_unlock_(&pool_m);
        return reps;
    }
    if (!strcmp(mode, "task_inline")) {
        for (int64_t i = 0; i < reps; i++)
            atomic_fetch_add_explicit(&sink, work(A, i), memory_order_relaxed);
        return reps;
    }
#ifndef _WIN32
    if (!strcmp(mode, "region")) {
        // One Goose data stack region: reserve A bytes plus a 1 MB guard,
        // protect the guard, release. What gs_reserve_region does.
        size_t size = (size_t)A + ((size_t)1 << 20);
        for (int64_t i = 0; i < reps; i++) {
            void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS
#ifdef MAP_NORESERVE
                               | MAP_NORESERVE
#endif
                           , -1, 0);
            if (p == MAP_FAILED) abort();
            if (mprotect((char *)p + A, (size_t)1 << 20, PROT_NONE)) abort();
            ((volatile char *)p)[0] = 1;
            munmap(p, size);
        }
        return reps;
    }
#endif
    return -1;
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr, "usage: spawn <mode> <a> <b> <target_ns> <batches>\n");
        return 2;
    }
    const char *mode = argv[1];
    A = atoll(argv[2]);
    B = atoll(argv[3]);
    int64_t target = atoll(argv[4]), batches = atoll(argv[5]);
    int64_t srcn = A > 64 ? A : 64;
    src = (int64_t *)malloc((size_t)srcn * 8);
    for (int64_t i = 0; i < srcn; i++) src[i] = i;
    ids = (thr_t *)calloc((size_t)(A > 256 ? A : 256), sizeof(thr_t));

    // Setup outside the timing: the idle population, or the pool.
    thr_t *idle = NULL;
    if (!strcmp(mode, "live")) {
        idle = (thr_t *)calloc((size_t)B + 1, sizeof(thr_t));
        for (int64_t i = 0; i < B; i++) thr_start(&idle[i], w_idle, NULL);
    }
    if (!strcmp(mode, "task_pool")) {
        idle = (thr_t *)calloc((size_t)B + 1, sizeof(thr_t));
        for (int64_t i = 0; i < B; i++) thr_start(&idle[i], w_pool, NULL);
    }

    if (run(mode, 1) < 0) {
        printf("unsupported %s\n", mode);
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
        printf("batch units=%lld ns=%lld\n", (long long)units, (long long)(now_ns() - t));
    }

    if (!strcmp(mode, "live")) {
        mtx_lock_(&idle_m);
        idle_release = 1;
        cnd_broadcast_(&idle_c);
        mtx_unlock_(&idle_m);
        for (int64_t i = 0; i < B; i++) thr_join(idle[i]);
    }
    if (!strcmp(mode, "task_pool")) {
        mtx_lock_(&pool_m);
        pool_quit = 1;
        cnd_broadcast_(&pool_c);
        mtx_unlock_(&pool_m);
        for (int64_t i = 0; i < B; i++) thr_join(idle[i]);
    }
    return 0;
}
