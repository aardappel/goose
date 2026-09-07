/* Direct runtime regression: bounded resource use under repeated worker
   creation, including workers nobody joins and concurrent/repeated waits.
   Small limits make the previous leaking region registry fail quickly. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

static volatile long live_allocations, live_regions;

static long counter_add(volatile long *p, long n) {
#ifdef _WIN32
    return InterlockedExchangeAdd(p, n);
#else
    return __sync_fetch_and_add(p, n);
#endif
}

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "runtime lifecycle check failed: %s\n", what);
        exit(2);
    }
}

static void *counted_malloc(size_t size) {
    void *p = malloc(size);
    if (p) counter_add(&live_allocations, 1);
    return p;
}

static void *counted_calloc(size_t n, size_t size) {
    void *p = calloc(n, size);
    if (p) counter_add(&live_allocations, 1);
    return p;
}

static void counted_free(void *p) {
    if (p) counter_add(&live_allocations, -1);
    free(p);
}

#ifdef _WIN32
static void *counted_virtual_alloc(void *base, SIZE_T size, DWORD kind, DWORD prot) {
    void *p = VirtualAlloc(base, size, kind, prot);
    if (p && (kind & MEM_RESERVE)) counter_add(&live_regions, 1);
    return p;
}

static BOOL counted_virtual_free(void *base, SIZE_T size, DWORD kind) {
    BOOL ok = VirtualFree(base, size, kind);
    if (ok && kind == MEM_RELEASE) counter_add(&live_regions, -1);
    return ok;
}
#define VirtualAlloc counted_virtual_alloc
#define VirtualFree counted_virtual_free
#else
static void *counted_mmap(void *base, size_t size, int prot, int flags, int fd, off_t off) {
    void *p = mmap(base, size, prot, flags, fd, off);
    if (p != MAP_FAILED) counter_add(&live_regions, 1);
    return p;
}

static int counted_munmap(void *base, size_t size) {
    int result = munmap(base, size);
    if (!result) counter_add(&live_regions, -1);
    return result;
}
#define mmap counted_mmap
#define munmap counted_munmap
#endif

#define malloc counted_malloc
#define calloc counted_calloc
#define free counted_free
#define GS_NEED_THREADS 1
#define GS_MAX_STACKS 8
#define GS_STACK_RESERVE (256u << 10)
#define GS_STACK_GAP (64u << 10)
#include "../src/runtime/runtime.h"
#include "../src/runtime/runtime_threads.h"

static gs_mutex test_mutex = GS_MUTEX_INIT;
static gs_cond test_cond = GS_COND_INIT;
static int gate_open, waiters_ready;
static int64_t descendant_id;

static void exercise_stacks(uint8_t *args) {
    int64_t stamp;
    memcpy(&stamp, args, sizeof(stamp));
    check(gs_nregions == 0, "worker starts without old mappings");
    GS_ENSURE(GS_MAX_STACKS);
    check(gs_nregions == GS_MAX_STACKS, "worker owns its region registry");
    for (int i = 0; i < GS_MAX_STACKS; ++i) {
        GS(i)->top[0] = (uint8_t)stamp;
        GS(i)->top[GS_STACK_RESERVE - 1] = (uint8_t)(stamp + i);
    }
    for (int i = 0; i < GS_MAX_STACKS; ++i) {
        check(GS(i)->top[0] == (uint8_t)stamp, "concurrent mappings are disjoint");
        check(GS(i)->top[GS_STACK_RESERVE - 1] == (uint8_t)(stamp + i),
              "last usable page is accessible");
    }
}

static void gated_worker(uint8_t *args) {
    exercise_stacks(args);
    gs_mutex_lock(&test_mutex);
    while (!gate_open) gs_cond_wait(&test_cond, &test_mutex);
    gs_mutex_unlock(&test_mutex);
    int64_t stamp;
    memcpy(&stamp, args, sizeof(stamp));
    check(GS(0)->top[0] == (uint8_t)stamp, "other workers' exits preserve this storage");
}

static void spawning_worker(uint8_t *args) {
    exercise_stacks(args);
    int64_t child = gs_thread_spawn(gated_worker, args, sizeof(int64_t));
    gs_mutex_lock(&test_mutex);
    descendant_id = child;
    gs_mutex_unlock(&test_mutex);
}

static void waiting_worker(uint8_t *args) {
    int64_t target;
    memcpy(&target, args, sizeof(target));
    gs_mutex_lock(&test_mutex);
    ++waiters_ready;
    gs_cond_broadcast(&test_cond);
    gs_mutex_unlock(&test_mutex);
    gs_thread_wait(target, __FILE__, __LINE__);
    gs_thread_wait(target, __FILE__, __LINE__);
}

/* Observe exit without calling thread_wait: cleanup must not depend on join. */
static void wait_quiescent(void) {
    gs_mutex_lock(&gs_threads_mutex);
    while (gs_threads) gs_cond_wait(&gs_threads_done, &gs_threads_mutex);
    gs_mutex_unlock(&gs_threads_mutex);
}

