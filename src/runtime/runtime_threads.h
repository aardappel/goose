/* Goose runtime — threads and typed queues (§11.2). Prepended after
   runtime.h; everything is compiled out unless the program uses threads or
   queues (the compiler defines GS_NEED_THREADS 1 before the runtime paste).
   This is the one part of the runtime allowed to allocate internally. */

/* hardware_threads() sizes worker pools, and a program that spawns none may
   still ask; it needs nothing below. */
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
#define gs_cond_broadcast(c) WakeAllConditionVariable(c)

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
#define gs_cond_broadcast(c) pthread_cond_broadcast(c)

#endif

/* ---------------------------------------------------------------------------
   Workers. Each runs its thread program on a fresh stack block; flat argument
   values are packed by the caller into one contiguous buffer and unpacked by
   a compiler-generated entry thunk. Only active workers have records. IDs
   increase monotonically, so an issued ID absent from the active list has
   completed; neither repeated waits nor unjoined workers retain resources.
   Workers still running at process exit are killed by teardown (§11.2). */

typedef struct gs_thread {
    struct gs_thread *next;
    int64_t id;
    void (*entry)(uint8_t *args);
    uint8_t *args;
} gs_thread;

static gs_thread *gs_threads;
static int64_t gs_numthreads;
static gs_mutex gs_threads_mutex = GS_MUTEX_INIT;
static gs_cond gs_threads_done = GS_COND_INIT;
static GS_TLS int64_t gs_current_thread_id = -1;

#ifdef _WIN32
static DWORD WINAPI gs_thread_main(LPVOID p)
#else
static void *gs_thread_main(void *p)
#endif
{
    gs_thread *t = (gs_thread *)p;
    gs_current_thread_id = t->id;
    gs_stks = gs_new_stack_block();
    gs_nstks = 0;
    t->entry(t->args);
    free(t->args);
    gs_free_thread_stacks();

    /* Publish completion only after all Goose storage has been released.
       No waiter retains a pointer to this record. The detached native thread
       releases its own final OS stack/TLS state on returning below. */
    gs_mutex_lock(&gs_threads_mutex);
    gs_thread **slot = &gs_threads;
    while (*slot != t) slot = &(*slot)->next;
    *slot = t->next;
    free(t);
    gs_cond_broadcast(&gs_threads_done);
    gs_mutex_unlock(&gs_threads_mutex);
    #ifdef _WIN32
        return 0;
    #else
        return NULL;
    #endif
}

static int64_t gs_thread_spawn(void (*entry)(uint8_t *), const void *args, int64_t argsize) {
    if (argsize < 0 || (uint64_t)argsize > SIZE_MAX)
        gs_panic("invalid thread argument size");
    gs_thread *t = (gs_thread *)malloc(sizeof(gs_thread));
    if (!t) gs_panic("out of memory spawning thread");
    t->entry = entry;
    t->args = (uint8_t *)malloc(argsize ? (size_t)argsize : 1);
    if (!t->args) {
        free(t);
        gs_panic("out of memory spawning thread");
    }
    if (argsize) memcpy(t->args, args, (size_t)argsize);
    gs_mutex_lock(&gs_threads_mutex);
    if (gs_numthreads == INT64_MAX) gs_panic("thread id space exhausted");
    int64_t id = gs_numthreads++;
    t->id = id;
    t->next = gs_threads;
    gs_threads = t;
    int failed;
    #ifdef _WIN32
        HANDLE handle = CreateThread(NULL, 0, gs_thread_main, t, 0, NULL);
        failed = handle == NULL;
        /* Closing the handle does not stop the worker. Completion is tracked
           above, so callers never need to retain or join a native handle. */
        if (handle && !CloseHandle(handle)) gs_panic("cannot close thread handle");
    #else
        pthread_t handle;
        pthread_attr_t attr;
        failed = pthread_attr_init(&attr);
        if (!failed) {
            failed = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
            if (!failed) failed = pthread_create(&handle, &attr, gs_thread_main, t);
            pthread_attr_destroy(&attr);
        }
    #endif
    if (failed) {
        gs_threads = t->next;
        --gs_numthreads;
        gs_mutex_unlock(&gs_threads_mutex);
        free(t->args);
        free(t);
        gs_panic("cannot create thread");
    }
    gs_mutex_unlock(&gs_threads_mutex);
    return id;
}

static void gs_thread_wait(int64_t id, const char *file, int line) {
    gs_mutex_lock(&gs_threads_mutex);
    if (id < 0 || id >= gs_numthreads) gs_abort(GS_E_THREADID, file, line);
    if (id == gs_current_thread_id) gs_abort(GS_E_THREADSELF, file, line);
    for (;;) {
        gs_thread *t = gs_threads;
        while (t && t->id != id) t = t->next;
        if (!t) break;
        gs_cond_wait(&gs_threads_done, &gs_threads_mutex);
    }
    gs_mutex_unlock(&gs_threads_mutex);
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
    if (!n) gs_panic("out of memory in qput");
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

#endif  /* GS_NEED_THREADS */
