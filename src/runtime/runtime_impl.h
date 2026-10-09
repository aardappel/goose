/* Goose runtime — what runtime.h declares and leaves to the runtime: aborts,
   the data stack regions and the faults that reach them, byte search, text
   forms and printing. Follows runtime.h, in a standalone program's unit or
   in the runtime object (runtime.h has the two); this, runtime_threads.h and
   runtime_os.h are the only parts of either that include the platform's
   headers. */

static const char *gs_errmsgs[] = {
    "limited array capacity exceeded",
    "slice bounds out of range",
    "relative reference offset overflow",
    "assert failed",
    "invalid capacity",
    "resize growth requires a fill value",
    "resize to a negative length",
    "pop on empty array",
    "thread_wait on an unknown thread id",
    "thread_wait on the current thread",
    "corrupt ADT tag",
    "serialization needs a little-endian host (not supported yet)",
    "invalid slice length",
    "slice not from this pool",
    "non-null relative reference encodes as null",
};

GS_API GS_NORETURN void gs_panic(const char *msg) {
    fprintf(stderr, "goose runtime error: %s\n", msg);
    exit(1);
}

GS_API GS_NORETURN void gs_abort(int err, const char *file, int line) {
    fprintf(stderr, "goose runtime error: %s (%s:%d)\n", gs_errmsgs[err], file, line);
    exit(1);
}

GS_API GS_NORETURN void gs_abort_msg(const uint8_t *msg, int64_t len, const char *file,
                                     int line) {
    fputs("goose runtime error: ", stderr);
    fwrite(msg, 1, (size_t)len, stderr);
    fprintf(stderr, " (%s:%d)\n", file, line);
    exit(1);
}

GS_API GS_NORETURN void gs_exit(int64_t code) {
    fflush(stdout);
    exit((int)code);
}

GS_API GS_NORETURN void gs_idxfail(int64_t i, int64_t n, const char *file, int line) {
    fprintf(stderr, "goose runtime error: index %lld out of bounds (length %lld) "
            "(%s:%d)\n", (long long)i, (long long)n, file, line);
    exit(1);
}

GS_API GS_NORETURN void gs_divfail(const char *file, int line) {
    fprintf(stderr, "goose runtime error: division by zero (%s:%d)\n", file, line);
    exit(1);
}

GS_API GS_NORETURN void gs_divovf(const char *file, int line) {
    fprintf(stderr, "goose runtime error: integer overflow in division (%s:%d)\n", file, line);
    exit(1);
}

GS_API GS_NORETURN void gs_ovf(int64_t a, const char *op, int64_t b, const char *type,
                               const char *file, int line) {
    fprintf(stderr, "goose runtime error: integer overflow (debug): %lld %s %lld at %s "
            "(%s:%d)\n", (long long)a, op, (long long)b, type, file, line);
    exit(1);
}

GS_API GS_NORETURN void gs_ovf_neg(int64_t a, const char *type, const char *file, int line) {
    fprintf(stderr, "goose runtime error: integer overflow (debug): -(%lld) at %s (%s:%d)\n",
            (long long)a, type, file, line);
    exit(1);
}

/* The message of a failing `as` check, with the source value as text. */
static GS_NORETURN void gs_asfail(const char *why, const char *num, const char *type,
                                  const char *file, int line) {
    fprintf(stderr, "goose runtime error: as conversion %s (debug): %s as %s (%s:%d)\n",
            why, num, type, file, line);
    exit(1);
}

GS_API GS_NORETURN void gs_asfail_i(const char *why, int64_t v, const char *type,
                                    const char *file, int line) {
    char num[24];
    snprintf(num, sizeof(num), "%lld", (long long)v);
    gs_asfail(why, num, type, file, line);
}

GS_API GS_NORETURN void gs_asfail_u(const char *why, uint64_t v, const char *type,
                                    const char *file, int line) {
    char num[24];
    snprintf(num, sizeof(num), "%llu", (unsigned long long)v);
    gs_asfail(why, num, type, file, line);
}

GS_API GS_NORETURN void gs_asfail_f(const char *why, double d, int f32, const char *type,
                                    const char *file, int line) {
    uint8_t num[32];
    num[f32 ? gs_fmt_f32(num, (float)d) : gs_fmt_f64(num, d)] = 0;
    gs_asfail(why, (const char *)num, type, file, line);
}

/* ---------------------------------------------------------------------------
   Data stack regions (runtime.h): the program's configuration, handed over
   by gs_rt_start, and every region owned by the current thread program, so
   the fault handler can tell "commit more" from a genuine crash and
   guard-gap overruns abort with a message. Goose workers cannot access
   another thread's storage. Keeping this registry thread-local avoids both
   races with fault handlers and signal-unsafe locks when a different worker
   allocates or exits. */

/* The program's arguments, for stdlib/os.goose (runtime_os.h). */
static int gs_argc;
static char **gs_argv;

/* What a new region reserves: the usable part, which halves whenever the
   platform refuses one (gs_reserve_region), and the guard gap behind it;
   and the usable part as configured, for the reports. */
static size_t gs_region_usable, gs_region_gap, gs_region_reserve;
/* The worker running on this thread, main's being -1 (runtime_threads.h). */
static GS_TLS int64_t gs_current_thread_id = -1;
/* The program's address space budget for regions and the most regions its
   main program and any one worker can hold, as gs_rt_start was told, and
   the workers that leaves room for: what hardware_threads() reports at
   most. */
static uint64_t gs_stack_budget;
static int64_t gs_mainregions, gs_workerregions;
static int64_t gs_thread_cap = INT64_MAX;

/* A region's own sizes travel with it: those reserved before a refusal
   halved the size are larger than those after. */
