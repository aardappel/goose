/* Goose runtime — what runtime.h declares and leaves to the runtime: aborts,
   the data stack regions and the faults that reach them, text forms and
   printing. Follows runtime.h, in a standalone program's unit or in the
   runtime object (runtime.h has the two); this, runtime_threads.h and
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
}

/* ---------------------------------------------------------------------------
   Text forms (§3.7). */

GS_API int64_t gs_fmt_i64(uint8_t *dst, int64_t v) {
    return (int64_t)snprintf((char *)dst, GS_FMT_MAX, "%lld", (long long)v);
}

GS_API int64_t gs_fmt_u64(uint8_t *dst, uint64_t v) {
    return (int64_t)snprintf((char *)dst, GS_FMT_MAX, "%llu", (unsigned long long)v);
}

/* C99 asks for at least two exponent digits; the older Microsoft C runtime
   (which is what tcc links against on Windows) always writes three. Trim the
   padding, so a float's text form is the language's and not the backend's. */
static int gs_fmt_exp(char *s, int n) {
    char *e = (char *)memchr(s, 'e', (size_t)n);
    if (!e) return n;
    char *d = e + 2;                    /* past the 'e' and the exponent sign */
    char *p = d;
    int digits = n - (int)(d - s);
    while (digits > 2 && *p == '0') p++, digits--;
    if (p != d) {
        memmove(d, p, (size_t)digits);
        n = (int)(d - s) + digits;
        s[n] = 0;
    }
    return n;
}

GS_API int64_t gs_fmt_f64(uint8_t *dst, double v) {
    /* C libraries disagree here (msvcrt, which tcc uses on Windows, writes
       1.#INF and -1.#IND; others give a NaN's sign bit, which depends on
       the CPU that made it), so these are spelled by the runtime. */
    if (v != v) { memcpy(dst, "nan", 3); return 3; }
    if (isinf(v)) {
        if (v > 0) { memcpy(dst, "inf", 3); return 3; }
        memcpy(dst, "-inf", 4);
        return 4;
    }
    int n = snprintf((char *)dst, GS_FMT_MAX, "%.15g", v);
    if (strtod((char *)dst, NULL) != v) n = snprintf((char *)dst, GS_FMT_MAX, "%.17g", v);
    n = gs_fmt_exp((char *)dst, n);
    /* A whole number still reads as a float: 1.0, not 1. */
    if (!memchr(dst, '.', (size_t)n) && !memchr(dst, 'e', (size_t)n)) {
        dst[n++] = '.';
        dst[n++] = '0';
    }
    return n;
}

/* The fewest significant digits that read back as the same f32, laid out as
   the text of the f64 nearest them, so both types share one style. Above
   the subnormals, %.6g already gives any shorter form that reads back. The
   test reads through strtod rather than strtof: tcc's strtof on Windows is
   a rounded strtod, and one test keeps every backend's choice the same. */
GS_API int64_t gs_fmt_f32(uint8_t *dst, float v) {
    double d = v;
    if (d == d && !isinf(d)) {
        char buf[GS_FMT_MAX];
        for (int p = (v < 0 ? -v : v) < 1.17549435e-38f ? 1 : 6; p <= 9; p++) {
            snprintf(buf, sizeof(buf), "%.*g", p, d);
            double r = strtod(buf, NULL);
            if ((float)r == v) {
                d = r;
                break;
            }
        }
    }
    return gs_fmt_f64(dst, d);
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

GS_API void gs_out_int(int64_t v) { printf("%lld", (long long)v); }
GS_API void gs_out_uint(uint64_t v) { printf("%llu", (unsigned long long)v); }
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
