/* Goose runtime — threads and typed queues (§11.2). Prepended after
   runtime.h; everything is compiled out unless the program uses threads or
   queues (the compiler defines GS_NEED_THREADS 1 before the runtime paste).
   This is the one part of the runtime allowed to allocate internally. */

#if GS_NEED_THREADS

#ifdef _WIN32

typedef SRWLOCK gs_mutex;
typedef CONDITION_VARIABLE gs_cond;
#define GS_MUTEX_INIT SRWLOCK_INIT
#define GS_COND_INIT  CONDITION_VARIABLE_INIT
#define gs_mutex_lock(m)   AcquireSRWLockExclusive(m)
#define gs_mutex_unlock(m) ReleaseSRWLockExclusive(m)
#define gs_cond_wait(c, m) SleepConditionVariableSRW((c), (m), INFINITE, 0)
#define gs_cond_signal(c)  WakeConditionVariable(c)

static int64_t gs_hardware_threads(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int64_t)si.dwNumberOfProcessors;
}

#else  /* posix */

#include <pthread.h>

typedef pthread_mutex_t gs_mutex;
typedef pthread_cond_t gs_cond;
#define GS_MUTEX_INIT PTHREAD_MUTEX_INITIALIZER
#define GS_COND_INIT  PTHREAD_COND_INITIALIZER
#define gs_mutex_lock(m)   pthread_mutex_lock(m)
#define gs_mutex_unlock(m) pthread_mutex_unlock(m)
#define gs_cond_wait(c, m) pthread_cond_wait((c), (m))
#define gs_cond_signal(c)  pthread_cond_signal(c)

static int64_t gs_hardware_threads(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int64_t)n : 1;
}

#endif

/* ---------------------------------------------------------------------------
   Workers. Each runs its thread program on a fresh stack block; flat argument
   values are packed by the caller into one contiguous buffer and unpacked by
   a compiler-generated entry thunk. Ids index a growable registry; workers
   still running at exit are killed by process teardown (§11.2). */

typedef struct {
    void (*entry)(uint8_t *args);
    uint8_t *args;
    #ifdef _WIN32
        HANDLE handle;
    #else
        pthread_t handle;
    #endif
} gs_thread;

static gs_thread *gs_threads;
static int64_t gs_numthreads, gs_capthreads;
static gs_mutex gs_threads_mutex = GS_MUTEX_INIT;

#ifdef _WIN32
static DWORD WINAPI gs_thread_main(LPVOID p)
#else
static void *gs_thread_main(void *p)
#endif
{
    gs_thread *t = (gs_thread *)p;
    gs_stks = gs_new_stack_block();
    gs_nstks = 0;
    t->entry(t->args);
    free(t->args);
    #ifdef _WIN32
        return 0;
    #else
        return NULL;
    #endif
}

static int64_t gs_thread_spawn(void (*entry)(uint8_t *), const void *args, int64_t argsize) {
    gs_mutex_lock(&gs_threads_mutex);
    if (gs_numthreads == gs_capthreads) {
        gs_capthreads = gs_capthreads ? gs_capthreads * 2 : 16;
        gs_threads = (gs_thread *)realloc(gs_threads, (size_t)gs_capthreads * sizeof(gs_thread));
        if (!gs_threads) gs_abort("out of memory spawning thread", "runtime");
    }
    gs_thread *t = &gs_threads[gs_numthreads];
    int64_t id = gs_numthreads++;
    t->entry = entry;
    t->args = (uint8_t *)malloc(argsize ? (size_t)argsize : 1);
    if (!t->args) gs_abort("out of memory spawning thread", "runtime");
    memcpy(t->args, args, (size_t)argsize);
    #ifdef _WIN32
        t->handle = CreateThread(NULL, 0, gs_thread_main, t, 0, NULL);
        if (!t->handle) gs_abort("cannot create thread", "runtime");
    #else
        if (pthread_create(&t->handle, NULL, gs_thread_main, t))
            gs_abort("cannot create thread", "runtime");
    #endif
    gs_mutex_unlock(&gs_threads_mutex);
    return id;
}

static void gs_thread_wait(int64_t id, const char *loc) {
    gs_mutex_lock(&gs_threads_mutex);
    if (id < 0 || id >= gs_numthreads) gs_abort("thread_wait on unknown thread id", loc);
    gs_thread t = gs_threads[id];
    gs_mutex_unlock(&gs_threads_mutex);
    #ifdef _WIN32
        WaitForSingleObject(t.handle, INFINITE);
    #else
        pthread_join(t.handle, NULL);
    #endif
}

/* ---------------------------------------------------------------------------
   Typed queues: one per flat element type used by the program (the compiler
   emits a gs_queue global per type). Values are contiguous byte images. */

typedef struct gs_qnode {
    struct gs_qnode *next;
    int64_t size;
    /* Value bytes follow the header. */
} gs_qnode;

typedef struct {
    gs_qnode *head, *tail;
    gs_mutex mutex;
    gs_cond cond;
} gs_queue;

#define GS_QUEUE_INIT { NULL, NULL, GS_MUTEX_INIT, GS_COND_INIT }

static void gs_qput(gs_queue *q, const void *data, int64_t size) {
    gs_qnode *n = (gs_qnode *)malloc(sizeof(gs_qnode) + (size_t)size);
    if (!n) gs_abort("out of memory in qput", "runtime");
    n->next = NULL;
    n->size = size;
    memcpy(n + 1, data, (size_t)size);
    gs_mutex_lock(&q->mutex);
    if (q->tail) q->tail->next = n; else q->head = n;
    q->tail = n;
    gs_mutex_unlock(&q->mutex);
    gs_cond_signal(&q->cond);
}

/* Both return a malloc'd node the caller copies from and frees; qpoll returns
   NULL when the queue is empty. */
static gs_qnode *gs_qget(gs_queue *q) {
    gs_mutex_lock(&q->mutex);
    while (!q->head) gs_cond_wait(&q->cond, &q->mutex);
    gs_qnode *n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    gs_mutex_unlock(&q->mutex);
    return n;
}

static gs_qnode *gs_qpoll(gs_queue *q) {
    gs_mutex_lock(&q->mutex);
    gs_qnode *n = q->head;
    if (n) {
        q->head = n->next;
        if (!q->head) q->tail = NULL;
    }
    gs_mutex_unlock(&q->mutex);
    return n;
}

#else  /* !GS_NEED_THREADS: hardware_threads stays available. */

#ifdef _WIN32
static int64_t gs_hardware_threads(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (int64_t)si.dwNumberOfProcessors;
}
#else
#include <unistd.h>
static int64_t gs_hardware_threads(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int64_t)n : 1;
}
#endif

#endif  /* GS_NEED_THREADS */