typedef struct {
    uint8_t *base;
    size_t usable, size;    /* size: with the guard gap */
} gs_region;
static GS_TLS gs_region *gs_regions;
static GS_TLS volatile long gs_nregions;
/* The registry's size: the thread program's region count, as the compiler
   worked it out, so a reservation past it is a compiler bug. */
static GS_TLS long gs_regions_cap;

/* Text without stdio, which a fault handler may not call (nor malloc):
   decimal digits, a size in the largest unit that holds it exactly, and a
   string. Each returns the end of what it wrote. */
static char *gs_dec(char *p, uint64_t v) {
    char d[24];
    int n = 0;
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) *p++ = d[--n];
    return p;
}
static char *gs_size(char *p, uint64_t bytes) {
    static const char *const units[] = { " bytes", " KB", " MB", " GB", " TB" };
    int u = 0;
    while (u < 4 && bytes && bytes % 1024 == 0) {
        bytes /= 1024;
        u++;
    }
    p = gs_dec(p, bytes);
    for (const char *s = units[u]; *s; s++) *p++ = *s;
    return p;
}
static char *gs_text(char *p, const char *s) {
    while (*s) *p++ = *s++;
    return p;
}

/* The report of a data stack overrunning its region into the guard gap,
   for one write from the fault handler: whose stacks, past how much, and
   what raises it. Fits GS_OVERFLOW_TEXT. */
#define GS_OVERFLOW_TEXT 320
static size_t gs_overflow_text(char *buf, const gs_region *r) {
    char *p = gs_text(buf, "goose runtime error: data stack overflow: a value on ");
    if (gs_current_thread_id < 0) {
        p = gs_text(p, "main's");
    } else {
        p = gs_text(p, "worker ");
        p = gs_dec(p, (uint64_t)gs_current_thread_id);
        p = gs_text(p, "'s");
    }
    p = gs_text(p, " data stacks grew past the ");
    p = gs_size(p, r->usable);
    p = gs_text(p, " reserved for it");
    if (r->usable < gs_region_reserve) {
        p = gs_text(p, " (halved from ");
        p = gs_size(p, gs_region_reserve);
        p = gs_text(p, " when the platform ran out of address space)");
    }
    p = gs_text(p, "; --stack-reserve or -DGS_STACK_RESERVE raises the reservation, "
                   "up to 256 TB\n");
    return (size_t)(p - buf);
}

/* The calling thread program's registry, empty, as it starts. */
static void gs_regions_begin(int64_t capacity) {
    gs_regions_cap = (long)capacity;
    gs_regions = (gs_region *)calloc((size_t)(capacity > 0 ? capacity : 1), sizeof(gs_region));
    if (!gs_regions) gs_panic("out of memory allocating the data stack registry");
    gs_nregions = 0;
}

static void gs_release_region(const gs_region *r);
static void gs_native_stack_free(void);

/* Unregister before unmapping, then drop the registry. */
GS_API void gs_release_regions(void) {
    while (gs_nregions) {
        long i = gs_nregions - 1;
        gs_region r = gs_regions[i];
        gs_nregions = i;
        gs_regions[i].base = NULL;
        gs_release_region(&r);
    }
    free(gs_regions);
    gs_regions = NULL;
    gs_native_stack_free();
}

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static size_t gs_page_size = 0;
#define GS_COMMIT_CHUNK (1u << 20)
/* What each thread program keeps of its native stack for reporting that
   stack's overflow (gs_native_stack_init). */
#define GS_STACK_GUARANTEE (64u << 10)

static LONG WINAPI gs_fault_filter(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code == EXCEPTION_STACK_OVERFLOW) {
        /* On the little stack the guarantee kept: WriteFile rather than
           stdio, which could want more. */
        static const char msg[] = "goose runtime error: native call stack overflow\n";
        DWORD written;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, sizeof(msg) - 1, &written, NULL);
        ExitProcess(1);
    }
    if (code != STATUS_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    uint8_t *hit = (uint8_t *)ep->ExceptionRecord->ExceptionInformation[1];
    for (long i = 0; i < gs_nregions; i++) {
        const gs_region *r = &gs_regions[i];
        if ((uintptr_t)hit - (uintptr_t)r->base < r->size) {
            /* Within the usable part: commit another chunk (clamped to the
               region) and resume. Within the gap: a data stack overran. */
            if (hit < r->base + r->usable) {
                uint8_t *page = (uint8_t *)((size_t)hit & ~(gs_page_size - 1));
                size_t n = GS_COMMIT_CHUNK;
                if (page + n > r->base + r->usable)
                    n = (size_t)(r->base + r->usable - page);
                if (VirtualAlloc(page, n, MEM_COMMIT, PAGE_READWRITE))
                    return EXCEPTION_CONTINUE_EXECUTION;
            }
            char msg[GS_OVERFLOW_TEXT];
            DWORD written;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), msg, (DWORD)gs_overflow_text(msg, r),
                      &written, NULL);
            ExitProcess(1);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* Installed once by main, before any worker can start. */
static void gs_regions_init(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    gs_page_size = si.dwPageSize;
    if (!AddVectoredExceptionHandler(1, gs_fault_filter))
        gs_panic("cannot install data stack fault handler");
}

/* Run by each thread program's thread as it starts. TinyCC's kernel32.def
   does not list SetThreadStackGuarantee, so a program it builds looks it up. */
static void gs_native_stack_init(void) {
    ULONG room = GS_STACK_GUARANTEE;
    #ifdef __TINYC__
        BOOL (WINAPI *guarantee)(PULONG) = (BOOL (WINAPI *)(PULONG))GetProcAddress(
            GetModuleHandleA("kernel32.dll"), "SetThreadStackGuarantee");
        if (guarantee) guarantee(&room);
    #else
        SetThreadStackGuarantee(&room);
    #endif
}

static void gs_native_stack_free(void) {}

/* Address space for one region, or NULL where the platform has none left
   to give: nothing is committed until the fault handler is asked. */
static uint8_t *gs_os_reserve(size_t usable, size_t size) {
    (void)usable;
    return (uint8_t *)VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE);
}

