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
#ifndef GS_DEBUG
#define GS_DEBUG 0          /* 1: overflow, `as` range, and tag checks (§9.3). */
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
/* §10.4 caps a stack reservation at 2^48 bytes, which is what lets the
   compiler treat every size, count and index as fitting in 48 bits: the
   guard region enforces it, so no growth path needs its own check. */
#if GS_STACK_RESERVE > (1ull << 48)
#error "GS_STACK_RESERVE exceeds the 2^48 limit (goose_spec.md 10.4)"
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
   Aborts (§9.3). Not catchable; message + nonzero exit. Compiler-emitted
   checks carry an error id plus a file (a static string in the generated
   code) and line; runtime-internal failures use gs_panic. The int64_t
   returns let checks sit inside expressions. */

enum {
    GS_E_CAPACITY,     /* limited array capacity exceeded */
    GS_E_SLICE,        /* slice bounds out of range */
    GS_E_RELOFF,       /* relative reference offset overflow */
    GS_E_ASSERT,       /* assert failed */
    GS_E_CAPRANGE,     /* invalid capacity */
    GS_E_RESIZEFILL,   /* resize growth requires a fill value */
    GS_E_RESIZENEG,    /* resize to a negative length */
    GS_E_POP,          /* pop on empty array */
    GS_E_THREADID,     /* thread_wait on an unknown thread id */
    GS_E_TAG,          /* corrupt ADT tag (debug builds only) */
};

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
    "corrupt ADT tag",
};

static GS_NORETURN void gs_panic(const char *msg) {
    fprintf(stderr, "goose runtime error: %s\n", msg);
    exit(1);
}

static GS_NORETURN void gs_abort(int err, const char *file, int line) {
    fprintf(stderr, "goose runtime error: %s (%s:%d)\n", gs_errmsgs[err], file, line);
    exit(1);
}

static int64_t gs_idxfail(int64_t i, int64_t n, const char *file, int line) {
    fprintf(stderr, "goose runtime error: index %lld out of bounds (length %lld) "
            "(%s:%d)\n", (long long)i, (long long)n, file, line);
    exit(1);
}

/* Bounds check as one unsigned compare; the index operand must be side-effect
   free (the compiler guarantees this at emission). */
#define GS_IDX(i, n, f, l) \
    ((uint64_t)(i) < (uint64_t)(n) ? (i) : gs_idxfail((int64_t)(i), (n), (f), (l)))

/* Statically unreachable spots (e.g. an ADT tag no variant matches): checked
   in debug builds, an optimizer hint in release. */
#if GS_DEBUG
#define GS_UNREACHABLE(f, l) gs_abort(GS_E_TAG, (f), (l))
#elif defined(_MSC_VER)
#define GS_UNREACHABLE(f, l) __assume(0)
#elif defined(__GNUC__)
#define GS_UNREACHABLE(f, l) __builtin_unreachable()
#else
#define GS_UNREACHABLE(f, l) ((void)0)
#endif

/* ---------------------------------------------------------------------------
   Integer semantics (§6.2): every operation runs at its operands' type. The
   operations compute wide (so wrap is defined in C), truncate back, and — when
   the generated C is compiled with -DGS_DEBUG=1 — abort when the wide result
   does not fit the type. Shifts mask their count to the width; division is
   zero-checked always.

   Only division and modulo check anything in a release build, so only they
   need to be functions there; everything else is a macro whose body is the
   expression the release function would have returned. An optimizing backend
   inlines either form to the same instruction, but a backend that does not
   inline (libtcc, or any -O0 build) would otherwise pay a call for every
   arithmetic operation in the program — which measured as 13-37% of total
   runtime across the benchmarks. */

#if GS_DEBUG
static void gs_ovf(void) { gs_panic("integer overflow (debug)"); }
#endif

static GS_NORETURN void gs_divfail(const char *file, int line) {
    fprintf(stderr, "goose runtime error: division by zero (%s:%d)\n", file, line);
    exit(1);
}

/* Division and modulo, both builds: the zero check is not optional, and `%`
   is Euclidean. */
#define GS_DIVOPS_S(SFX, T, MIN, MAX) \
static T gs_div_##SFX(T a, T b, const char *file, int line) { \
    if (b == 0) gs_divfail(file, line); \
    int64_t r = (int64_t)a / (int64_t)b; \
    if (GS_DEBUG && (r < MIN || r > MAX)) gs_ovf(); \
    return (T)r; } \