static void check_baseline(long allocations, long regions) {
    check(counter_add(&live_allocations, 0) == allocations,
          "all worker records, arguments and stack blocks released");
    check(counter_add(&live_regions, 0) == regions, "all worker reservations released");
    check(gs_nregions == 1 && GS(0)->top[0] == 99, "main storage survives worker exit");
}

int main(void) {
    gs_rt_init();
    GS_ENSURE(1);
    GS(0)->top[0] = 99;
    long allocations = counter_add(&live_allocations, 0);
    long regions = counter_add(&live_regions, 0);
#ifdef _WIN32
    DWORD handles_before, handles_after;
    check(GetProcessHandleCount(GetCurrentProcess(), &handles_before), "read handle count");
#endif
    int64_t oldest = -1, previous = -1;
    for (int round = 0; round < 64; ++round) {
        int64_t ids[8];
        for (int i = 0; i < 8; ++i) {
            int64_t stamp = round * 8 + i;
            ids[i] = gs_thread_spawn(exercise_stacks, &stamp, sizeof(stamp));
            check(ids[i] > previous, "thread IDs are never reused");
            previous = ids[i];
            if (oldest < 0) oldest = ids[i];
        }
        if (round % 2 == 0) {
            for (int i = 7; i >= 0; --i) gs_thread_wait(ids[i], __FILE__, __LINE__);
        } else {
            wait_quiescent();
        }
        check_baseline(allocations, regions);
        gs_thread_wait(oldest, __FILE__, __LINE__);
    }

    int64_t stamp = 42;
    int64_t parent = gs_thread_spawn(spawning_worker, &stamp, sizeof(stamp));
    gs_thread_wait(parent, __FILE__, __LINE__);
    gs_mutex_lock(&test_mutex);
    int64_t child = descendant_id;
    gate_open = 1;
    gs_cond_broadcast(&test_cond);
    gs_mutex_unlock(&test_mutex);
    gs_thread_wait(child, __FILE__, __LINE__);
    check_baseline(allocations, regions);
    gate_open = 0;

    int64_t target = gs_thread_spawn(gated_worker, &stamp, sizeof(stamp));
    int64_t waiter_a = gs_thread_spawn(waiting_worker, &target, sizeof(target));
    int64_t waiter_b = gs_thread_spawn(waiting_worker, &target, sizeof(target));
    gs_mutex_lock(&test_mutex);
    while (waiters_ready != 2) gs_cond_wait(&test_cond, &test_mutex);
    gate_open = 1;
    gs_cond_broadcast(&test_cond);
    gs_mutex_unlock(&test_mutex);
    gs_thread_wait(target, __FILE__, __LINE__);
    gs_thread_wait(waiter_a, __FILE__, __LINE__);
    gs_thread_wait(waiter_b, __FILE__, __LINE__);
    check_baseline(allocations, regions);
#ifdef _WIN32
    check(GetProcessHandleCount(GetCurrentProcess(), &handles_after), "read handle count");
    check(handles_after == handles_before, "native thread handles released");
#endif
    gs_free_thread_stacks();
    check(counter_add(&live_allocations, 0) == 0, "test releases its main stack block");
    check(counter_add(&live_regions, 0) == 0, "test releases its main reservation");
    puts("runtime thread cleanup ok");
    return 0;
}