static void gs_release_region(const gs_region *r) {
    if (!VirtualFree(r->base, 0, MEM_RELEASE))
        gs_panic("cannot release data stack address space");
}

#else  /* posix */

#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

/* The calling thread's native stack, from GS_NATIVE_SLOP below its lowest
   usable address up to its top: a fault in there is that stack overflowing,
   whether on the guard below it or at a limit the kernel would not grow it
   past. The slop covers a frame larger than the guard reaching over it.
   Empty where the platform cannot tell. */
#define GS_NATIVE_SLOP (64u << 10)
static GS_TLS uintptr_t gs_native_lo, gs_native_hi;
/* The alternate signal stack gs_native_stack_init allocated, if it did. */
static GS_TLS void *gs_sigstack;
#define GS_SIGSTACK_MIN (64u << 10)
/* What the fault handler replaced, for the faults that are not its own. */
static struct sigaction gs_prev_segv, gs_prev_bus;

static void gs_fault_handler(int sig, siginfo_t *info, void *ctx) {
    (void)ctx;
    uint8_t *hit = (uint8_t *)info->si_addr;
    for (long i = 0; i < gs_nregions; i++) {
        const gs_region *r = &gs_regions[i];
        if ((uintptr_t)hit - (uintptr_t)r->base < r->size) {
            char msg[GS_OVERFLOW_TEXT];
            ssize_t w = write(2, msg, gs_overflow_text(msg, r));
            (void)w;
            _exit(1);
        }
    }
    if ((uintptr_t)hit - gs_native_lo < gs_native_hi - gs_native_lo) {
        static const char msg[] = "goose runtime error: native call stack overflow\n";
        ssize_t w = write(2, msg, sizeof(msg) - 1);
        (void)w;
        _exit(1);
    }
    /* Not ours: the access faults again under what handled it before, the
       default action or a sanitizer's report. */
    sigaction(sig, sig == SIGSEGV ? &gs_prev_segv : &gs_prev_bus, NULL);
}

static void gs_regions_init(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = gs_fault_handler;
    /* A native stack overflow leaves the handler no room on the thread's own. */
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, &gs_prev_segv))
        gs_panic("cannot install data stack fault handler");
    #ifdef SIGBUS
        if (sigaction(SIGBUS, &sa, &gs_prev_bus))
            gs_panic("cannot install data stack fault handler");
    #endif
}

#ifdef __linux__
/* pthread.h declares it only under _GNU_SOURCE, which would have to come
   before every system header a file includes. */
int pthread_getattr_np(pthread_t, pthread_attr_t *);
#endif

static size_t gs_sigstack_size(void) {
    return SIGSTKSZ > GS_SIGSTACK_MIN ? (size_t)SIGSTKSZ : (size_t)GS_SIGSTACK_MIN;
}

/* Run by each thread program's thread as it starts: an alternate stack for
   the fault handler, unless the thread has one (ASan gives every thread its
   own), and the bounds of the thread's stack. */
static void gs_native_stack_init(void) {
    stack_t ss;
    uintptr_t lo = 0, hi = 0;
    if (!sigaltstack(NULL, &ss) && (ss.ss_flags & SS_DISABLE)) {
        ss.ss_size = gs_sigstack_size();
        ss.ss_sp = malloc(ss.ss_size);
        ss.ss_flags = 0;
        if (ss.ss_sp && !sigaltstack(&ss, NULL)) gs_sigstack = ss.ss_sp;
        else free(ss.ss_sp);
    }
    #if defined(__APPLE__)
        hi = (uintptr_t)pthread_get_stackaddr_np(pthread_self());
        lo = hi - pthread_get_stacksize_np(pthread_self());
    #elif defined(__linux__)
        pthread_attr_t attr;
        void *base;
        size_t size;
        if (!pthread_getattr_np(pthread_self(), &attr)) {
            if (!pthread_attr_getstack(&attr, &base, &size)) {
                lo = (uintptr_t)base;
                hi = lo + size;
            }
            pthread_attr_destroy(&attr);
        }
    #endif
    if (lo > GS_NATIVE_SLOP) {
        gs_native_lo = lo - GS_NATIVE_SLOP;
        gs_native_hi = hi;
    }
}

/* At a thread program's end, the alternate stack gs_native_stack_init gave
   it. */
static void gs_native_stack_free(void) {
    stack_t ss;
    if (!gs_sigstack) return;
    memset(&ss, 0, sizeof(ss));
    ss.ss_flags = SS_DISABLE;
    ss.ss_size = gs_sigstack_size();
    sigaltstack(&ss, NULL);
    free(gs_sigstack);
    gs_sigstack = NULL;
}

/* Address space for one region, or NULL where the platform has none left
   to give: its address space or an address-space limit exhausted, or
   strict overcommit accounting, which counts even a MAP_NORESERVE mapping.
   Commit-on-touch via overcommit; the gap at the end stays PROT_NONE. */
static uint8_t *gs_os_reserve(size_t usable, size_t size) {
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS
                   #ifdef MAP_NORESERVE
                       | MAP_NORESERVE
                   #endif
                   , -1, 0);
    if (p == MAP_FAILED) return NULL;
    if (mprotect((uint8_t *)p + usable, size - usable, PROT_NONE)) {
        munmap(p, size);
        gs_panic("cannot protect data stack guard gap");
    }
    return (uint8_t *)p;
}

static void gs_release_region(const gs_region *r) {
    if (munmap(r->base, r->size)) gs_panic("cannot release data stack address space");
}

#endif

/* The smallest a region gets before a refusal is fatal. */
#define GS_REGION_MIN (1u << 20)

