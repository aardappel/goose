/* Direct runtime regression: bounded resource use under repeated worker
   creation, including workers nobody joins, concurrent/repeated waits and
   contained workers that abort, whose messages the runtime keeps until a
   wait takes them; and under thread pools run phase after phase, across
   programs started and stopped in turn, both the pools a program destroys
   and those it leaves for the stop. A pool's last worker is the thread
   that waits for it, which runs tasks on stacks the pool keeps for it and
   finds its own as it left them, alone in a pool of one worker, also after
   a contained task (pool.apply_contained) aborts there. The outputs tasks
   keep for their apply go with the apply, or else with the pool. Small
   limits make a region registry that leaks entries fail quickly. */
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
#include <sys/wait.h>
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

/* A block made from nothing is one more; a grown one is the same block. */
static void *counted_realloc(void *p, size_t size) {
    void *q = realloc(p, size);
    if (q && !p) counter_add(&live_allocations, 1);
    return q;
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
#define realloc counted_realloc
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

/* Whether a task runs on main's thread, the one that waits for the pools
   here, rather than on a thread of a pool's own. */
#ifdef _WIN32
static DWORD main_thread;
static void note_main_thread(void) { main_thread = GetCurrentThreadId(); }
static int on_main_thread(void) { return GetCurrentThreadId() == main_thread; }
#else
static pthread_t main_thread;
static void note_main_thread(void) { main_thread = pthread_self(); }
static int on_main_thread(void) { return pthread_equal(pthread_self(), main_thread); }
#endif

/* A pool's task starts as a worker's thread program does, with no stack
   block or globals instance until it opens its own. One that main runs
   while it waits for a pool runs as one of the pool's workers, not as main:
   on a registry of the pool's, which exercise_stacks finds empty, under a
   worker's id. */
static void check_task_start(void) {
    check(!gs_stks && !gs_nstks && !gs_gl,
          "a pool's task starts with no stack block or globals instance");
    if (on_main_thread())
        check(gs_current_thread_id >= 0, "the thread waiting for a pool runs its tasks as a worker");
}

/* What main holds once a wait for a pool returns: its own registry, stack
   block and id again, whatever tasks it ran meanwhile. */
static void check_main_kept(void) {
    check(gs_current_thread_id == -1 && gs_nregions == 1 && gs_nstks == 1 && GS(0)->top[0] == 99,
          "the thread waiting for a pool gets its own storage back");
}

/* The pools the runtime lists as live, newest first. */
static int live_pools(void) {
    int n = 0;
    for (struct gs_pool_state *p = gs_live_pools, *before = NULL; p; before = p, p = p->next) {
        check(p->prev == before, "the live pools' list links both ways");
        ++n;
    }
    return n;
}

/* A pool's first phase, one task for each of its workers: each waits until
   all have started, so that every worker runs one and maps its stacks. */
#define POOL_WORKERS 4
static int pool_arrived, pool_phases, pool_on_main;

static void pool_barrier_task(uint8_t *args) {
    check_task_start();
    exercise_stacks(args);
    gs_mutex_lock(&test_mutex);
    pool_on_main += on_main_thread();
    int phase = pool_phases;
    if (++pool_arrived == POOL_WORKERS) {
        pool_arrived = 0;
        ++pool_phases;
        gs_cond_broadcast(&test_cond);
    }
    while (phase == pool_phases) gs_cond_wait(&test_cond, &test_mutex);
    gs_mutex_unlock(&test_mutex);
}

/* A phase's task after the first, on whichever worker takes it. */
static void pool_phase_task(uint8_t *args) {
    check_task_start();
    exercise_stacks(args);
}

/* Phases as pool.apply runs them: tasks handed to the pool, then waited
   for. A pool's workers are its threads and the thread waiting for it, so
   that main runs one of the first phase's tasks. The phases after the
   first, of more tasks than workers, run on the stacks it mapped, and leave
   the pool holding what it held after it. */
static void run_pool(gs_pool pool, int64_t seed) {
    check(pool->nthreads == POOL_WORKERS - 1,
          "a pool's workers are its threads and the thread that waits for it");
    pool_on_main = 0;
    for (int i = 0; i < POOL_WORKERS; ++i) {
        int64_t stamp = seed + i;
        gs_pool_spawn(pool, pool_barrier_task, &stamp, sizeof(stamp));
    }
    gs_pool_wait(pool);
    check(pool_on_main == 1, "the thread waiting for a pool runs a task as its last worker");
    check_main_kept();
    long allocations = counter_add(&live_allocations, 0);
    long regions = counter_add(&live_regions, 0);
    for (int phase = 1; phase < 4; ++phase) {
        for (int i = 0; i < 3 * POOL_WORKERS; ++i) {
            int64_t stamp = seed + phase * 3 * POOL_WORKERS + i;
            gs_pool_spawn(pool, pool_phase_task, &stamp, sizeof(stamp));
        }
        gs_pool_wait(pool);
        check_main_kept();
        check(counter_add(&live_allocations, 0) == allocations,
              "a pool's tasks release their records, arguments and stack blocks");
        check(counter_add(&live_regions, 0) == regions, "a pool's workers reuse their mappings");
    }
}

/* A pool of one worker has no thread of its own: the thread that waits for
   it runs every task, in the order the pool was handed them, each on the
   stacks the pool keeps for that thread, which stay mapped from task to
   task and from wait to wait. A task that opens no stack block or globals
   instance releases none, main's least of all. Destroying the pool runs
   the tasks it has not, which no thread would, then releases those
   stacks. */
static int64_t solo_order[8];
static int solo_ran;

static void solo_task(uint8_t *args) {
    check(on_main_thread(), "a pool of one worker runs its tasks on the thread waiting for it");
    check_task_start();
    int64_t stamp;
    memcpy(&stamp, args, sizeof(stamp));
    if (stamp % 2 == 0) exercise_stacks(args);
    solo_order[solo_ran++] = stamp;
}

static void run_solo_pool(void) {
    long allocations = counter_add(&live_allocations, 0);
    long regions = counter_add(&live_regions, 0);
    gs_pool solo = gs_pool_create(1, __FILE__, __LINE__);
    check(solo->nthreads == 0, "a pool of one worker has no thread of its own");
    for (int round = 0; round < 3; ++round) {
        solo_ran = 0;
        for (int i = 0; i < 8; ++i) {
            int64_t stamp = round * 8 + i;
            gs_pool_spawn(solo, solo_task, &stamp, sizeof(stamp));
        }
        check(solo_ran == 0, "a pool of one worker runs nothing until it is waited for");
        gs_pool_wait(solo);
        check(solo_ran == 8, "waiting for a pool of one worker runs all its tasks");
        for (int i = 0; i < 8; ++i)
            check(solo_order[i] == round * 8 + i, "in the order the pool was handed them");
        check_main_kept();
        check(counter_add(&live_regions, 0) == regions + GS_MAX_STACKS,
              "the thread waiting for a pool reuses the stacks the pool keeps for it");
    }
    solo_ran = 0;
    for (int i = 0; i < 3; ++i) {
        int64_t stamp = 24 + i;
        gs_pool_spawn(solo, solo_task, &stamp, sizeof(stamp));
    }
    gs_pool_destroy(solo);
    check(solo_ran == 3, "destroying a pool of one worker runs the tasks it has not");
    check_main_kept();
    check_baseline(allocations, regions);
}

/* The tasks of pool.apply_contained (gs_pool_spawn_contained), each apply
   a batch of its own. An abort in one ends that task alone, which releases
   its stack block and globals instance as a return does, wherever it runs:
   the thread waiting for the pool gets its own storage back. The wait for
   the batch says whether one aborted and takes the first abort's message;
   the batch's tasks no worker had started by then are skipped, and the pool
   runs the next batch, of either kind, as before. */
static int contained_ran, contained_started;
static gs_pool contained_pool;
static gs_pool_batch *contained_batch;

/* What a task's thunk opens, a stack block and a globals instance; those
   stamped 1 modulo 4 then abort. */
static void contained_task(uint8_t *args) {
    check_task_start();
    exercise_stacks(args);
    gs_gl = calloc(1, 64);
    check(gs_gl != NULL, "allocate a globals instance");
    int64_t stamp;
    memcpy(&stamp, args, sizeof(stamp));
    solo_order[contained_ran++] = stamp;
    if (stamp % 4 == 1) gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
}

/* An abort's message as the wait hands it over, freed. */
static int took_message(uint8_t *msg, int64_t len, const char *want) {
    size_t n = strlen(want);
    int ok = msg && len > (int64_t)n && !memcmp(msg, want, n) && msg[len - 1] == ')';
    gs_free_msg(msg);
    return ok;
}

/* A pool of one worker runs a batch's tasks in order on the thread waiting
   for it, which skips those after the first that aborts. */
static void run_contained_solo(void) {
    long allocations = counter_add(&live_allocations, 0);
    long regions = counter_add(&live_regions, 0);
    gs_pool solo = gs_pool_create(1, __FILE__, __LINE__);
    long held = counter_add(&live_allocations, 0);
    uint8_t *msg;
    int64_t len;
    for (int round = 0; round < 3; ++round) {
        contained_ran = 0;
        gs_pool_batch *batch = NULL;
        for (int i = 0; i < 8; ++i) {
            int64_t stamp = round * 8 + i;
            gs_pool_spawn_contained(solo, &batch, contained_task, &stamp, sizeof(stamp));
        }
        check(batch && batch->pool == solo, "a contained apply's first task makes its batch");
        check(!gs_pool_wait_contained(solo, batch, &msg, &len),
              "a contained batch in which a task aborts waits false");
        check(took_message(msg, len, "assert failed ("), "the wait takes the abort's message");
        check(contained_ran == 2 && solo_order[0] == round * 8 && solo_order[1] == round * 8 + 1,
              "a pool of one worker skips the tasks after the first that aborts");
        check_main_kept();
        check(counter_add(&live_allocations, 0) == held,
              "an aborted task's stack block and globals are released, the skipped tasks' records");
        check(counter_add(&live_regions, 0) == regions + GS_MAX_STACKS,
              "an aborted task's stacks stay mapped for the next task");
    }
    contained_ran = 0;
    gs_pool_batch *returned = NULL;
    for (int i = 0; i < 8; ++i) {
        int64_t stamp = 4 * i;
        gs_pool_spawn_contained(solo, &returned, contained_task, &stamp, sizeof(stamp));
    }
    check(gs_pool_wait_contained(solo, returned, &msg, &len) && !msg && len == 0 &&
          contained_ran == 8,
          "after an abort, a contained batch whose tasks return all runs and waits true");
    check(gs_pool_wait_contained(solo, NULL, &msg, &len) && !msg && len == 0,
          "an apply that handed out no task waits true");
    contained_ran = 0;
    gs_pool_batch *unasked = NULL;
    for (int i = 0; i < 8; ++i) {
        int64_t stamp = 4 * i + 1;
        gs_pool_spawn_contained(solo, &unasked, contained_task, &stamp, sizeof(stamp));
    }
    check(!gs_pool_wait_contained(solo, unasked, NULL, NULL) && contained_ran == 1,
          "a wait that asks for no message still says the batch aborted");
    contained_ran = 0;
    for (int i = 0; i < 8; ++i) {
        int64_t stamp = 4 * i + 2;
        gs_pool_spawn(solo, contained_task, &stamp, sizeof(stamp));
    }
    gs_pool_wait(solo);
    check(contained_ran == 8, "a plain batch after contained ones runs all its tasks");
    check(counter_add(&live_allocations, 0) == held,
          "a message no wait asked for is freed with its batch");
    gs_pool_destroy(solo);
    check_main_kept();
    check_baseline(allocations, regions);
}

/* The first four tasks of a batch, on a pool of four workers, start
   together. The second aborts once the others have started, which then
   abort too once the batch has recorded it, each with another message. The
   second's is the batch's and the others' are freed, and the tasks after
   the four, which the workers take only then, are skipped. */
static void contained_barrier_task(uint8_t *args) {
    check_task_start();
    exercise_stacks(args);
    gs_gl = calloc(1, 64);
    check(gs_gl != NULL, "allocate a globals instance");
    int64_t stamp;
    memcpy(&stamp, args, sizeof(stamp));
    gs_mutex_lock(&test_mutex);
    ++contained_ran;
    if (stamp >= POOL_WORKERS) {
        gs_mutex_unlock(&test_mutex);
        return;
    }
    ++contained_started;
    gs_cond_broadcast(&test_cond);
    if (stamp == 1) {
        while (contained_started < POOL_WORKERS) gs_cond_wait(&test_cond, &test_mutex);
        gs_mutex_unlock(&test_mutex);
        gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
    }
    for (;;) {
        gs_mutex_lock(&contained_pool->mutex);
        int aborted = contained_batch->aborted;
        gs_mutex_unlock(&contained_pool->mutex);
        if (aborted) break;
        gs_cond_wait_until(&test_cond, &test_mutex, gs_qclock_ns() + 1000000);
    }
    gs_mutex_unlock(&test_mutex);
    gs_abort(GS_E_POP, __FILE__, __LINE__);
}

static void run_contained_pool(void) {
    long allocations = counter_add(&live_allocations, 0);
    long regions = counter_add(&live_regions, 0);
    contained_pool = gs_pool_create(POOL_WORKERS, __FILE__, __LINE__);
    /* What the pool holds once its threads have started and run a task. */
    long held = 0;
    uint8_t *msg;
    int64_t len;
    for (int round = 0; round < 3; ++round) {
        contained_ran = contained_started = 0;
        contained_batch = NULL;
        for (int64_t stamp = 0; stamp < 3 * POOL_WORKERS; ++stamp)
            gs_pool_spawn_contained(contained_pool, &contained_batch, contained_barrier_task,
                                    &stamp, sizeof(stamp));
        check(!gs_pool_wait_contained(contained_pool, contained_batch, &msg, &len),
              "a contained batch whose tasks abort on every worker waits false");
        check(took_message(msg, len, "assert failed ("),
              "the wait takes the first abort's message, not a later one's");
        check(contained_ran == POOL_WORKERS,
              "the tasks a worker takes once one of the batch has aborted are skipped");
        check_main_kept();
        if (!round) held = counter_add(&live_allocations, 0);
        check(counter_add(&live_allocations, 0) == held,
              "the aborted tasks' storage and records are released, and the later messages");
        check(counter_add(&live_regions, 0) == regions + POOL_WORKERS * GS_MAX_STACKS,
              "each worker keeps the stacks of the task that aborted on it mapped");
    }
    contained_ran = 0;
    contained_batch = NULL;
    for (int64_t i = 0; i < 3 * POOL_WORKERS; ++i) {
        int64_t stamp = POOL_WORKERS + i;
        gs_pool_spawn_contained(contained_pool, &contained_batch, contained_barrier_task, &stamp,
                                sizeof(stamp));
    }
    check(gs_pool_wait_contained(contained_pool, contained_batch, &msg, &len) && !msg &&
          contained_ran == 3 * POOL_WORKERS,
          "the pool's workers run the next batch whole after an abort on each");
    contained_ran = 0;
    for (int64_t i = 0; i < 3 * POOL_WORKERS; ++i) {
        int64_t stamp = POOL_WORKERS + i;
        gs_pool_spawn(contained_pool, contained_barrier_task, &stamp, sizeof(stamp));
    }
    gs_pool_wait(contained_pool);
    check(contained_ran == 3 * POOL_WORKERS && counter_add(&live_allocations, 0) == held,
          "and a plain batch");
    gs_pool_destroy(contained_pool);
    contained_pool = NULL;
    check_main_kept();
    check_baseline(allocations, regions);
}

/* An apply an argument of another makes on the same pool, between the
   other's tasks, has a batch of its own: on a pool of one worker, its wait
   runs the outer apply's task handed out before it, which aborts. The outer
   batch has that abort, the inner one's tasks all run and its wait says
   none aborted, and the outer one's later task is skipped, its wait taking
   the abort's message. A batch whose wait never comes, as where a
   long-distance return leaves an apply's argument, goes with its pool. */
static void run_interleaved_batches(void) {
    long allocations = counter_add(&live_allocations, 0);
    long regions = counter_add(&live_regions, 0);
    gs_pool solo = gs_pool_create(1, __FILE__, __LINE__);
    long held = counter_add(&live_allocations, 0);
    uint8_t *msg;
    int64_t len;
    contained_ran = 0;
    gs_pool_batch *outer = NULL, *inner = NULL;
    int64_t stamp = 1;
    gs_pool_spawn_contained(solo, &outer, contained_task, &stamp, sizeof(stamp));
    for (int i = 0; i < 4; ++i) {
        stamp = 4 * i;
        gs_pool_spawn_contained(solo, &inner, contained_task, &stamp, sizeof(stamp));
    }
    check(inner != outer, "each apply has a batch of its own");
    check(gs_pool_wait_contained(solo, inner, &msg, &len) && !msg && contained_ran == 5,
          "an apply between another's tasks keeps to its own tasks' aborts");
    stamp = 2;
    gs_pool_spawn_contained(solo, &outer, contained_task, &stamp, sizeof(stamp));
    check(!gs_pool_wait_contained(solo, outer, &msg, &len) && contained_ran == 5,
          "the other's wait says its batch aborted, and its later task is skipped");
    check(took_message(msg, len, "assert failed ("), "the other's wait takes its abort's message");
    check(counter_add(&live_allocations, 0) == held, "both batches are freed");
    gs_pool_batch *left = NULL;
    stamp = 5;
    gs_pool_spawn_contained(solo, &left, contained_task, &stamp, sizeof(stamp));
    gs_pool_wait(solo);
    gs_pool_destroy(solo);
    check_main_kept();
    check_baseline(allocations, regions);
}

/* A task of a pool.apply whose worker grows a T[>..]& of its own, as its
   thunk runs it: the elements go on a stack of the task's own, and as it
   returns, into the slot its arguments name (gs_pool_keep). One whose
   arguments say so aborts first, which leaves the slot empty. */
typedef struct {
    gs_pool_out *slot;
    int64_t stamp;
    int64_t aborts;
} keeping_args;

static void keeping_task(uint8_t *args) {
    check_task_start();
    keeping_args a;
    memcpy(&a, args, sizeof a);
    gs_stack out;
    gs_stack_init(&out);
    uint8_t *base = out.top;
    int64_t n = a.stamp % 3;
    for (int64_t i = 0; i < n; ++i) {
        int64_t v = a.stamp * 10 + i;
        memcpy(out.top, &v, sizeof v);
        out.top += sizeof v;
    }
    if (a.aborts) gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
    gs_pool_keep(a.slot, base, n, sizeof(int64_t));
}

/* Whether a slot holds what keeping_task keeps for stamp. */
static int kept_as(const gs_pool_out *o, int64_t stamp) {
    int64_t n = stamp % 3;
    if (o->len != n || o->bytes != n * (int64_t)sizeof(int64_t) || (n == 0) != !o->data)
        return 0;
    for (int64_t i = 0; i < n; ++i) {
        int64_t v;
        memcpy(&v, o->data + i * sizeof v, sizeof v);
        if (v != stamp * 10 + i) return 0;
    }
    return 1;
}

static void spawn_keeping(gs_pool pool, gs_pool_batch **batch, gs_pool_out *outs, int64_t i,
                          int64_t stamp, int aborts) {
    keeping_args a = { outs + i, stamp, aborts };
    if (batch) gs_pool_spawn_contained(pool, batch, keeping_task, &a, sizeof a);
    else gs_pool_spawn(pool, keeping_task, &a, sizeof a);
}

/* An apply's outputs (gs_pool_outs_new), on a pool of four workers, each of
   which has run a task, and on one of one worker: every task keeps what it
   grew in its own slot, on whichever worker runs it, a copy that the apply
   frees with the slots. A contained batch's task that aborts keeps
   nothing, nor do those skipped after it. Outputs no apply frees go with
   the pool, once the tasks it has yet to run have kept theirs. */
static void run_kept_outputs(void) {
    long allocations = counter_add(&live_allocations, 0);
    long regions = counter_add(&live_regions, 0);
    gs_pool pools[2];
    pools[0] = gs_pool_create(POOL_WORKERS, __FILE__, __LINE__);
    pools[1] = gs_pool_create(1, __FILE__, __LINE__);
    run_pool(pools[0], 900);
    for (int k = 0; k < 2; ++k) {
        gs_pool pool = pools[k];
        for (int round = 0; round < 3; ++round) {
            long held = counter_add(&live_allocations, 0);
            gs_pool_out *outs = gs_pool_outs_new(pool, 12);
            check(counter_add(&live_allocations, 0) == held + 1,
                  "an apply's outputs are one record");
            for (int64_t i = 0; i < 12; ++i) spawn_keeping(pool, NULL, outs, i, round * 12 + i, 0);
            gs_pool_wait(pool);
            check_main_kept();
            long copies = 0;
            for (int64_t i = 0; i < 12; ++i) {
                check(kept_as(&outs[i], round * 12 + i),
                      "each task keeps what it grew in its own slot");
                copies += outs[i].data != NULL;
            }
            check(counter_add(&live_allocations, 0) == held + 1 + copies,
                  "a task that grew nothing keeps no copy");
            gs_pool_outs_free(outs);
            check(counter_add(&live_allocations, 0) == held,
                  "freeing an apply's outputs frees what its tasks kept");
        }
        long held = counter_add(&live_allocations, 0);
        gs_pool_batch *batch = NULL;
        gs_pool_out *outs = gs_pool_outs_new(pool, 12);
        for (int64_t i = 0; i < 12; ++i) spawn_keeping(pool, &batch, outs, i, 40 + i, i % 4 == 2);
        check(!gs_pool_wait_contained(pool, batch, NULL, NULL),
              "a contained batch in which a task aborts waits false");
        for (int64_t i = 0; i < 12; ++i)
            check(i % 4 == 2 ? !outs[i].data && outs[i].len == 0
                             : (!outs[i].data && outs[i].len == 0) || kept_as(&outs[i], 40 + i),
                  "a task that aborts keeps nothing, nor does one skipped");
        if (k == 1)
            check(kept_as(&outs[0], 40) && kept_as(&outs[1], 41) && outs[3].len == 0,
                  "a pool of one worker runs a batch's tasks up to the first that aborts");
        gs_pool_outs_free(outs);
        check(counter_add(&live_allocations, 0) == held, "and the others' copies are freed");
        outs = gs_pool_outs_new(pool, 4);
        for (int64_t i = 0; i < 4; ++i) spawn_keeping(pool, NULL, outs, i, 50 + i, 0);
        gs_pool_wait(pool);
        outs = gs_pool_outs_new(pool, 2);
        for (int64_t i = 0; i < 2; ++i) spawn_keeping(pool, NULL, outs, i, 60 + i, 0);
        gs_pool_destroy(pool);
        check_main_kept();
    }
    check_baseline(allocations, regions);
}

/* A contained worker is contained again once the wait for the batch it
   handed its pool has ended: an abort then ends the worker alone. */
static void waited_then_aborting_worker(uint8_t *args) {
    gs_pool pool = gs_pool_create(2, __FILE__, __LINE__);
    gs_pool_spawn(pool, pool_phase_task, args, sizeof(int64_t));
    gs_pool_wait(pool);
    gs_pool_destroy(pool);
    gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
}

/* Two pools with tasks out at once, handed out A then B and waited for A
   then B: the worker is contained again once both waits have ended, and
   not before (interleaved_handing_worker). Pools of one worker, whose
   tasks stay queued until the worker's wait runs them. */
static void interleaved_aborting_worker(uint8_t *args) {
    gs_pool a = gs_pool_create(1, __FILE__, __LINE__);
    gs_pool b = gs_pool_create(1, __FILE__, __LINE__);
    gs_pool_spawn(a, pool_phase_task, args, sizeof(int64_t));
    gs_pool_spawn(b, pool_phase_task, args, sizeof(int64_t));
    gs_pool_wait(a);
    gs_pool_wait(b);
    gs_pool_destroy(a);
    gs_pool_destroy(b);
    gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
}

#ifndef _WIN32
static int parked;

/* A task that never ends. */
static void parked_task(uint8_t *args) {
    (void)args;
    gs_mutex_lock(&test_mutex);
    parked = 1;
    gs_cond_broadcast(&test_cond);
    for (;;) gs_cond_wait(&test_cond, &test_mutex);
}

/* Stopping while a pool still has a task to finish is the runtime error a
   running worker is, rather than a wait for the task or the pool destroyed
   under it: in a process of its own, which the error ends. An idle pool,
   newer and so listed first, does not hide the busy one. */
static void check_busy_pool_stop(void) {
    int err[2];
    check(!pipe(err), "open a pipe for the stopped program's error");
    fflush(NULL);
    pid_t child = fork();
    check(child >= 0, "fork a program to stop mid-task");
    if (!child) {
        dup2(err[1], STDERR_FILENO);
        close(err[0]);
        close(err[1]);
        alarm(20);  /* a stop that waits for the task never returns */
        gs_rt_init(0, NULL, 1, 1, GS_MAX_STACKS);
        gs_pool busy = gs_pool_create(POOL_WORKERS, __FILE__, __LINE__);
        gs_pool_create(1, __FILE__, __LINE__);
        int64_t stamp = 7;
        gs_pool_spawn(busy, parked_task, &stamp, sizeof(stamp));
        gs_mutex_lock(&test_mutex);
        while (!parked) gs_cond_wait(&test_cond, &test_mutex);
        gs_mutex_unlock(&test_mutex);
        gs_thread_release();
        gs_rt_stop();
        _exit(0);
    }
    close(err[1]);
    char text[512];
    size_t n = 0;
    ssize_t got;
    while (n < sizeof(text) - 1 && (got = read(err[0], text + n, sizeof(text) - 1 - n)) > 0)
        n += (size_t)got;
    text[n] = 0;
    close(err[0]);
    int status;
    int refused = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                  WEXITSTATUS(status) == 1 &&
                  strstr(text, "goose_exit: a thread program is still running");
    if (!refused) fputs(text, stderr);
    check(refused, "stopping while a pool's task runs is a runtime error");
}

static void aborting_task(uint8_t *args) {
    (void)args;
    gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
}

/* A contained worker applying a pool of one worker, which runs the task on
   the worker's thread. */
static void pool_applying_worker(uint8_t *args) {
    gs_pool solo = gs_pool_create(1, __FILE__, __LINE__);
    gs_pool_spawn(solo, aborting_task, args, sizeof(int64_t));
    gs_pool_wait(solo);
    gs_pool_destroy(solo);
}

/* A contained worker that hands its pool a task and aborts before it waits
   for it, while the task may still be writing into the worker's storage. */
static void handing_worker(uint8_t *args) {
    gs_pool pool = gs_pool_create(2, __FILE__, __LINE__);
    gs_pool_spawn(pool, pool_phase_task, args, sizeof(int64_t));
    gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
}

/* As interleaved_aborting_worker, but aborting once A's wait has ended,
   with B's task still out. */
static void interleaved_handing_worker(uint8_t *args) {
    gs_pool a = gs_pool_create(1, __FILE__, __LINE__);
    gs_pool b = gs_pool_create(1, __FILE__, __LINE__);
    gs_pool_spawn(a, pool_phase_task, args, sizeof(int64_t));
    gs_pool_spawn(b, pool_phase_task, args, sizeof(int64_t));
    gs_pool_wait(a);
    gs_abort(GS_E_ASSERT, __FILE__, __LINE__);
}

/* Aborts a contained worker cannot contain, which end the program instead:
   in a process of its own, which the abort ends. A pool's tasks are not
   contained but where pool.apply_contained hands them out, wherever they
   run: an abort in one that a contained worker runs while it waits for the
   pool ends the program, as it would on any of the pool's own threads,
   rather than returning to the worker's own handler on the pool's stacks.
   Nor is a contained worker contained while its pool has tasks out, which
   the worker's end would release the storage of under them. */
static void check_contained_abort_ends(void (*worker)(uint8_t *), const char *what) {
    int err[2];
    check(!pipe(err), "open a pipe for the aborted program's error");
    fflush(NULL);
    pid_t child = fork();
    check(child >= 0, "fork a program whose contained worker aborts");
    if (!child) {
        dup2(err[1], STDERR_FILENO);
        close(err[0]);
        close(err[1]);
        alarm(20);
        gs_rt_init(0, NULL, 1, 1, GS_MAX_STACKS);
        int64_t stamp = 5;
        int64_t id = gs_thread_spawn(worker, &stamp, sizeof(stamp), 1);
        gs_thread_wait(id, NULL, NULL, __FILE__, __LINE__);
        _exit(0);
    }
    close(err[1]);
    char text[512];
    size_t n = 0;
    ssize_t got;
    while (n < sizeof(text) - 1 && (got = read(err[0], text + n, sizeof(text) - 1 - n)) > 0)
        n += (size_t)got;
    text[n] = 0;
    close(err[0]);
    int status;
    int ended = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
                WEXITSTATUS(status) == 1 && strstr(text, "goose runtime error: assert failed");
    if (!ended) fputs(text, stderr);
    check(ended, what);
}
#endif

int main(void) {
#ifndef _WIN32
    struct sigaction before;
    check(!sigaction(SIGSEGV, NULL, &before), "read the fault handling the runtime will find");
#endif
    note_main_thread();
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
        /* The two records of which workers aborted (gs_aborted_add) stay
           for the program's lifetime. */
        check_baseline(allocations + 2, regions);
    }
    /* A message no wait takes stays until the program ends. */
    int64_t bad = gs_thread_spawn(bad_waiting_worker, &stamp, sizeof(stamp), 1);
    check(!gs_thread_wait(bad, NULL, NULL, __FILE__, __LINE__), "a contained worker aborts in thread_wait");
    check(gs_thread_wait(target, NULL, NULL, __FILE__, __LINE__), "a plain worker that returned waits true");
    check_baseline(allocations + 3, regions);
#ifdef _WIN32
    check(GetProcessHandleCount(GetCurrentProcess(), &handles_after), "read handle count");
    check(handles_after == handles_before, "native thread handles released");
#endif
    /* The main program's end (goose_exit): its stack block and globals
       instance (main has none here) as a worker's end releases them, then
       gs_rt_stop, which takes the aborted workers' records (the message
       above among them), main's regions and the fault handling, so that a
       program can start again in the process and end as cleanly. */
    gs_thread_release();
    gs_rt_stop();
    check(counter_add(&live_allocations, 0) == 0,
          "stopping releases main's stack block and the abort records");
    check(counter_add(&live_regions, 0) == 0, "stopping releases main's reservation");
#ifndef _WIN32
    struct sigaction after;
    check(!sigaction(SIGSEGV, NULL, &after) && after.sa_sigaction == before.sa_sigaction,
          "stopping restores the fault handling the runtime found");
#endif
    gs_rt_init(0, NULL, 1, 1, GS_MAX_STACKS);
    check(gs_nregions == 1, "a program starts again after a stop");
    GS(0)->top[0] = 99;
    allocations = counter_add(&live_allocations, 0);
    regions = counter_add(&live_regions, 0);
    int64_t again = gs_thread_spawn(aborting_worker, &stamp, sizeof(stamp), 1);
    check(!gs_thread_wait(again, NULL, NULL, __FILE__, __LINE__),
          "a worker of the program started again runs, and its abort is recorded");
    check_baseline(allocations + 3, regions);
    gs_thread_release();
    gs_rt_stop();
    check(counter_add(&live_allocations, 0) == 0 && counter_add(&live_regions, 0) == 0,
          "the program started again ends as cleanly");

    /* Pools over programs started and stopped in turn, each pool run
       through its phases. The program destroys one, as a local's scope exit
       does: the oldest, the middle or the newest of those live. The stop
       destroys those it leaves, as it leaves a global's, joining their
       workers and releasing their stacks; among them one made just before
       it, whose workers may not have started. */
    for (int cycle = 0; cycle < 3; ++cycle) {
        gs_rt_init(0, NULL, 1, 1, GS_MAX_STACKS);
        GS(0)->top[0] = 99;
        allocations = counter_add(&live_allocations, 0);
        regions = counter_add(&live_regions, 0);
        gs_pool pools[3];
        long held_allocations[3], held_regions[3];
        long all_allocations = 0, all_regions = 0;
        for (int k = 0; k < 3; ++k) {
            long a = counter_add(&live_allocations, 0), m = counter_add(&live_regions, 0);
            pools[k] = gs_pool_create(POOL_WORKERS, __FILE__, __LINE__);
            run_pool(pools[k], cycle * 64 + k * 16);
            held_allocations[k] = counter_add(&live_allocations, 0) - a;
            held_regions[k] = counter_add(&live_regions, 0) - m;
            check(held_regions[k] == POOL_WORKERS * GS_MAX_STACKS,
                  "each worker of a pool keeps its stacks mapped between tasks");
            check(live_pools() == k + 1 && gs_live_pools == pools[k], "a pool made is live");
            all_allocations += held_allocations[k];
            all_regions += held_regions[k];
        }
        gs_pool_destroy(pools[cycle]);
        check(live_pools() == 2, "a pool destroyed is not live");
        check_baseline(allocations + all_allocations - held_allocations[cycle],
                       regions + all_regions - held_regions[cycle]);
        gs_pool_create(POOL_WORKERS, __FILE__, __LINE__);
        gs_thread_release();
        gs_rt_stop();
        check(!gs_live_pools, "stopping destroys the pools left");
        check(counter_add(&live_allocations, 0) == 0,
              "stopping releases the pools' records and their workers' registries");
        check(counter_add(&live_regions, 0) == 0, "stopping releases the pools' worker stacks");
    }

    /* A pool of one worker, run and destroyed; then one the program leaves
       open holding the stacks its tasks mapped on main, for the stop to
       release. */
    gs_rt_init(0, NULL, 1, 1, GS_MAX_STACKS);
    GS(0)->top[0] = 99;
    run_solo_pool();
    gs_pool left = gs_pool_create(1, __FILE__, __LINE__);
    solo_ran = 0;
    gs_pool_spawn(left, solo_task, &stamp, sizeof(stamp));
    gs_pool_wait(left);
    check(solo_ran == 1, "a pool left open ran its task");
    gs_thread_release();
    gs_rt_stop();
    check(counter_add(&live_allocations, 0) == 0 && counter_add(&live_regions, 0) == 0,
          "stopping releases the stacks a pool keeps for the thread that waits for it");

    /* Contained batches, on a pool of one worker and one of four; then
       contained workers that wait for their pools' batches and abort. */
    gs_rt_init(0, NULL, 1, 1, GS_MAX_STACKS);
    GS(0)->top[0] = 99;
    run_contained_solo();
    run_contained_pool();
    run_interleaved_batches();
    run_kept_outputs();
    int64_t waited = gs_thread_spawn(waited_then_aborting_worker, &stamp, sizeof(stamp), 1);
    check(!gs_thread_wait(waited, NULL, NULL, __FILE__, __LINE__),
          "a contained worker is contained again once it has waited for its pool");
    int64_t interleaved = gs_thread_spawn(interleaved_aborting_worker, &stamp, sizeof(stamp), 1);
    check(!gs_thread_wait(interleaved, NULL, NULL, __FILE__, __LINE__),
          "a contained worker is contained again once its pools' interleaved waits have ended");
    gs_thread_release();
    gs_rt_stop();
    check(counter_add(&live_allocations, 0) == 0 && counter_add(&live_regions, 0) == 0,
          "stopping releases what contained batches and their pools held");
#ifndef _WIN32
    check_busy_pool_stop();
    check_contained_abort_ends(pool_applying_worker,
                               "an abort in a pool's task ends the program, a contained worker "
                               "waiting or not");
    check_contained_abort_ends(handing_worker,
                               "a contained worker's abort ends the program while its pool has "
                               "tasks out");
    check_contained_abort_ends(interleaved_handing_worker,
                               "a contained worker's abort ends the program while one of its "
                               "pools has tasks out, another's wait ended");
#endif
    puts("runtime thread cleanup ok");
    return 0;
}
