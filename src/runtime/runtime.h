/* Goose runtime — prepended verbatim to every compiler-generated C file.
   Plain C99 (+ #pragma pack, unaligned scalar access); compiles with MSVC,
   gcc, clang, and tcc. Kept deliberately small: per-operation behavior (push,
   indexing, field access) is emitted inline by the compiler; only genuinely
   shared machinery lives here (data stacks, varints, printing, aborts,
   threads/queues in runtime_threads.h). */

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef GS_NEED_THREADS
#define GS_NEED_THREADS 0
#endif

/* Configuration; all overridable from the compile command line. */
#ifndef GS_MAX_STACKS
#define GS_MAX_STACKS 1024          /* Data stacks per thread program. */
#endif
#ifndef GS_STACK_RESERVE
#define GS_STACK_RESERVE (256ull << 20)  /* Address space reserved per stack. */
#endif
#ifndef GS_STACK_GAP
#define GS_STACK_GAP (1ull << 20)   /* Unmapped tail so runaway growth aborts. */
#endif

#ifdef _MSC_VER
#define GS_NORETURN __declspec(noreturn)
#else
#define GS_NORETURN __attribute__((noreturn))
#endif

#if GS_NEED_THREADS
#ifdef _MSC_VER
#define GS_TLS __declspec(thread)
#else
#define GS_TLS __thread
#endif
#else
#define GS_TLS
#endif

/* ---------------------------------------------------------------------------
   Aborts (§9.3). Not catchable; message + nonzero exit. The int64_t returns
   let the checks sit inside expressions. */

static GS_NORETURN void gs_abort(const char *msg, const char *loc) {
    fprintf(stderr, "goose runtime error: %s (%s)\n", msg, loc);
    exit(1);
}

static int64_t gs_idxfail(int64_t i, int64_t n, const char *loc) {
    fprintf(stderr, "goose runtime error: index %lld out of bounds (length %lld) (%s)\n",
            (long long)i, (long long)n, loc);
    exit(1);
}

/* Bounds check as one unsigned compare; the index operand must be side-effect
   free (the compiler guarantees this at emission). */
#define GS_IDX(i, n, loc) ((uint64_t)(i) < (uint64_t)(n) ? (i) : gs_idxfail((int64_t)(i), (n), (loc)))

/* ---------------------------------------------------------------------------
   Integer semantics (§6.2): wrapping two's-complement arithmetic (computed
   unsigned so it is defined in C), masked shifts, always-checked division.
   Compiling the generated C with -DGS_DEBUG=1 turns on the debug-build
   aborts: integer overflow and `as` range violations (§9.3). */

#ifndef GS_DEBUG
#define GS_DEBUG 0
#endif

#if GS_DEBUG

static int64_t gs_addchk(int64_t a, int64_t b) {
    int64_t r = (int64_t)((uint64_t)a + (uint64_t)b);
    if (((a ^ r) & (b ^ r)) < 0) gs_abort("integer overflow", "debug");
    return r;
}
static int64_t gs_subchk(int64_t a, int64_t b) {
    int64_t r = (int64_t)((uint64_t)a - (uint64_t)b);
    if (((a ^ b) & (a ^ r)) < 0) gs_abort("integer overflow", "debug");
    return r;
}
static int64_t gs_mulchk(int64_t a, int64_t b) {
    int64_t r = (int64_t)((uint64_t)a * (uint64_t)b);
    if (a && b) {
        if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN) || r / b != a)
            gs_abort("integer overflow", "debug");
    }
    return r;
}
static int64_t gs_negchk(int64_t a) {
    if (a == INT64_MIN) gs_abort("integer overflow", "debug");
    return -a;
}
#define GS_ADD(a, b) gs_addchk((a), (b))
#define GS_SUB(a, b) gs_subchk((a), (b))
#define GS_MUL(a, b) gs_mulchk((a), (b))
#define GS_NEG(a)    gs_negchk(a)

/* `as` range checks: abort whenever the conversion would change the value. */
static int64_t gs_rangechk(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo || v > hi) gs_abort("as conversion out of range", "debug");
    return v;
}
static int64_t gs_f2ichk(double d) {
    if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
        gs_abort("as conversion out of range", "debug");
    int64_t v = (int64_t)d;
    if ((double)v != d) gs_abort("as conversion changes the value", "debug");
    return v;
}
static double gs_i2fchk(int64_t v) {
    double d = (double)v;
    if ((int64_t)d != v || d >= 9223372036854775808.0)
        gs_abort("as conversion changes the value", "debug");
    return d;
}
static float gs_f2f32chk(double d) {
    float f = (float)d;
    if ((double)f != d) gs_abort("as conversion changes the value", "debug");
    return f;
}
#define GS_RANGE(v, lo, hi) gs_rangechk((v), (lo), (hi))
#define GS_F2I(d)   gs_f2ichk(d)
#define GS_I2F(v)   gs_i2fchk(v)
#define GS_F2F32(d) gs_f2f32chk(d)

#else

