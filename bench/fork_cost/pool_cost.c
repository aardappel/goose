/* The per-fork cost of three fork-join shapes on Windows, with empty bodies,
   for docs/design/child_threads.md §1.1 and §4.9:
   1. create P OS threads and join them (what a per-pass spawn pays)
   2. a persistent pool of P-1 threads parked on WaitOnAddress, woken per
      fork with WakeByAddressAll; the forking thread runs chunk 0 and then
      waits on a done counter
   3. the same pool, but a worker spins for a bounded time before parking
   Mode 2 is the protocol the note proposes for the runtime's pool.
   Build: clang -O2 pool_cost.c -o pool_cost.exe -lsynchronization
   Run:   pool_cost.exe [reps] */
#include <windows.h>
#include <synchapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#pragma comment(lib, "synchronization.lib")

static double now(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

/* ---- 1. create + join ---- */
static DWORD WINAPI empty_main(LPVOID p) { (void)p; return 0; }

static double create_join(int P, int reps) {
    HANDLE *h = malloc(sizeof(HANDLE) * P);
    double t0 = now();
    for (int r = 0; r < reps; r++) {
        for (int i = 0; i < P; i++) h[i] = CreateThread(NULL, 0, empty_main, NULL, 0, NULL);
        WaitForMultipleObjects(P, h, TRUE, INFINITE);
        for (int i = 0; i < P; i++) CloseHandle(h[i]);
    }
    double t = now() - t0;
    free(h);
    return t / reps;
}

/* ---- 2/3. persistent pool ---- */
typedef struct {
    volatile LONG generation;     /* bumped per fork; workers wait for a change */
    volatile LONG done;           /* chunks finished in this fork */
    volatile LONG quit;
    int spin;                     /* iterations to spin before parking */
    int P;
    void (*body)(int chunk, void *env);
    void *env;
} pool_t;

typedef struct { pool_t *pool; int index; } worker_arg;

static DWORD WINAPI pool_main(LPVOID p) {
    worker_arg *wa = (worker_arg *)p;
    pool_t *pool = wa->pool;
    LONG seen = 0;
    for (;;) {
        /* wait for a new generation: spin first, then park */
        int spun = 0;
        while (pool->generation == seen) {
            if (spun < pool->spin) { spun++; YieldProcessor(); continue; }
            LONG expect = seen;
            WaitOnAddress(&pool->generation, &expect, sizeof expect, INFINITE);
        }
        seen = pool->generation;
        if (pool->quit) return 0;
        pool->body(wa->index, pool->env);
        if (InterlockedIncrement(&pool->done) == pool->P) WakeByAddressSingle((void *)&pool->done);
    }
}

static void empty_body(int chunk, void *env) { (void)chunk; (void)env; }

static double pool_forks(int P, int reps, int spin) {
    pool_t pool = {0};
    pool.P = P;
    pool.spin = spin;
    pool.body = empty_body;
    worker_arg *args = malloc(sizeof(worker_arg) * P);
    HANDLE *h = malloc(sizeof(HANDLE) * P);
    for (int i = 1; i < P; i++) {
        args[i].pool = &pool;
        args[i].index = i;
        h[i] = CreateThread(NULL, 0, pool_main, &args[i], 0, NULL);
    }
    Sleep(50);                                          /* let them park */
    double t0 = now();
    for (int r = 0; r < reps; r++) {
        pool.done = 0;
        InterlockedIncrement(&pool.generation);
        WakeByAddressAll((void *)&pool.generation);
        pool.body(0, pool.env);                         /* the forking thread is chunk 0 */
        if (InterlockedIncrement(&pool.done) != P) {
            int spun = 0;
            while (pool.done != P) {
                if (spun < spin) { spun++; YieldProcessor(); continue; }
                LONG expect = pool.done;
                if (expect != P) WaitOnAddress(&pool.done, &expect, sizeof expect, INFINITE);
            }
        }
    }
    double t = now() - t0;
    pool.quit = 1;
    InterlockedIncrement(&pool.generation);
    WakeByAddressAll((void *)&pool.generation);
    for (int i = 1; i < P; i++) { WaitForSingleObject(h[i], INFINITE); CloseHandle(h[i]); }
    free(h);
    free(args);
    return t / reps;
}

int main(int argc, char **argv) {
    int reps = argc > 1 ? atoi(argv[1]) : 2000;
    int Ps[] = {2, 4, 8, 16, 32};
    printf("%-4s %12s %12s %12s   (us per fork-join round, empty bodies)\n",
           "P", "create+join", "pool park", "pool spin");
    for (size_t i = 0; i < sizeof Ps / sizeof *Ps; i++) {
        int P = Ps[i];
        double a = create_join(P, reps / 10 > 0 ? reps / 10 : 1);
        double b = pool_forks(P, reps, 0);
        double c = pool_forks(P, reps, 20000);
        printf("%-4d %12.1f %12.1f %12.1f\n", P, a * 1e6, b * 1e6, c * 1e6);
    }
    return 0;
}
