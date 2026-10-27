/* Direct runtime regression: bounded resource use under repeated worker
   creation, including workers nobody joins, concurrent/repeated waits and
   contained workers that abort, whose messages the runtime keeps until a
   wait takes them. Small limits make a region registry that leaks
   entries fail quickly. */
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
#include "../../src/runtime/runtime.h"
#include "../../src/runtime/runtime_impl.h"
#include "../../src/runtime/runtime_threads.h"

static gs_mutex test_mutex = GS_MUTEX_INIT;
static gs_cond test_cond = GS_COND_INIT;
static int gate_open, waiters_ready;
static int64_t descendant_id;

static void exercise_stacks(uint8_t *args) {
    int64_t stamp;
    memcpy(&stamp, args, sizeof(stamp));
    check(gs_nregions == 0, "worker starts without old mappings");
    gs_stack_block(GS_MAX_STACKS);
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
    int64_t child = gs_thread_spawn(gated_worker, args, sizeof(int64_t), 0);
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
    gs_thread_wait(target, NULL, NULL, __FILE__, __LINE__);
    gs_thread_wait(target, NULL, NULL, __FILE__, __LINE__);
}

/* As a thunk does, a globals instance, then an abort past it: the contained
   worker's storage is released as on a return. */
static void aborting_worker(uint8_t *args) {
    exercise_stacks(args);
    gs_gl = calloc(1, 64);
    check(gs_gl != NULL, "allocate a globals instance");
    gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
}

/* An abort raised under the worker registry's lock, which must be free
   again once the worker has ended. */
static void bad_waiting_worker(uint8_t *args) {
    (void)args;
    gs_thread_wait(-1, NULL, NULL, __FILE__, __LINE__);
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
    /* Main's one stack, in its one region; a worker's GS_MAX_STACKS. */
    gs_rt_init(0, NULL, 1, 1, GS_MAX_STACKS);
    check(gs_hardware_threads() >= 1, "hardware_threads() reports at least one worker");
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
            ids[i] = gs_thread_spawn(exercise_stacks, &stamp, sizeof(stamp), 0);
            check(ids[i] > previous, "thread IDs are never reused");
            previous = ids[i];
            if (oldest < 0) oldest = ids[i];
        }
        if (round % 2 == 0) {
            for (int i = 7; i >= 0; --i) gs_thread_wait(ids[i], NULL, NULL, __FILE__, __LINE__);
        } else {
            wait_quiescent();
        }
        check_baseline(allocations, regions);
        gs_thread_wait(oldest, NULL, NULL, __FILE__, __LINE__);
    }

    int64_t stamp = 42;
    int64_t parent = gs_thread_spawn(spawning_worker, &stamp, sizeof(stamp), 0);
    gs_thread_wait(parent, NULL, NULL, __FILE__, __LINE__);
    gs_mutex_lock(&test_mutex);
    int64_t child = descendant_id;
    gate_open = 1;
    gs_cond_broadcast(&test_cond);
    gs_mutex_unlock(&test_mutex);
    gs_thread_wait(child, NULL, NULL, __FILE__, __LINE__);
    check_baseline(allocations, regions);
    gate_open = 0;

    int64_t target = gs_thread_spawn(gated_worker, &stamp, sizeof(stamp), 0);
    int64_t waiter_a = gs_thread_spawn(waiting_worker, &target, sizeof(target), 0);
    int64_t waiter_b = gs_thread_spawn(waiting_worker, &target, sizeof(target), 0);
    gs_mutex_lock(&test_mutex);
    while (waiters_ready != 2) gs_cond_wait(&test_cond, &test_mutex);
    gate_open = 1;
    gs_cond_broadcast(&test_cond);
    gs_mutex_unlock(&test_mutex);
    gs_thread_wait(target, NULL, NULL, __FILE__, __LINE__);
    gs_thread_wait(waiter_a, NULL, NULL, __FILE__, __LINE__);
    gs_thread_wait(waiter_b, NULL, NULL, __FILE__, __LINE__);
    check_baseline(allocations, regions);

    for (int round = 0; round < 16; ++round) {
        int64_t ids[8];
        for (int i = 0; i < 8; ++i) {
            int64_t seed = round * 8 + i;
            ids[i] = gs_thread_spawn(i % 4 == 3 ? exercise_stacks : aborting_worker, &seed,
                                     sizeof(seed), 1);
        }
        if (round % 2 == 0) wait_quiescent();
        for (int i = 0; i < 8; ++i) {
            check(gs_thread_wait(ids[i], NULL, NULL, __FILE__, __LINE__) == (i % 4 == 3),
                  "thread_wait tells an aborted contained worker from a returned one");
            uint8_t *msg;
            int64_t len;
            static const char want[] = "assert failed (";
            check(gs_thread_wait(ids[i], &msg, &len, __FILE__, __LINE__) == (i % 4 == 3),
                  "a repeated wait gives the same answer");
            check(i % 4 == 3 ? !msg && len == 0
                             : msg && len > (int64_t)sizeof want &&
                               !memcmp(msg, want, sizeof want - 1) && msg[len - 1] == ')',
                  "the first wait to ask takes the abort's message, and a return has none");
            gs_free_msg(msg);
            check(gs_thread_wait(ids[i], &msg, &len, __FILE__, __LINE__) == (i % 4 == 3) &&
                  !msg && len == 0, "a later wait still answers, but gets no message");
        }
        check_baseline(allocations, regions);
    }
    /* A message no wait takes stays until the process ends. */
    int64_t bad = gs_thread_spawn(bad_waiting_worker, &stamp, sizeof(stamp), 1);
    check(!gs_thread_wait(bad, NULL, NULL, __FILE__, __LINE__), "a contained worker aborts in thread_wait");
    check(gs_thread_wait(target, NULL, NULL, __FILE__, __LINE__), "a plain worker that returned waits true");
    check_baseline(allocations + 1, regions);
#ifdef _WIN32
    check(GetProcessHandleCount(GetCurrentProcess(), &handles_after), "read handle count");
    check(handles_after == handles_before, "native thread handles released");
#endif
    /* As a worker's end does (gs_thread_main): the stack block and globals
       instance (main has none here), then the regions. */
    gs_thread_release();
    gs_release_regions();
    check(counter_add(&live_allocations, 0) == 1, "test releases its main stack block");
    check(counter_add(&live_regions, 0) == 0, "test releases its main reservation");
    puts("runtime thread cleanup ok");
    return 0;
}