#define GS_ADD(a, b) ((int64_t)((uint64_t)(a) + (uint64_t)(b)))
#define GS_SUB(a, b) ((int64_t)((uint64_t)(a) - (uint64_t)(b)))
#define GS_MUL(a, b) ((int64_t)((uint64_t)(a) * (uint64_t)(b)))
#define GS_NEG(a)    ((int64_t)(0u - (uint64_t)(a)))
#define GS_RANGE(v, lo, hi) (v)
#define GS_F2I(d)   gs_f2iwrap(d)   /* Deterministic truncation in release too. */
#define GS_I2F(v)   ((double)(v))
#define GS_F2F32(d) ((float)(d))

#endif

#define GS_SHL(a, b) ((int64_t)((uint64_t)(a) << ((b) & 63)))
#define GS_SHR(a, b) ((a) >> ((b) & 63))

static int64_t gs_idiv(int64_t a, int64_t b, const char *loc) {
    if (b == 0) gs_abort("division by zero", loc);
    if (a == INT64_MIN && b == -1) gs_abort("integer overflow in division", loc);
    return a / b;
}

static int64_t gs_imod(int64_t a, int64_t b, const char *loc) {
    if (b == 0) gs_abort("modulo by zero", loc);
    if (a == INT64_MIN && b == -1) gs_abort("integer overflow in modulo", loc);
    return a % b;
}

/* `as!` float-to-int: truncate toward zero, wrap modulo 2^64 (§6.3). Defined
   the same on every platform, unlike a raw C cast of an out-of-range value. */
static int64_t gs_f2iwrap(double d) {
    if (d != d) return 0;
    d = trunc(d);
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) return (int64_t)d;
    d = fmod(d, 18446744073709551616.0);
    if (d < 0) d += 18446744073709551616.0;
    return (int64_t)(uint64_t)d;
}

static int64_t gs_udiv(int64_t a, int64_t b, const char *loc) {
    if (b == 0) gs_abort("division by zero", loc);
    return (int64_t)((uint64_t)a / (uint64_t)b);
}

static int64_t gs_umod(int64_t a, int64_t b, const char *loc) {
    if (b == 0) gs_abort("modulo by zero", loc);
    return (int64_t)((uint64_t)a % (uint64_t)b);
}

/* ---------------------------------------------------------------------------
   Data stacks (§1.2, Appendix C.1/C.4): large reserved regions, committed on
   use, bump-pointer allocation, watermark restore on scope exit. Emitted code
   holds them per thread program via gs_stks (index = the hidden gs_sp
   argument plus a per-function constant); globals own dedicated stacks. */

typedef struct {
    uint8_t *top;
    uint8_t *base;
} gs_stack;

/* Every reserved region, so the Windows fault handler can tell "commit more"
   from a genuine crash, and so overruns into the gap abort with a message. */
typedef struct { uint8_t *base; size_t size; } gs_region;
static gs_region gs_regions[GS_MAX_STACKS * 4];
static volatile long gs_nregions = 0;

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

static size_t gs_page_size = 0;
#define GS_COMMIT_CHUNK (1u << 20)