static T gs_mod_##SFX(T a, T b, const char *file, int line) { \
    if (b == 0) gs_divfail(file, line); \
    int64_t r = (int64_t)a % (int64_t)b; \
    if (r < 0) r += (int64_t)b < 0 ? -(int64_t)b : (int64_t)b; \
    return (T)r; }

#define GS_DIVOPS_U(SFX, T) \
static T gs_div_##SFX(T a, T b, const char *file, int line) { \
    if (b == 0) gs_divfail(file, line); \
    return (T)(a / b); } \
static T gs_mod_##SFX(T a, T b, const char *file, int line) { \
    if (b == 0) gs_divfail(file, line); \
    return (T)(a % b); }

GS_DIVOPS_S(i8, int8_t, -128, 127)
GS_DIVOPS_S(i16, int16_t, -32768, 32767)
GS_DIVOPS_S(i32, int32_t, INT32_MIN, INT32_MAX)
GS_DIVOPS_U(u8, uint8_t)
GS_DIVOPS_U(u16, uint16_t)
GS_DIVOPS_U(u32, uint32_t)

#if GS_DEBUG

/* Signed narrow types (8/16/32 bits): 64-bit signed math covers every
   intermediate result. */
#define GS_INTOPS_S(SFX, T, MIN, MAX, BITS) \
static T gs_add_##SFX(T a, T b) { \
    int64_t r = (int64_t)a + (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(); \
    return (T)r; } \
static T gs_sub_##SFX(T a, T b) { \
    int64_t r = (int64_t)a - (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(); \
    return (T)r; } \
static T gs_mul_##SFX(T a, T b) { \
    int64_t r = (int64_t)a * (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(); \
    return (T)r; } \
static T gs_neg_##SFX(T a) { \
    int64_t r = -(int64_t)a; \
    if (r < MIN || r > MAX) gs_ovf(); \
    return (T)r; } \
static T gs_shl_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a << (n & (BITS - 1))); } \
static T gs_shr_##SFX(T a, int64_t n) { \
    return (T)((int64_t)a >> (n & (BITS - 1))); }

/* Unsigned types wrap modulo 2^width by definition (§6.2) in every build:
   plain C unsigned arithmetic, truncated back to the width. */
#define GS_INTOPS_U(SFX, T, MAX, BITS) \
static T gs_add_##SFX(T a, T b) { return (T)(a + b); } \
static T gs_sub_##SFX(T a, T b) { return (T)(a - b); } \
static T gs_mul_##SFX(T a, T b) { return (T)((uint64_t)a * (uint64_t)b); } \
static T gs_shl_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a << (n & (BITS - 1))); } \
static T gs_shr_##SFX(T a, int64_t n) { \
    return (T)((uint64_t)a >> (n & (BITS - 1))); }

GS_INTOPS_S(i8,  int8_t,  -128, 127, 8)
GS_INTOPS_S(i16, int16_t, -32768, 32767, 16)
GS_INTOPS_S(i32, int32_t, INT32_MIN, INT32_MAX, 32)
GS_INTOPS_U(u8,  uint8_t,  255u, 8)
GS_INTOPS_U(u16, uint16_t, 65535u, 16)
GS_INTOPS_U(u32, uint32_t, 4294967295u, 32)

/* The 64-bit types detect overflow on the value itself. */
static int64_t gs_add_i64(int64_t a, int64_t b) {
    int64_t r = (int64_t)((uint64_t)a + (uint64_t)b);
    if (((a ^ r) & (b ^ r)) < 0) gs_ovf();
    return r;
}
static int64_t gs_sub_i64(int64_t a, int64_t b) {
    int64_t r = (int64_t)((uint64_t)a - (uint64_t)b);
    if (((a ^ b) & (a ^ r)) < 0) gs_ovf();
    return r;
}
static int64_t gs_mul_i64(int64_t a, int64_t b) {
    int64_t r = (int64_t)((uint64_t)a * (uint64_t)b);
    if (a && b &&
        ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN) || r / b != a))
        gs_ovf();
    return r;
}
static int64_t gs_neg_i64(int64_t a) {
    if (a == INT64_MIN) gs_ovf();
    return (int64_t)(0u - (uint64_t)a);
}
static int64_t gs_shl_i64(int64_t a, int64_t n) {
    return (int64_t)((uint64_t)a << (n & 63));
}
static int64_t gs_shr_i64(int64_t a, int64_t n) { return a >> (n & 63); }
static uint64_t gs_add_u64(uint64_t a, uint64_t b) { return a + b; }
static uint64_t gs_sub_u64(uint64_t a, uint64_t b) { return a - b; }
static uint64_t gs_mul_u64(uint64_t a, uint64_t b) { return a * b; }
static uint64_t gs_shl_u64(uint64_t a, int64_t n) { return a << (n & 63); }
static uint64_t gs_shr_u64(uint64_t a, int64_t n) { return a >> (n & 63); }

