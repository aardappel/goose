/* Direct shared-memory regression: mappings, allocation leases and borrow
   authorizations must be reclaimed after return/abort and descendant exit. */
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
static int fail_next_calloc;

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
    if (fail_next_calloc) { fail_next_calloc = 0; return NULL; }
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

static gs_mutex gate_mutex = GS_MUTEX_INIT;
static gs_cond gate_cond = GS_COND_INIT;
static int gate;
static int64_t child_id;

static void wait_gate(uint8_t *args) {
    gs_shared_cap view; memcpy(&view, args, sizeof view);
    gs_mutex_lock(&gate_mutex);
    while (!gate) gs_cond_wait(&gate_cond, &gate_mutex);
    gs_mutex_unlock(&gate_mutex);
    gs_shared_cap access = gs_shared_borrow(view, __FILE__, __LINE__);
    uint64_t value = 42;
    gs_shared_write(access, 0, &value, 8, 0, __FILE__, __LINE__);
    /* Abandoned authorization must be drained by worker cleanup. */
}
static void owned(uint8_t *args) {
    int aborting; memcpy(&aborting, args, sizeof aborting);
    uint8_t ok; gs_shared_cap owner = gs_shared_alloc(4096, &ok); check(ok, "worker allocation");
    gs_shared_cap view = gs_shared_view(owner, 0, 4096, __FILE__, __LINE__);
    gs_shared_borrow(view, __FILE__, __LINE__);
    if (aborting) gs_shared_borrow(view, __FILE__, __LINE__);
}
static void spawning(uint8_t *args) {
    (void)args;
    uint8_t ok; gs_shared_cap owner = gs_shared_alloc(4096, &ok); check(ok, "parent allocation");
    gs_shared_cap view = gs_shared_view(owner, 0, 4096, __FILE__, __LINE__);
    gs_shared_handoff(view, __FILE__, __LINE__);
    child_id = gs_thread_spawn(wait_gate, &view, sizeof view, 0);
}
static void pending_abort(uint8_t *args) {
    (void)args;
    uint8_t ok; gs_shared_cap owner = gs_shared_alloc(4096, &ok); check(ok, "pending allocation");
    gs_shared_cap view = gs_shared_view(owner, 0, 4096, __FILE__, __LINE__);
    gs_shared_handoff(view, __FILE__, __LINE__);
    gs_shared_cap invalid = {0, 0};
    gs_shared_handoff(invalid, __FILE__, __LINE__);
}
static void joined(int64_t id, int expected) {
    uint8_t *msg; int64_t len;
    check(gs_thread_wait(id, &msg, &len, __FILE__, __LINE__) == expected, "worker outcome");
    gs_free_msg(msg);
}
static void open_gate(void) {
    gs_mutex_lock(&gate_mutex); gate = 1; gs_cond_broadcast(&gate_cond); gs_mutex_unlock(&gate_mutex);
}
static void baseline(long allocations, long regions) {
    check(counter_add(&live_allocations, 0) == allocations, "worker/setup allocation leak");
    check(counter_add(&live_regions, 0) == regions, "shared mapping leak");
    for (uint64_t i = 0; i < gs_shared_highwater; i++) check(!gs_shared_at(i)->kind, "live capability after cleanup");
}
#ifdef __linux__
static long resident_pages(void) {
    FILE *f = fopen("/proc/self/statm", "r");
    long total = 0, resident = -1;
    if (f) { if (fscanf(f, "%ld %ld", &total, &resident) != 2) resident = -1; fclose(f); }
    return resident;
}
#endif
int main(void) {
    gs_rt_init(0, NULL, 1, 1, 1);
#ifdef _MSC_VER
    check(__alignof(gs_shared_cap) == 1, "packed capability ABI");
    check(__alignof(gs_shared_record) >= 8, "naturally aligned atomic metadata");
#else
    check(_Alignof(gs_shared_cap) == 1, "packed capability ABI");
    check(_Alignof(gs_shared_record) >= 8, "naturally aligned atomic metadata");
#endif
    uint8_t ok;
    gs_shared_cap denied = gs_shared_alloc(INT64_MAX, &ok);
    check(!ok && !denied.generation, "OS refuses impossible address-space allocation");
    long before = counter_add(&live_regions, 0);
    fail_next_calloc = 1;
    gs_shared_cap failed = gs_shared_alloc(4096, &ok);
    check(!ok && !failed.generation, "metadata allocation failure reports false");
    check(counter_add(&live_regions, 0) == before, "failed metadata allocation unmaps transaction");
#ifdef __linux__
    long rss_before = resident_pages();
    gs_shared_cap lazy = gs_shared_alloc(64ull << 20, &ok);
    if (ok) {
        long rss_after = resident_pages(), page = sysconf(_SC_PAGESIZE);
        if (rss_before >= 0 && rss_after >= rss_before && page > 0)
            check((uint64_t)(rss_after - rss_before) * (uint64_t)page < (8ull << 20),
                  "allocation does not eagerly touch mapped bytes");
        check(gs_shared_close(lazy, __FILE__, __LINE__), "lazy allocation close");
    }
#endif
    int aborting = 1;
    joined(gs_thread_spawn(owned, &aborting, sizeof aborting, 1), 0);
    long allocations = counter_add(&live_allocations, 0), regions = counter_add(&live_regions, 0);
    baseline(allocations, regions);
    for (int round = 0; round < 300; round++) {
        aborting = round % 2;
        joined(gs_thread_spawn(owned, &aborting, sizeof aborting, 1), !aborting);
        baseline(allocations, regions);
    }
    joined(gs_thread_spawn(pending_abort, NULL, 0, 1), 0);
    baseline(allocations, regions);
    gate = 0; joined(gs_thread_spawn(spawning, NULL, 0, 0), 1);
    check(counter_add(&live_regions, 0) == regions + 1, "child lease preserves orphan mapping");
    open_gate(); joined(child_id, 1); baseline(allocations, regions);

    gs_shared_cap owner = gs_shared_alloc(4096, &ok); check(ok, "main allocation");
    gs_shared_cap view = gs_shared_view(owner, 0, 4096, __FILE__, __LINE__);
    gate = 0; gs_shared_handoff(view, __FILE__, __LINE__);
    int64_t id = gs_thread_spawn(wait_gate, &view, sizeof view, 0);
    check(!gs_shared_close(owner, __FILE__, __LINE__), "pre-start lease prevents close");
    open_gate(); joined(id, 1);
    uint64_t highwater = gs_shared_highwater;
    for (int i = 0; i < 100000; i++) {
        gs_shared_cap access = gs_shared_borrow(view, __FILE__, __LINE__);
        uint64_t n = (uint64_t)i, out;
        gs_shared_write(access, 1, &n, 8, 1, __FILE__, __LINE__);
        gs_shared_read(access, 1, &out, 8, 1, __FILE__, __LINE__);
        check(n == out, "unaligned scalar roundtrip");
        gs_shared_release(access, __FILE__, __LINE__);
    }
    check(gs_shared_highwater == highwater, "bounded borrows reuse descriptors");
    check(gs_shared_close(owner, __FILE__, __LINE__), "main close");
    baseline(allocations, regions);
    puts("shared runtime lifecycle ok"); return 0;
}
