/* Direct runtime checks: decommit unused Windows pages and drop them from
   the POSIX resident set; keep live partial pages and other stacks, and
   grow again after trimming. macOS growth needs no runtime-handled faults;
   Windows recommits behind a commit mark that follows each trim. This runs
   beside the Goose fixtures in both CI profiles. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#define GS_NEED_THREADS 1
#define GS_STACK_RESERVE (64u << 20)
#define GS_STACK_GAP (64u << 10)
#include "../../src/runtime/runtime.h"
#include "../../src/runtime/runtime_impl.h"
#include "../../src/runtime/runtime_threads.h"
#ifndef _WIN32
#include <sys/wait.h>
#endif

static void check(int ok, const char *what) {
    if (!ok) { fprintf(stderr, "trim memory check failed: %s\n", what); exit(2); }
}

/* The registry entry of the region starting at base. */
static const gs_region *region_of(const uint8_t *base) {
    for (long i = 0; i < gs_nregions; i++)
        if (gs_regions[i].base == base) return &gs_regions[i];
    check(0, "region registered");
    return NULL;
}

#ifndef _WIN32
/* Whether the page holding p is resident. */
static int resident(const uint8_t *p) {
#ifdef __APPLE__
    char v = 0;
#else
    unsigned char v = 0;
#endif
    uintptr_t page = (uintptr_t)p / gs_page_size * gs_page_size;
    check(mincore((void *)page, gs_page_size, &v) == 0, "mincore");
    return v & 1;
}
#endif

#ifdef __APPLE__
static volatile sig_atomic_t data_faults;
static void count_faults(int sig, siginfo_t *info, void *ctx) {
    data_faults++;
    gs_fault_handler(sig, info, ctx);
}
#endif

int main(int argc, char **argv) {
    gs_rt_init(argc, argv, 2, 3, 0);
#ifdef __APPLE__
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = count_faults;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    check(!sigaction(SIGSEGV, &sa, NULL) && !sigaction(SIGBUS, &sa, NULL),
          "count data stack faults");
#endif
    uint8_t *base = GS(0)->top;
    /* The platform may have halved the reservation (gs_reserve_region). */
    const gs_region *r0 = region_of(base);
    uint8_t *other = GS(1)->top;
    gs_stack global; gs_stack_init(&global);
    uint8_t *global_base = global.top;
    GS(0)->top = base + (2u << 20);
    base[0] = base[(2u << 20) - 1] = 17;
#ifdef __APPLE__
    check(!data_faults, "initial growth needs no signal handler");
#endif
    GS(0)->top = base;
    check(gs_trim_memory(), "trim empty and untouched stacks");
    global.top += 8197; memset(global_base, 37, 8197);
    GS(1)->top += 8197; memset(other, 61, 8197);
    size_t keeps[] = { 0, 1, gs_page_size - 1, gs_page_size, gs_page_size + 1,
                      2 * gs_page_size + 5 };
    for (size_t k = 0; k < sizeof keeps / sizeof *keeps; k++) {
        size_t count = 16u << 20, keep = keeps[k];
        GS(0)->top = base + count;
        memset(base, 85, count);
#ifndef _WIN32
        check(resident(base + count - 1), "written page is resident");
#endif
        check(gs_trim_memory(), "trim with a live large value");
        check(base[0] == 85 && base[count - 1] == 85, "large live value survives");
        GS(0)->top = base + keep;
        check(gs_trim_memory(), "trim after shrink");
        check(gs_trim_memory(), "repeat trim is harmless");
        size_t start = (keep + gs_page_size - 1) / gs_page_size * gs_page_size;
#ifdef _WIN32
        MEMORY_BASIC_INFORMATION info;
        check(VirtualQuery(base + start, &info, sizeof info) != 0, "query discarded page");
        check(info.State == MEM_RESERVE, "unused page really decommitted");
        check(VirtualQuery(base + count - 1, &info, sizeof info) != 0 &&
              info.State == MEM_RESERVE, "last used page decommitted");
        check(VirtualQuery(base + r0->usable, &info, sizeof info) != 0 &&
              info.State == MEM_RESERVE, "guard gap remains uncommitted");
        /* The next trim starts its walk from here, not the reservation's end. */
        check(r0->committed == base + start, "commit mark follows the trim");
#endif
#ifndef _WIN32
        check(!resident(base + start), "unused page leaves the resident set");
        check(!resident(base + count - 1), "last used page leaves the resident set");
#endif
        check(base[start] == 0 && base[count - 1] == 0, "discarded pages are zeroed");
        for (size_t i = 0; i < keep; i++) check(base[i] == 85, "live prefix survives");
        for (size_t i = 0; i < 8197; i++) {
            check(other[i] == 61, "other local stack survives");
            check(global_base[i] == 37, "global stack survives");
        }
        GS(0)->top = base + count;
        memset(base + keep, 93, count - keep);
        for (size_t i = keep; i < count; i++) check(base[i] == 93, "regrowth recommits");
#ifdef _WIN32
        check(r0->committed >= base + count, "fault handler raises the commit mark");
#endif
#ifdef __APPLE__
        check(!data_faults, "regrowth needs no signal handler");
#endif
    }
    /* Growth that skips pages leaves untouched pages below live data. */
    GS(0)->top = base + 1;
    check(gs_trim_memory(), "trim before skipping pages");
    GS(0)->top = base + (8u << 20);
    base[(8u << 20) - 1] = 7;
    GS(0)->top = base + 1;
    check(gs_trim_memory(), "trim over skipped pages");
    check(base[0] == 85, "live byte survives a trim over skipped pages");
#ifdef _WIN32
    check(r0->committed == base + gs_page_size, "skipped pages trimmed to the mark");
#endif
    GS(0)->top = base + r0->usable;
    base[r0->usable - 1] = 19;
    check(gs_trim_memory(), "trim a full stack");
    check(base[r0->usable - 1] == 19, "last usable byte survives");
    GS(0)->top = base + 1;
    check(gs_trim_memory(), "trim from the end of the reservation");
#ifndef _WIN32
    check(!resident(base + r0->usable - 1), "last usable page leaves the resident set");
    pid_t child = fork();
    check(child >= 0, "fork guard check");
    if (!child) {
        close(STDERR_FILENO);
        ((volatile uint8_t *)base)[r0->usable] = 1;
        _exit(0);
    }
    int status;
    check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 1,
          "guard gap still aborts after trimming");
#endif
#ifdef __APPLE__
    check(!data_faults, "sparse growth needs no signal handler");
#endif
    /* As a thread program's end does: its stack block, then its regions. */
    gs_thread_release();
    gs_release_regions();
    puts("runtime trim memory ok");
    return 0;
}