#else  /* release: each operation is the expression the function returned */

#define gs_add_i8(a, b)  ((int8_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i8(a, b)  ((int8_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i8(a, b)  ((int8_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i8(a)     ((int8_t)(-(int64_t)(a)))
#define gs_shl_i8(a, n)  ((int8_t)((uint64_t)(a) << ((n) & 7)))
#define gs_shr_i8(a, n)  ((int8_t)((int64_t)(a) >> ((n) & 7)))

#define gs_add_i16(a, b) ((int16_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i16(a, b) ((int16_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i16(a, b) ((int16_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i16(a)    ((int16_t)(-(int64_t)(a)))
#define gs_shl_i16(a, n) ((int16_t)((uint64_t)(a) << ((n) & 15)))
#define gs_shr_i16(a, n) ((int16_t)((int64_t)(a) >> ((n) & 15)))

#define gs_add_i32(a, b) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i32(a, b) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i32(a, b) ((int32_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i32(a)    ((int32_t)(-(int64_t)(a)))
#define gs_shl_i32(a, n) ((int32_t)((uint64_t)(a) << ((n) & 31)))
#define gs_shr_i32(a, n) ((int32_t)((int64_t)(a) >> ((n) & 31)))

#define gs_add_u8(a, b)  ((uint8_t)((a) + (b)))
#define gs_sub_u8(a, b)  ((uint8_t)((a) - (b)))
#define gs_mul_u8(a, b)  ((uint8_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_shl_u8(a, n)  ((uint8_t)((uint64_t)(a) << ((n) & 7)))
#define gs_shr_u8(a, n)  ((uint8_t)((uint64_t)(a) >> ((n) & 7)))

#define gs_add_u16(a, b) ((uint16_t)((a) + (b)))
#define gs_sub_u16(a, b) ((uint16_t)((a) - (b)))
#define gs_mul_u16(a, b) ((uint16_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_shl_u16(a, n) ((uint16_t)((uint64_t)(a) << ((n) & 15)))
#define gs_shr_u16(a, n) ((uint16_t)((uint64_t)(a) >> ((n) & 15)))

#define gs_add_u32(a, b) ((uint32_t)((a) + (b)))
#define gs_sub_u32(a, b) ((uint32_t)((a) - (b)))
#define gs_mul_u32(a, b) ((uint32_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_shl_u32(a, n) ((uint32_t)((uint64_t)(a) << ((n) & 31)))
#define gs_shr_u32(a, n) ((uint32_t)((uint64_t)(a) >> ((n) & 31)))

#define gs_add_i64(a, b) ((int64_t)((uint64_t)(a) + (uint64_t)(b)))
#define gs_sub_i64(a, b) ((int64_t)((uint64_t)(a) - (uint64_t)(b)))
#define gs_mul_i64(a, b) ((int64_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_neg_i64(a)    ((int64_t)(0u - (uint64_t)(a)))
#define gs_shl_i64(a, n) ((int64_t)((uint64_t)(a) << ((n) & 63)))
#define gs_shr_i64(a, n) ((int64_t)((a) >> ((n) & 63)))

#define gs_add_u64(a, b) ((uint64_t)((a) + (b)))
#define gs_sub_u64(a, b) ((uint64_t)((a) - (b)))
#define gs_mul_u64(a, b) ((uint64_t)((a) * (b)))
#define gs_shl_u64(a, n) ((uint64_t)((a) << ((n) & 63)))
#define gs_shr_u64(a, n) ((uint64_t)((a) >> ((n) & 63)))

#endif  /* GS_DEBUG */

/* 64-bit division and modulo. Division overflow (i64.min / -1) would trap in
   hardware and aborts in every build. */