/* A region of the size new ones currently get. Where the platform refuses
   one, every region from then on is half as large, down to GS_REGION_MIN:
   a smaller region is always safe, since the checks the compiler leaves
   out assume no more than GS_STACK_RESERVE bytes in a stack, and the guard
   gap behind a smaller one aborts growth that much sooner. Workers racing
   through a refusal may each halve the size once more than needed, or
   restore a larger one for a moment; it only ever settles downward. */
GS_API uint8_t *gs_reserve_region(void) {
    if (gs_nregions >= gs_regions_cap)
        gs_panic("too many data stack regions");
    size_t usable = gs_region_usable;
    uint8_t *p;
    for (;;) {
        p = gs_os_reserve(usable, usable + gs_region_gap);
        if (p) break;
        if (usable <= GS_REGION_MIN) gs_panic("cannot reserve data stack address space");
        usable /= 2;
        gs_region_usable = usable;
    }
    long i = gs_nregions;
    gs_regions[i].base = p;
    gs_regions[i].usable = usable;
    gs_regions[i].size = usable + gs_region_gap;
    gs_nregions = i + 1;  /* Publish only the initialized entry. */
    return p;
}

/* ---------------------------------------------------------------------------
   Byte search: std's find_any and find_pair (docs/stdlib.md). A set arrives
   as std's ByteSet, made by its byte_set, which gs_byteset lays out as the
   Goose struct is (spec C.2: packed, in declaration order). `members` is
   the set. `kind` says how a block of bytes is tested for it: by comparing
   with 1 to 3 of `bytes` (kinds 1-3), or as the range bytes[0] .. bytes[0] +
   bytes[1] (kind 4), both complemented where `invert` is set; kind 0 is the
   empty set, or with `invert` every byte. Every set also has nibble tables,
   bit k of lo[j] standing for the byte k * 16 + j and bit k of hi[j] for the
   byte 128 + k * 16 + j, which are all that kind 5 has.

   Every x86-64 CPU has SSE2, so the comparisons test blocks of 16 bytes
   without any dispatch. The tables take SSSE3's pshufb, which the CPU is
   asked for as the runtime starts, and a search where either set is of kind
   5 tests both by their tables. Without SSSE3 such a search, and on other
   targets, under TinyCC (which has no intrinsics), or over fewer than 16
   positions, every search, tests one byte at a time, a single byte by
   memchr. No load reaches past the range: its last part is tested as the
   range's last whole block, with the positions before it, already tested,
   shifted out of the result. */

#pragma pack(push, 1)
typedef struct {
    uint8_t members[256];
    uint8_t kind, invert, bytes[3], lo[16], hi[16];
} gs_byteset;
#pragma pack(pop)

static int64_t gs_scan_any_bytes(const uint8_t *p, int64_t n, const gs_byteset *s) {
    if (s->kind == 1 && !s->invert) {
        const uint8_t *q = (const uint8_t *)memchr(p, s->bytes[0], (size_t)n);
        return q ? (int64_t)(q - p) : -1;
    }
    for (int64_t i = 0; i < n; i++)
        if (s->members[p[i]]) return i;
    return -1;
}

/* The first of the np positions i with p[i] in a and p[i + d] in b. */
static int64_t gs_scan_pair_bytes(const uint8_t *p, int64_t np, const gs_byteset *a, int64_t d,
                                  const gs_byteset *b) {
    for (int64_t i = 0; i < np; i++)
        if (a->members[p[i]] & b->members[p[i + d]]) return i;
    return -1;
}

#if !defined(__TINYC__) && (defined(__x86_64__) || defined(_M_X64))
#define GS_SCAN_SSE2 1
#include <emmintrin.h>
#include <tmmintrin.h>
#ifdef _WIN32
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#if defined(__GNUC__) || defined(__clang__)
#define GS_INLINE __attribute__((always_inline)) inline
/* clang and gcc compile pshufb only into a function that asks for it. */
#define GS_SSSE3 __attribute__((target("ssse3")))
#define gs_ctz32(x) __builtin_ctz(x)
#else
#define GS_INLINE __forceinline
#define GS_SSSE3
static int gs_ctz32(unsigned x) {
    unsigned long i;
    _BitScanForward(&i, x);
    return (int)i;
}
#endif

static int gs_cpu_ssse3;

static void gs_scan_init(void) {
#ifdef _WIN32
    int r[4];
    __cpuid(r, 1);
    gs_cpu_ssse3 = (r[2] >> 9) & 1;
#else
    unsigned a, b, c, d;
    gs_cpu_ssse3 = __get_cpuid(1, &a, &b, &c, &d) && ((c >> 9) & 1);
#endif
}

/* A set's block test, held in registers: the bytes or range it compares
   with and all ones where the result is complemented, or its tables. */
typedef struct { __m128i a, b, c, flip; } gs_bsm;

static GS_INLINE void gs_bsm_init(gs_bsm *m, const gs_byteset *s, int tables) {
    if (tables) {
        m->a = _mm_loadu_si128((const __m128i *)s->lo);
        m->b = _mm_loadu_si128((const __m128i *)s->hi);
        m->c = _mm_setr_epi8(1, 2, 4, 8, 16, 32, 64, -128, 1, 2, 4, 8, 16, 32, 64, -128);
        m->flip = _mm_setzero_si128();
    } else {
        m->a = _mm_set1_epi8((char)s->bytes[0]);
        m->b = _mm_set1_epi8((char)s->bytes[1]);
        m->c = _mm_set1_epi8((char)s->bytes[2]);
        m->flip = s->invert ? _mm_set1_epi8(-1) : _mm_setzero_si128();
    }
}

/* The members among the 16 bytes of x, as 0xff bytes, for a set of kind k
   from 1 to 4, a constant wherever this is inlined. A range holds x where
   x - lo, wrapping, is at most its width. */