static LONG WINAPI gs_fault_filter(EXCEPTION_POINTERS *ep) {
    if (ep->ExceptionRecord->ExceptionCode != STATUS_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    uint8_t *hit = (uint8_t *)ep->ExceptionRecord->ExceptionInformation[1];
    for (long i = 0; i < gs_nregions; i++) {
        gs_region r = gs_regions[i];
        if (hit >= r.base && hit < r.base + r.size) {
            /* Within the usable part: commit another chunk (clamped to the
               region) and resume. Within the gap: a data stack overran. */
            if (hit < r.base + r.size - GS_STACK_GAP) {
                uint8_t *page = (uint8_t *)((size_t)hit & ~(gs_page_size - 1));
                size_t n = GS_COMMIT_CHUNK;
                if (page + n > r.base + r.size - GS_STACK_GAP)
                    n = (size_t)(r.base + r.size - GS_STACK_GAP - page);
                if (VirtualAlloc(page, n, MEM_COMMIT, PAGE_READWRITE))
                    return EXCEPTION_CONTINUE_EXECUTION;
            }
            fputs("goose runtime error: data stack overflow\n", stderr);
            ExitProcess(1);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static uint8_t *gs_reserve_region(size_t size) {
    if (!gs_page_size) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        gs_page_size = si.dwPageSize;
        AddVectoredExceptionHandler(1, gs_fault_filter);
    }
    uint8_t *p = (uint8_t *)VirtualAlloc(0, size, MEM_RESERVE, PAGE_READWRITE);
    if (!p) gs_abort("cannot reserve data stack address space", "startup");
    long i = InterlockedIncrement(&gs_nregions) - 1;
    gs_regions[i].base = p;
    gs_regions[i].size = size;
    return p;
}

#else  /* posix */

#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>

static void gs_fault_handler(int sig, siginfo_t *info, void *ctx) {
    (void)sig; (void)ctx;
    uint8_t *hit = (uint8_t *)info->si_addr;
    for (long i = 0; i < gs_nregions; i++) {
        gs_region r = gs_regions[i];
        if (hit >= r.base && hit < r.base + r.size) {
            static const char msg[] = "goose runtime error: data stack overflow\n";
            ssize_t w = write(2, msg, sizeof(msg) - 1);
            (void)w;
            _exit(1);
        }
    }
    signal(SIGSEGV, SIG_DFL);  /* Not ours: recrash with default handling. */
}

static uint8_t *gs_reserve_region(size_t size) {
    static int handler_installed = 0;
    if (!handler_installed) {
        handler_installed = 1;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = gs_fault_handler;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, NULL);
#ifdef SIGBUS
        sigaction(SIGBUS, &sa, NULL);
#endif
    }
    /* Commit-on-touch via overcommit; the gap at the end stays PROT_NONE. */
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS
#ifdef MAP_NORESERVE
                   | MAP_NORESERVE
#endif
                   , -1, 0);
    if (p == MAP_FAILED) gs_abort("cannot reserve data stack address space", "startup");
    mprotect((uint8_t *)p + size - GS_STACK_GAP, GS_STACK_GAP, PROT_NONE);
    long i = __sync_fetch_and_add(&gs_nregions, 1);
    gs_regions[i].base = (uint8_t *)p;
    gs_regions[i].size = size;
    return (uint8_t *)p;
}

#endif

/* The current thread program's stack block. gs_sp-relative indices resolve
   through this; stacks materialize lazily as call depth first reaches them. */
static GS_TLS gs_stack *gs_stks;
static GS_TLS int64_t gs_nstks;

#define GS(i) (&gs_stks[i])

static void gs_stks_grow(int64_t n) {
    if (n > GS_MAX_STACKS) gs_abort("too many data stacks (deep call nesting?)", "runtime");
    while (gs_nstks < n) {
        gs_stack *s = &gs_stks[gs_nstks++];
        s->base = s->top = gs_reserve_region((size_t)GS_STACK_RESERVE + (size_t)GS_STACK_GAP);
    }
}

#define GS_ENSURE(n) do { if ((n) > gs_nstks) gs_stks_grow(n); } while (0)

static gs_stack *gs_new_stack_block(void) {
    gs_stack *b = (gs_stack *)calloc(GS_MAX_STACKS, sizeof(gs_stack));
    if (!b) gs_abort("out of memory allocating stack block", "runtime");
    return b;
}

static void gs_stack_init(gs_stack *s) {
    s->base = s->top = gs_reserve_region((size_t)GS_STACK_RESERVE + (size_t)GS_STACK_GAP);
}

static void gs_rt_init(void) {
    // Unbuffered stdout: output is never lost to an abort or a killed run,
    // and interleaves correctly with stderr diagnostics. Revisit if print
    // throughput ever matters.
    setvbuf(stdout, NULL, _IONBF, 0);
    gs_stks = gs_new_stack_block();
    gs_nstks = 0;
}

/* ---------------------------------------------------------------------------
   varint (§3.6): ULEB128; struct/payload/offset values additionally zigzag. */

static int64_t gs_uleb_read(const uint8_t *p) {
    uint64_t v = 0;
    int shift = 0;
    for (;;) {
        uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) return (int64_t)v;
        shift += 7;
    }
}

static int64_t gs_uleb_size(const uint8_t *p) {
    const uint8_t *q = p;
    while (*q & 0x80) q++;
    return (int64_t)(q - p) + 1;
}

static int64_t gs_uleb_write(uint8_t *p, uint64_t v) {
    uint8_t *q = p;
    for (;;) {
        uint8_t b = v & 0x7f;
        v >>= 7;
        if (v) *q++ = b | 0x80; else { *q++ = b; break; }
    }
    return (int64_t)(q - p);
}

static int64_t gs_zig_read(const uint8_t *p) {
    uint64_t u = (uint64_t)gs_uleb_read(p);
    return (int64_t)((u >> 1) ^ (0u - (u & 1)));
}

static int64_t gs_zig_write(uint8_t *p, int64_t v) {
    return gs_uleb_write(p, ((uint64_t)v << 1) ^ (uint64_t)(v >> 63));
}

/* ---------------------------------------------------------------------------
   print (§12). One value per call, newline-terminated. */

static void gs_print_int(int64_t v) { printf("%lld\n", (long long)v); }

static void gs_print_flt(double v) {
    /* Shortest form that still round-trips. */
    char buf[40];
    snprintf(buf, sizeof(buf), "%.15g", v);
    if (strtod(buf, NULL) != v) snprintf(buf, sizeof(buf), "%.17g", v);
    printf("%s\n", buf);
}

static void gs_print_bool(int64_t v) { fputs(v ? "true\n" : "false\n", stdout); }

static void gs_print_bytes(const uint8_t *p, int64_t len) {
    fwrite(p, 1, (size_t)len, stdout);
    fputc('\n', stdout);
}