static int64_t gs_div_i64(int64_t a, int64_t b, const char *file, int line) {
    if (b == 0) gs_divfail(file, line);
    if (a == INT64_MIN && b == -1) {
        fprintf(stderr, "goose runtime error: integer overflow in division (%s:%d)\n",
                file, line);
        exit(1);
    }
    return a / b;
}
static int64_t gs_mod_i64(int64_t a, int64_t b, const char *file, int line) {
    if (b == 0) gs_divfail(file, line);
    if (b == -1) return 0;   /* Exactly 0, and i64.min % -1 would trap. */
    int64_t r = a % b;
    /* |b| unsigned, so a divisor of i64.min (unrepresentable negated) works. */
    if (r < 0) r = (int64_t)((uint64_t)r + (b < 0 ? 0u - (uint64_t)b : (uint64_t)b));
    return r;
}
static uint64_t gs_div_u64(uint64_t a, uint64_t b, const char *file, int line) {
    if (b == 0) gs_divfail(file, line);
    return a / b;
}
static uint64_t gs_mod_u64(uint64_t a, uint64_t b, const char *file, int line) {
    if (b == 0) gs_divfail(file, line);
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

/* `as` conversion checks (§6.3): abort in debug builds whenever the
   conversion would change the value; identity/plain casts in release. */
#if GS_DEBUG

static int64_t gs_rangechk(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo || v > hi) gs_panic("as conversion out of range (debug)");
    return v;
}
static uint64_t gs_rangechk_u(uint64_t v, uint64_t hi) {
    if (v > hi) gs_panic("as conversion out of range (debug)");
    return v;
}
static int64_t gs_f2ichk(double d) {
    if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
        gs_panic("as conversion out of range (debug)");
    int64_t v = (int64_t)d;
    if ((double)v != d) gs_panic("as conversion changes the value (debug)");
    return v;
}
static uint64_t gs_f2uchk(double d) {
    if (!(d >= 0 && d < 18446744073709551616.0))
        gs_panic("as conversion out of range (debug)");
    uint64_t v = (uint64_t)d;
    if ((double)v != d) gs_panic("as conversion changes the value (debug)");
    return v;
}
static double gs_i2fchk(int64_t v) {
    double d = (double)v;
    if ((int64_t)d != v || d >= 9223372036854775808.0)
        gs_panic("as conversion changes the value (debug)");
    return d;
}
static double gs_u2fchk(uint64_t v) {
    double d = (double)v;
    if (d >= 18446744073709551616.0 || (uint64_t)d != v)
        gs_panic("as conversion changes the value (debug)");
    return d;
}
static float gs_f2f32chk(double d) {
    float f = (float)d;
    if ((double)f != d) gs_panic("as conversion changes the value (debug)");
    return f;
}
#define GS_RANGE(v, lo, hi) gs_rangechk((v), (lo), (hi))
#define GS_RANGE_U(v, hi)   gs_rangechk_u((v), (hi))
#define GS_F2I(d)    gs_f2ichk(d)
#define GS_F2U(d)    gs_f2uchk(d)
#define GS_I2F(v)    gs_i2fchk(v)
#define GS_U2F(v)    gs_u2fchk(v)
#define GS_I2F32(v)  gs_f2f32chk(gs_i2fchk(v))
#define GS_U2F32(v)  gs_f2f32chk(gs_u2fchk(v))
#define GS_F2F32(d)  gs_f2f32chk(d)

#else

#define GS_RANGE(v, lo, hi) (v)
#define GS_RANGE_U(v, hi)   (v)
#define GS_F2I(d)    gs_f2iwrap(d)   /* Deterministic truncation in release too. */
#define GS_F2U(d)    ((uint64_t)gs_f2iwrap(d))
#define GS_I2F(v)    ((double)(v))
#define GS_U2F(v)    ((double)(uint64_t)(v))
#define GS_I2F32(v)  ((float)(v))
#define GS_U2F32(v)  ((float)(uint64_t)(v))
#define GS_F2F32(d)  ((float)(d))

#endif

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
    if (!p) gs_panic("cannot reserve data stack address space");
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
    if (p == MAP_FAILED) gs_panic("cannot reserve data stack address space");
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
    if (n > GS_MAX_STACKS) gs_panic("too many data stacks (deep call nesting?)");
    while (gs_nstks < n) {
        gs_stack *s = &gs_stks[gs_nstks++];
        s->base = s->top = gs_reserve_region((size_t)GS_STACK_RESERVE + (size_t)GS_STACK_GAP);
    }
}

#define GS_ENSURE(n) do { if ((n) > gs_nstks) gs_stks_grow(n); } while (0)

static gs_stack *gs_new_stack_block(void) {
    gs_stack *b = (gs_stack *)calloc(GS_MAX_STACKS, sizeof(gs_stack));
    if (!b) gs_panic("out of memory allocating stack block");
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

static void gs_print_uint(uint64_t v) { printf("%llu\n", (unsigned long long)v); }

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