static GS_INLINE __m128i gs_bsm_test(const gs_bsm *m, int k, __m128i x) {
    __m128i r, t;
    if (k == 1) {
        r = _mm_cmpeq_epi8(x, m->a);
    } else if (k == 2) {
        r = _mm_or_si128(_mm_cmpeq_epi8(x, m->a), _mm_cmpeq_epi8(x, m->b));
    } else if (k == 3) {
        r = _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(x, m->a), _mm_cmpeq_epi8(x, m->b)),
                         _mm_cmpeq_epi8(x, m->c));
    } else {
        t = _mm_sub_epi8(x, m->a);
        r = _mm_cmpeq_epi8(_mm_min_epu8(t, m->b), t);
    }
    return _mm_xor_si128(r, m->flip);
}

/* The same by the tables: pshufb looks up lo[x & 15] where x < 128 and
   hi[x & 15] where not (it gives 0 for an index with its top bit set), and
   the bit for x's bits 4 to 6, which the entry has or not. */
static GS_SSSE3 GS_INLINE __m128i gs_bsm_tables(const gs_bsm *m, __m128i x) {
    __m128i lo = _mm_shuffle_epi8(m->a, x);
    __m128i hi = _mm_shuffle_epi8(m->b, _mm_xor_si128(x, _mm_set1_epi8((char)0x80)));
    __m128i bit = _mm_shuffle_epi8(m->c, _mm_and_si128(_mm_srli_epi16(x, 4), _mm_set1_epi8(7)));
    return _mm_cmpeq_epi8(_mm_and_si128(_mm_or_si128(lo, hi), bit), bit);
}

#define GS_LOAD(q) _mm_loadu_si128((const __m128i *)(q))

/* The block loop of a search over n >= 16 positions for the bytes TEST
   finds in a block. */
#define GS_ANY_LOOP(TEST)                                                              \
    int64_t i = 0;                                                                     \
    unsigned k;                                                                        \
    for (; i + 16 <= n; i += 16) {                                                     \
        k = (unsigned)_mm_movemask_epi8(TEST(GS_LOAD(p + i)));                         \
        if (k) return i + gs_ctz32(k);                                                 \
    }                                                                                  \
    if (i == n) return -1;                                                             \
    k = (unsigned)_mm_movemask_epi8(TEST(GS_LOAD(p + n - 16))) >> (16 - (n - i));      \
    return k ? i + gs_ctz32(k) : -1;

/* The same over np >= 16 positions for two sets, d bytes apart. */
#define GS_PAIR_LOOP(TESTA, TESTB)                                                     \
    int64_t i = 0;                                                                     \
    unsigned k;                                                                        \
    for (; i + 16 <= np; i += 16) {                                                    \
        k = (unsigned)_mm_movemask_epi8(                                               \
            _mm_and_si128(TESTA(GS_LOAD(p + i)), TESTB(GS_LOAD(p + i + d))));          \
        if (k) return i + gs_ctz32(k);                                                 \
    }                                                                                  \
    if (i == np) return -1;                                                            \
    k = (unsigned)_mm_movemask_epi8(_mm_and_si128(TESTA(GS_LOAD(p + np - 16)),         \
                                                  TESTB(GS_LOAD(p + np - 16 + d))))    \
        >> (16 - (np - i));                                                            \
    return k ? i + gs_ctz32(k) : -1;

#define GS_TEST_S(x) gs_bsm_test(&ms, ks, x)
#define GS_TEST_A(x) gs_bsm_test(&ma, ka, x)
#define GS_TEST_B(x) gs_bsm_test(&mb, kb, x)
#define GS_TABLES_S(x) gs_bsm_tables(&ms, x)
#define GS_TABLES_A(x) gs_bsm_tables(&ma, x)
#define GS_TABLES_B(x) gs_bsm_tables(&mb, x)

static GS_INLINE int64_t gs_scan_any_k(const uint8_t *p, int64_t n, const gs_byteset *s, int ks) {
    gs_bsm ms;
    gs_bsm_init(&ms, s, 0);
    GS_ANY_LOOP(GS_TEST_S)
}

static GS_SSSE3 int64_t gs_scan_any_tables(const uint8_t *p, int64_t n, const gs_byteset *s) {
    gs_bsm ms;
    gs_bsm_init(&ms, s, 1);
    GS_ANY_LOOP(GS_TABLES_S)
}

static GS_INLINE int64_t gs_scan_pair_k(const uint8_t *p, int64_t np, const gs_byteset *a,
                                        int64_t d, const gs_byteset *b, int ka, int kb) {
    gs_bsm ma, mb;
    gs_bsm_init(&ma, a, 0);
    gs_bsm_init(&mb, b, 0);
    GS_PAIR_LOOP(GS_TEST_A, GS_TEST_B)
}

static GS_SSSE3 int64_t gs_scan_pair_tables(const uint8_t *p, int64_t np, const gs_byteset *a,
                                            int64_t d, const gs_byteset *b) {
    gs_bsm ma, mb;
    gs_bsm_init(&ma, a, 1);
    gs_bsm_init(&mb, b, 1);
    GS_PAIR_LOOP(GS_TABLES_A, GS_TABLES_B)
}

/* A loop of its own for each kind, and for each pair of kinds, from 1 to 4. */
static int64_t gs_scan_any_blocks(const uint8_t *p, int64_t n, const gs_byteset *s) {
    switch (s->kind) {
        case 1: return gs_scan_any_k(p, n, s, 1);
        case 2: return gs_scan_any_k(p, n, s, 2);
        case 3: return gs_scan_any_k(p, n, s, 3);
        default: return gs_scan_any_k(p, n, s, 4);
    }
}

#define GS_PAIR_CASE(ka, kb) \
    case (ka) * 4 + (kb): return gs_scan_pair_k(p, np, a, d, b, ka, kb);

static int64_t gs_scan_pair_blocks(const uint8_t *p, int64_t np, const gs_byteset *a, int64_t d,
                                   const gs_byteset *b) {
    switch (a->kind * 4 + b->kind) {
        GS_PAIR_CASE(1, 1) GS_PAIR_CASE(1, 2) GS_PAIR_CASE(1, 3) GS_PAIR_CASE(1, 4)
        GS_PAIR_CASE(2, 1) GS_PAIR_CASE(2, 2) GS_PAIR_CASE(2, 3) GS_PAIR_CASE(2, 4)
        GS_PAIR_CASE(3, 1) GS_PAIR_CASE(3, 2) GS_PAIR_CASE(3, 3) GS_PAIR_CASE(3, 4)
        GS_PAIR_CASE(4, 1) GS_PAIR_CASE(4, 2) GS_PAIR_CASE(4, 3)
        default: return gs_scan_pair_k(p, np, a, d, b, 4, 4);
    }
}

#else
static void gs_scan_init(void) {}
#endif

GS_API int64_t gs_scan_any(const uint8_t *p, int64_t n, const void *set) {
    const gs_byteset *s = (const gs_byteset *)set;
    if (n <= 0) return -1;
    if (s->kind == 0) return s->invert ? 0 : -1;
#ifdef GS_SCAN_SSE2
    if (n >= 16 && s->kind != 5) return gs_scan_any_blocks(p, n, s);
    if (n >= 16 && gs_cpu_ssse3) return gs_scan_any_tables(p, n, s);
#endif
    return gs_scan_any_bytes(p, n, s);
}

GS_API int64_t gs_scan_pair(const uint8_t *p, int64_t n, const void *a, int64_t d,
                            const void *b) {
    const gs_byteset *sa = (const gs_byteset *)a, *sb = (const gs_byteset *)b;
    if (d < 0 || d >= n) return -1;
    int64_t np = n - d;     /* the positions both bytes fit at */
    /* A set of every byte leaves a search for the other. */
    if (sa->kind == 0) return sa->invert ? gs_scan_any(p + d, np, sb) : -1;
    if (sb->kind == 0) return sb->invert ? gs_scan_any(p, np, sa) : -1;
#ifdef GS_SCAN_SSE2
    if (np >= 16 && sa->kind != 5 && sb->kind != 5) return gs_scan_pair_blocks(p, np, sa, d, sb);
    if (np >= 16 && gs_cpu_ssse3) return gs_scan_pair_tables(p, np, sa, d, sb);
#endif
    return gs_scan_pair_bytes(p, np, sa, d, sb);
}

GS_API void gs_rt_start(int argc, char **argv, uint64_t reserve, uint64_t gap,
                        uint64_t budget, int64_t mainregions, int64_t workerregions) {
    gs_argc = argc;
    gs_argv = argv;
    gs_region_usable = gs_region_reserve = (size_t)reserve;
    gs_region_gap = (size_t)gap;
    gs_stack_budget = budget;
    gs_mainregions = mainregions;
    gs_workerregions = workerregions;
    /* The workers the budget holds beside the main program, each at the
       most regions one can take: what hardware_threads() reports at most.
       At least one, so a program sized by it still runs; the regions the
       platform then refuses are retried smaller (gs_reserve_region). */
    if (workerregions > 0) {
        uint64_t per = reserve + gap;
        uint64_t total = per ? budget / per : 0;
        int64_t spare = total > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)total;
        spare -= mainregions;
        gs_thread_cap = spare > workerregions ? spare / workerregions : 1;
    }
    // Unbuffered stdout: output is never lost to an abort or a killed run,
    // and interleaves correctly with stderr diagnostics. Revisit if print
    // throughput ever matters.
    setvbuf(stdout, NULL, _IONBF, 0);
    gs_regions_init();
    gs_regions_begin(mainregions);
    gs_native_stack_init();
    gs_scan_init();
}

/* ---------------------------------------------------------------------------
   Text forms (§3.7). */

/* Integers are written by a digit loop rather than printf, whose locale
   handling and format parsing cost several times the conversion itself:
   the digit count first, then the digits from the right, two per division
   by 100. */
static const char gs_digit_pairs[201] =
    "00010203040506070809101112131415161718192021222324252627282930313233343536373839"
    "40414243444546474849505152535455565758596061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

static int gs_fmt_digits(uint64_t v) {
    int n = 1;
    for (;;) {
        if (v < 10) return n;
        if (v < 100) return n + 1;
        if (v < 1000) return n + 2;
        if (v < 10000) return n + 3;
        v /= 10000;
        n += 4;
    }
}

GS_API int64_t gs_fmt_u64(uint8_t *dst, uint64_t v) {
    int n = gs_fmt_digits(v);
    uint8_t *p = dst + n;
    while (v >= 100) {
        unsigned r = (unsigned)(v % 100);
        v /= 100;
        p -= 2;
        p[0] = (uint8_t)gs_digit_pairs[2 * r];
        p[1] = (uint8_t)gs_digit_pairs[2 * r + 1];
    }
    if (v >= 10) {
        p[-2] = (uint8_t)gs_digit_pairs[2 * v];
        p[-1] = (uint8_t)gs_digit_pairs[2 * v + 1];
    } else {
        p[-1] = (uint8_t)('0' + v);
    }
    return n;
}

GS_API int64_t gs_fmt_i64(uint8_t *dst, int64_t v) {
    if (v >= 0) return gs_fmt_u64(dst, (uint64_t)v);
    /* The magnitude in unsigned arithmetic, so i64.min needs no case. */
    dst[0] = '-';
    return 1 + gs_fmt_u64(dst + 1, 0 - (uint64_t)v);
}

/* A float's text is the fewest significant digits that read back as the
   same value of its own type and, of those, the ones nearest it, a tie going
   to the even digit (as Python's repr and Ryu choose). They are found in
   integers, by Burger and Dybvig's free-format algorithm, not through the C
   library: msvcrt, which tcc uses on Windows, rounds a printf tie away from
   zero, and below a power of two the gap to the next float is half the gap
   above, so the shortest text can lie above the value while the correctly
   rounded spelling of that length, below it, does not read back.

   The numbers are naturals in 32-bit limbs, least significant first. The
   denominator s stays below 2^1079 (ten times 2^1075, for the smallest
   doubles), and the others below eleven times s. */
typedef struct { int n; uint32_t d[36]; } gs_big;

/* a = v << sh, for v > 0. */
static void gs_big_set(gs_big *a, uint64_t v, int sh) {
    int w = sh >> 5, b = sh & 31, i;
    uint64_t lo = v << b, hi = b ? v >> (64 - b) : 0;
    for (i = 0; i < w; i++) a->d[i] = 0;
    a->d[w] = (uint32_t)lo;
    a->d[w + 1] = (uint32_t)(lo >> 32);
    a->d[w + 2] = (uint32_t)hi;
    a->n = w + 3;
    while (!a->d[a->n - 1]) a->n--;
}

static void gs_big_mul(gs_big *a, uint32_t m) {
    uint64_t c = 0;
    for (int i = 0; i < a->n; i++) {
        c += (uint64_t)a->d[i] * m;
        a->d[i] = (uint32_t)c;
        c >>= 32;
    }
    if (c) a->d[a->n++] = (uint32_t)c;
}

static void gs_big_mul_pow10(gs_big *a, int k) {
    for (; k >= 9; k -= 9) gs_big_mul(a, 1000000000u);
    for (; k > 0; k--) gs_big_mul(a, 10);
}

static int gs_big_cmp(const gs_big *a, const gs_big *b) {
    if (a->n != b->n) return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; i--)
        if (a->d[i] != b->d[i]) return a->d[i] < b->d[i] ? -1 : 1;
    return 0;
}

/* t = a + b */
static void gs_big_add(gs_big *t, const gs_big *a, const gs_big *b) {
    int n = a->n > b->n ? a->n : b->n;
    uint64_t c = 0;
    for (int i = 0; i < n; i++) {
        c += (uint64_t)(i < a->n ? a->d[i] : 0) + (i < b->n ? b->d[i] : 0);
        t->d[i] = (uint32_t)c;
        c >>= 32;
    }
    t->n = n;
    if (c) t->d[t->n++] = (uint32_t)c;
}

/* a -= b, for a >= b. */
static void gs_big_sub(gs_big *a, const gs_big *b) {
    uint64_t borrow = 0;
    for (int i = 0; i < a->n; i++) {
        uint64_t x = (uint64_t)a->d[i] - (i < b->n ? b->d[i] : 0) - borrow;
        a->d[i] = (uint32_t)x;
        borrow = x >> 63;
    }
    while (a->n && !a->d[a->n - 1]) a->n--;
}

/* The shortest digits of f * 2^e (f > 0) as a float of `bits` significant
   bits and least exponent emin, into dig; returns their count and sets *k
   so that the value they spell is 0.d1d2... * 10^*k. */
static int gs_float_digits(uint64_t f, int e, int bits, int emin, char *dig, int *k) {
    gs_big r, s, mp, mlo, t;
    /* Reading rounds a tie to the even significand, so the interval that
       reads back as f * 2^e includes its ends when f is even. */
    int even = !(f & 1);
    /* Below a power of two, the gap to the next float down is half the gap up. */
    int asym = f == (uint64_t)1 << (bits - 1) && e > emin;
    int up = e > 0 ? e : 0, lg = e - 1, n = 0, c;
    /* The value is r / s, and what reads back as it reaches from (r - *mm) / s
       to (r + mp) / s: half the gap to each neighbour. */
    gs_big *mm = asym ? &mlo : &mp;
    gs_big_set(&r, f, up + 1 + asym);
    gs_big_set(&s, 1, up - e + 1 + asym);
    gs_big_set(&mp, 1, up + asym);
    gs_big_set(&mlo, 1, up);
    /* Scale to 10^*k, the least power of ten above the interval: with
       lg = floor(log2 value), ceil(lg log10 2) is it or one short, and
       78913 / 2^18 is log10 2 closely enough to compute that for |lg| < 1650. */
    for (uint64_t g = f; g; g >>= 1) lg++;
    *k = lg > 0 ? ((lg * 78913) >> 18) + 1 : -((-lg * 78913) >> 18);
    if (*k >= 0) gs_big_mul_pow10(&s, *k);
    else {
        gs_big_mul_pow10(&r, -*k);
        gs_big_mul_pow10(&mp, -*k);
        if (asym) gs_big_mul_pow10(&mlo, -*k);
    }
    gs_big_add(&t, &r, &mp);
    c = gs_big_cmp(&t, &s);
    if (even ? c >= 0 : c > 0) {
        gs_big_mul(&s, 10);
        ++*k;
    }
    /* Each digit d leaves the remainder r: stop where the digits so far
       followed by d (r within *mm) or by d + 1 (r + mp past s) read back. */
    for (;;) {
        int d = 0, low, high;
        gs_big_mul(&r, 10);
        gs_big_mul(&mp, 10);
        if (asym) gs_big_mul(&mlo, 10);
        while (gs_big_cmp(&r, &s) >= 0) {
            gs_big_sub(&r, &s);
            d++;
        }
        c = gs_big_cmp(&r, mm);
        low = even ? c <= 0 : c < 0;
        gs_big_add(&t, &r, &mp);
        c = gs_big_cmp(&t, &s);
        high = even ? c >= 0 : c > 0;
        if (!low && !high) {
            dig[n++] = (char)('0' + d);
            continue;
        }
        if (low && high) {
            /* Both read back: the nearer, d + 1 when r is past half of s,
               and of two as near, the even one. */
            gs_big_add(&t, &r, &r);
            c = gs_big_cmp(&t, &s);
            high = c > 0 || (c == 0 && (d & 1));
        }
        dig[n++] = (char)('0' + d + high);
        return n;
    }
}

/* The text of a finite float from its fields: the sign, the significand
   without its implicit bit and the biased exponent, for a type of `bits`
   significant bits and least exponent emin. The digits are laid out as C's
   %g lays them out at a precision of max(15, digits): in exponent form, with
   at least two exponent digits, below 1e-4 or from that power of ten up. */
static int64_t gs_fmt_float(uint8_t *dst, int neg, uint64_t frac, int bexp, int bits,
                            int emin) {
    char dig[20], *p = (char *)dst;
    int k, n, x, i;
    if (neg) *p++ = '-';
    if (!frac && !bexp) {
        memcpy(p, "0.0", 3);
        return p + 3 - (char *)dst;
    }
    n = gs_float_digits(bexp ? frac | (uint64_t)1 << (bits - 1) : frac,
                        (bexp ? bexp - 1 : 0) + emin, bits, emin, dig, &k);
    x = k - 1;
    if (x < -4 || x >= (n > 15 ? n : 15)) {
        *p++ = dig[0];
        if (n > 1) *p++ = '.';
        for (i = 1; i < n; i++) *p++ = dig[i];
        *p++ = 'e';
        *p++ = x < 0 ? '-' : '+';
        if (x < 0) x = -x;
        if (x >= 100) *p++ = (char)('0' + x / 100);
        *p++ = (char)('0' + x / 10 % 10);
        *p++ = (char)('0' + x % 10);
    } else if (x < 0) {
        *p++ = '0';
        *p++ = '.';
        for (i = -1; i > x; i--) *p++ = '0';
        for (i = 0; i < n; i++) *p++ = dig[i];
    } else {
        for (i = 0; i < n || i <= x; i++) {
            if (i == x + 1) *p++ = '.';
            *p++ = i < n ? dig[i] : '0';
        }
        /* A whole number still reads as a float: 1.0, not 1. */
        if (n <= x + 1) {
            *p++ = '.';
            *p++ = '0';
        }
    }
    return p - (char *)dst;
}

GS_API int64_t gs_fmt_f64(uint8_t *dst, double v) {
    uint64_t b;
    /* C libraries disagree here (msvcrt, which tcc uses on Windows, writes
       1.#INF and -1.#IND; others give a NaN's sign bit, which depends on
       the CPU that made it), so these are spelled by the runtime. */
    if (v != v) { memcpy(dst, "nan", 3); return 3; }
    if (isinf(v)) {
        if (v > 0) { memcpy(dst, "inf", 3); return 3; }
        memcpy(dst, "-inf", 4);
        return 4;
    }
    memcpy(&b, &v, sizeof b);
    return gs_fmt_float(dst, (int)(b >> 63), b & (((uint64_t)1 << 52) - 1),
                        (int)(b >> 52) & 0x7FF, 53, -1074);
}

/* The digits are the f32's own, laid out as an f64's are, so 0.1 as an f32
   prints as 0.1, not as the digits of the f64 it widens to. */
GS_API int64_t gs_fmt_f32(uint8_t *dst, float v) {
    double d = v;
    uint32_t b;
    if (d != d || isinf(d)) return gs_fmt_f64(dst, d);
    memcpy(&b, &v, sizeof b);
    return gs_fmt_float(dst, (int)(b >> 31), b & 0x7FFFFF, (int)(b >> 23) & 0xFF, 24, -149);
}

GS_API int64_t gs_fmt_bool(uint8_t *dst, int64_t v) {
    memcpy(dst, v ? "true" : "false", v ? 4 : 5);
    return v ? 4 : 5;
}

GS_API int64_t gs_fmt_quoted(uint8_t *dst, const uint8_t *s, int64_t n) {
    uint8_t *d = dst;
    *d++ = '"';
    for (int64_t i = 0; i < n; i++) {
        uint8_t c = s[i];
        switch (c) {
            case '"': *d++ = '\\'; *d++ = '"'; break;
            case '\\': *d++ = '\\'; *d++ = '\\'; break;
            case '\n': *d++ = '\\'; *d++ = 'n'; break;
            case '\r': *d++ = '\\'; *d++ = 'r'; break;
            case '\t': *d++ = '\\'; *d++ = 't'; break;
            default: *d++ = c; break;
        }
    }
    *d++ = '"';
    return (int64_t)(d - dst);
}

/* stdout is unbuffered (gs_rt_start), so every piece is its own write;
   revisit if print throughput ever matters. */

GS_API void gs_out_int(int64_t v) {
    uint8_t buf[GS_FMT_MAX];
    fwrite(buf, 1, (size_t)gs_fmt_i64(buf, v), stdout);
}
GS_API void gs_out_uint(uint64_t v) {
    uint8_t buf[GS_FMT_MAX];
    fwrite(buf, 1, (size_t)gs_fmt_u64(buf, v), stdout);
}
GS_API void gs_out_flt(double v) {
    uint8_t buf[GS_FMT_MAX];
    fwrite(buf, 1, (size_t)gs_fmt_f64(buf, v), stdout);
}
GS_API void gs_out_f32(float v) {
    uint8_t buf[GS_FMT_MAX];
    fwrite(buf, 1, (size_t)gs_fmt_f32(buf, v), stdout);
}
GS_API void gs_out_bool(int64_t v) { fputs(v ? "true" : "false", stdout); }
GS_API void gs_out_bytes(const uint8_t *p, int64_t len) { fwrite(p, 1, (size_t)len, stdout); }
GS_API void gs_out_nl(void) { fputc('\n', stdout); }
