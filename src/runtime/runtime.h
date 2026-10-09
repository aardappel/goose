/* Goose runtime — the part every compiler-generated C file starts with.
   Plain C99 (+ #pragma pack, unaligned scalar access); compiles with MSVC,
   gcc, clang, and tcc. Kept deliberately small: per-operation behavior (push,
   indexing, field access) is emitted inline by the compiler; only genuinely
   shared machinery lives in the runtime (data stacks, varints, printing,
   aborts, threads/queues, byte search).

   This file holds what a program's own translation unit needs: types,
   macros, the configuration, the helpers that must inline (arithmetic,
   checks, varints, slice pool spans), the data stack state the emitted code
   reads, and declarations of the rest. That rest (runtime_impl.h,
   runtime_threads.h, and runtime_os.h after runtime_ext.h) needs the
   platform's headers, which no program's unit includes, and is built one
   of two ways:

   - Standalone (`goose --standalone`, and every JIT run): one translation
     unit holds it all, and everything the runtime defines is static.
   - Separate (`goose -o`'s default): the program's unit declares the rest
     extern (GS_SEPARATE_RUNTIME), and `goose --emit-runtime` writes it out
     as a C file of its own (GS_RUNTIME_OBJECT), compiled once and linked
     with every program. That object takes no configuration of its own: the
     program hands it the data stack sizes as it starts (gs_rt_init), it
     always supports threads, and it holds the checks of debug and release
     builds alike, so one object serves every program a compiler emits.
     GS_RUNTIME_VERSION names gs_rt_start after the runtime's text, so a
     program linked with another compiler's runtime fails to link. */

/* The OS layer (runtime_os.h) opens files on Windows with _wfopen, which the
   Microsoft CRT deprecates in favour of its own _s variant. */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS 1
#endif

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
/* The most data stacks the compiler lets one thread program use at once:
   a static count past this is a compile error. The runtime takes the
   counts from the program and never checks this itself. */
#ifndef GS_MAX_STACKS
#define GS_MAX_STACKS 1024
#endif
#ifndef GS_STACK_RESERVE
#define GS_STACK_RESERVE (2048ull << 20)  /* Address space reserved per stack. */
#endif
#ifndef GS_STACK_GAP
#define GS_STACK_GAP (1ull << 20)   /* Unmapped tail so runaway growth aborts. */
#endif
/* The address space the program means to spend on data stack regions over
   every thread program at once, which is what caps hardware_threads()
   (§11.2): the regions it holds, less the main program's, divided by a
   worker's. Not enforced at reservation; what the platform refuses is
   retried smaller (gs_reserve_region). */
#ifndef GS_STACK_BUDGET
#define GS_STACK_BUDGET (32ull << 40)
#endif
#ifndef GS_STACK_STATS
#define GS_STACK_STATS 0    /* 1: each thread program reports its stack use as it ends. */
#endif
/* §10.4 caps a stack reservation at 2^48 bytes, which is what lets the
   compiler treat every size, count and index as fitting in 48 bits: the
   guard region enforces it, so no growth path needs its own check. */
#if GS_STACK_RESERVE > (1ull << 48)
#error "GS_STACK_RESERVE exceeds the 2^48 limit (goose_spec.md 10.4)"
#endif

#ifdef _MSC_VER
#define GS_NORETURN __declspec(noreturn)
#define GS_NOINLINE __declspec(noinline)
#else
#define GS_NORETURN __attribute__((noreturn))
#define GS_NOINLINE __attribute__((noinline))
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

/* The linkage of everything the runtime defines outside this file. */
#ifdef GS_SEPARATE_RUNTIME
#define GS_API extern
#else
#define GS_API static
#endif

#define GS_CAT_(a, b) a##b
#define GS_CAT(a, b) GS_CAT_(a, b)
#ifdef GS_RUNTIME_VERSION
#define gs_rt_start GS_CAT(gs_rt_start_, GS_RUNTIME_VERSION)
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
    GS_E_THREADSELF,   /* a worker cannot wait for itself */
    GS_E_TAG,          /* corrupt ADT tag (debug builds only) */
    GS_E_ENDIAN,       /* serialization on a big-endian host */
    GS_E_SLICELEN,     /* slice pool length negative or beyond any data stack */
    GS_E_POOLSLICE,    /* a slice handed to a slice pool is not one of its runs */
    GS_E_RELNULL,      /* a non-null optional self-relative target has offset zero */
};

GS_API GS_NORETURN void gs_panic(const char *msg);
GS_API GS_NORETURN void gs_abort(int err, const char *file, int line);
/* The program's own abort(msg) (§9.3): the message is Goose bytes, not a C
   string, so it goes out with an explicit length. */
GS_API GS_NORETURN void gs_abort_msg(const uint8_t *msg, int64_t len, const char *file,
                                     int line);
GS_API GS_NORETURN void gs_exit(int64_t code);
GS_API GS_NORETURN void gs_idxfail(int64_t i, int64_t n, const char *file, int line);
GS_API GS_NORETURN void gs_divfail(const char *file, int line);
GS_API GS_NORETURN void gs_divovf(const char *file, int line);
/* The debug build's failing overflow and `as` checks. */
GS_API GS_NORETURN void gs_ovf(int64_t a, const char *op, int64_t b, const char *type,
                               const char *file, int line);
GS_API GS_NORETURN void gs_ovf_neg(int64_t a, const char *type, const char *file, int line);
GS_API GS_NORETURN void gs_asfail_i(const char *why, int64_t v, const char *type,
                                    const char *file, int line);
GS_API GS_NORETURN void gs_asfail_u(const char *why, uint64_t v, const char *type,
                                    const char *file, int line);
/* f32: d is an f32's value, and takes that type's text form. */
GS_API GS_NORETURN void gs_asfail_f(const char *why, double d, int f32, const char *type,
                                    const char *file, int line);

/* Bounds check as one unsigned compare; the index operand must be side-effect
   free (the compiler guarantees this at emission). The failing arm's int64_t
   gives the result the type of (i) and an int64_t together, and its call is
   one the C compiler knows does not return, also where gs_idxfail is in the
   runtime object. */
#define GS_IDX(i, n, f, l) \
    ((uint64_t)(i) < (uint64_t)(n) ? (i) \
                                   : (gs_idxfail((int64_t)(i), (n), (f), (l)), (int64_t)0))

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

/* simd functions (§7.12): the compiler writes each one's body once per
   instruction-set level, the baseline under the function's name, and the
   baseline calls the highest version the CPU supports. GS_SIMD is the
   highest level built: 0 just the baseline, 1 adds an AVX2 version (with
   BMI1, BMI2, LZCNT and POPCNT, which the check below asks for one by one),
   2 an AVX-512 one as well (F, BW, CD, DQ and VL: x86-64-v4). It needs clang
   on x86-64: its target attribute, its inline assembly for cpuid, and a
   pragma that keeps every version from contracting a multiply and an add
   into one fused rounding (AVX-512 implies FMA to clang), which would make
   a version's float results differ from the baseline's. Elsewhere the
   versions are left out, and so is the choice. -DGS_SIMD=0 or 1 lowers the
   level; nothing raises it. */
#if defined(__clang__) && (defined(__x86_64__) || defined(_M_X64)) && !defined(__TINYC__)
#ifndef GS_SIMD
#define GS_SIMD 2
#endif
#else
#undef GS_SIMD
#define GS_SIMD 0
#endif
#if GS_SIMD >= 1
#define GS_SIMD_TARGET1 __attribute__((target("avx2,bmi,bmi2,lzcnt,popcnt")))
#define GS_SIMD_TARGET2 \
    __attribute__((target("avx512f,avx512bw,avx512cd,avx512dq,avx512vl,avx2,bmi,bmi2,lzcnt,popcnt")))
#define GS_SIMD_EXACT _Pragma("clang fp contract(off)")

static inline void gs_cpuid(uint32_t leaf, uint32_t r[4]) {
    __asm__ volatile("cpuid" : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3]) : "a"(leaf), "c"(0));
}

/* The highest level both the CPU and the operating system support (the OS
   has to save the wider registers: XGETBV's XCR0), capped at GS_SIMD. */
static inline int gs_simd_detect(void) {
    uint32_t r[4], b7, xcr0, xhi;
    gs_cpuid(0, r);
    if (r[0] < 7) return 0;
    gs_cpuid(1, r);
    /* OSXSAVE, AVX, POPCNT. */
    if ((r[2] & (1u << 27 | 1u << 28 | 1u << 23)) != (1u << 27 | 1u << 28 | 1u << 23)) return 0;
    __asm__ volatile("xgetbv" : "=a"(xcr0), "=d"(xhi) : "c"(0));
    (void)xhi;
    if ((xcr0 & 0x6) != 0x6) return 0;                  /* XMM and YMM state. */
    gs_cpuid(7, r);
    b7 = r[1];
    if ((b7 & (1u << 5 | 1u << 3 | 1u << 8)) != (1u << 5 | 1u << 3 | 1u << 8))
        return 0;                                       /* AVX2, BMI1, BMI2. */
    gs_cpuid(0x80000000u, r);
    if (r[0] < 0x80000001u) return 0;
    gs_cpuid(0x80000001u, r);
    if (!(r[2] & (1u << 5))) return 0;                  /* LZCNT. */
    if (GS_SIMD < 2 || (xcr0 & 0xe0) != 0xe0) return 1; /* Opmask and ZMM state. */
    /* AVX512F, DQ, CD, BW, VL. */
    if ((b7 & (1u << 16 | 1u << 17 | 1u << 28 | 1u << 30 | 1u << 31)) !=
        (1u << 16 | 1u << 17 | 1u << 28 | 1u << 30 | 1u << 31))
        return 1;
    return 2;
}

/* Detected at the first call and kept. Every thread computes the same
   level, so a relaxed atomic is all a race between two first calls needs. */
static inline int gs_simd_level(void) {
    static int level = -1;
    int l = __atomic_load_n(&level, __ATOMIC_RELAXED);
    if (l < 0) {
        l = gs_simd_detect();
        __atomic_store_n(&level, l, __ATOMIC_RELAXED);
    }
    return l;
}
#else
#define GS_SIMD_EXACT
#endif

/* Unaligned loads of 4 and 8 bytes. The optimizing backends turn the
   fixed-size memcpy into one move. */
static uint32_t gs_ld32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t gs_ld64(const void *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* The low k bytes of a word set (none for k <= 0, all for k >= 8): which
   bytes of a little-endian load lie within a length. */
static uint64_t gs_bytemask(int64_t k) {
    return k >= 8 ? ~(uint64_t)0 : k <= 0 ? 0 : ((uint64_t)1 << (k * 8)) - 1;
}

/* memcpy, memmove and memcmp for a slice's elements. An empty slice's data
   pointer is NULL where the slice was zero-filled (default<T>(), a default
   element), and C leaves all three undefined on a null pointer even for
   zero bytes. */
static void gs_memcpy(void *dst, const void *src, size_t n) {
    if (n) memcpy(dst, src, n);
}

static void gs_memmove(void *dst, const void *src, size_t n) {
    if (n) memmove(dst, src, n);
}

/* Whether n bytes at a and b are equal: two overlapping loads per side up
   to 16 bytes, where keys and names mostly are and a call costs more than
   the compare, the library's memcmp beyond. TinyCC inlines no memcpy, so
   its build calls memcmp for every length. */
static int gs_memeq(const void *a, const void *b, size_t n) {
#ifndef __TINYC__
    const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
    if (n >= 8 && n <= 16)
        return ((gs_ld64(p) ^ gs_ld64(q)) | (gs_ld64(p + n - 8) ^ gs_ld64(q + n - 8))) == 0;
    if (n >= 4 && n < 8)
        return ((gs_ld32(p) ^ gs_ld32(q)) | (gs_ld32(p + n - 4) ^ gs_ld32(q + n - 4))) == 0;
    if (n < 4)
        return n == 0 || ((p[0] ^ q[0]) | (p[n >> 1] ^ q[n >> 1]) | (p[n - 1] ^ q[n - 1])) == 0;
#endif
    return n == 0 || memcmp(a, b, n) == 0;
}

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
   runtime across the benchmarks.

   The signed types' add, sub, mul and neg also take the file and line of the
   operation, for the debug build's overflow message; the release macros
   drop them unevaluated. */

#if GS_DEBUG
#define GS_OVFCHK(r, MIN, MAX, a, op, b, type, file, line) \
    do { if ((r) < (MIN) || (r) > (MAX)) gs_ovf((a), (op), (b), (type), (file), (line)); } while (0)
#else
#define GS_OVFCHK(r, MIN, MAX, a, op, b, type, file, line) ((void)0)
#endif

/* Division and modulo, both builds: the zero check is not optional, and `%`
   is Euclidean. */
#define GS_DIVOPS_S(SFX, T, MIN, MAX) \
static T gs_div_##SFX(T a, T b, const char *file, int line) { \
    if (b == 0) gs_divfail(file, line); \
    int64_t r = (int64_t)a / (int64_t)b; \
    GS_OVFCHK(r, MIN, MAX, a, "/", b, #SFX, file, line); \
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
static T gs_add_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a + (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(a, "+", b, #SFX, file, line); \
    return (T)r; } \
static T gs_sub_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a - (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(a, "-", b, #SFX, file, line); \
    return (T)r; } \
static T gs_mul_##SFX(T a, T b, const char *file, int line) { \
    int64_t r = (int64_t)a * (int64_t)b; \
    if (r < MIN || r > MAX) gs_ovf(a, "*", b, #SFX, file, line); \
    return (T)r; } \
static T gs_neg_##SFX(T a, const char *file, int line) { \
    int64_t r = -(int64_t)a; \
    if (r < MIN || r > MAX) gs_ovf_neg(a, #SFX, file, line); \
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
static int64_t gs_add_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a + (uint64_t)b);
    if (((a ^ r) & (b ^ r)) < 0) gs_ovf(a, "+", b, "i64", file, line);
    return r;
}
static int64_t gs_sub_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a - (uint64_t)b);
    if (((a ^ b) & (a ^ r)) < 0) gs_ovf(a, "-", b, "i64", file, line);
    return r;
}
static int64_t gs_mul_i64(int64_t a, int64_t b, const char *file, int line) {
    int64_t r = (int64_t)((uint64_t)a * (uint64_t)b);
    if (a && b &&
        ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN) || r / b != a))
        gs_ovf(a, "*", b, "i64", file, line);
    return r;
}
static int64_t gs_neg_i64(int64_t a, const char *file, int line) {
    if (a == INT64_MIN) gs_ovf_neg(a, "i64", file, line);
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

#define gs_add_i8(a, b, f, l)  ((int8_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i8(a, b, f, l)  ((int8_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i8(a, b, f, l)  ((int8_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i8(a, f, l)     ((int8_t)(-(int64_t)(a)))
#define gs_shl_i8(a, n)  ((int8_t)((uint64_t)(a) << ((n) & 7)))
#define gs_shr_i8(a, n)  ((int8_t)((int64_t)(a) >> ((n) & 7)))

#define gs_add_i16(a, b, f, l) ((int16_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i16(a, b, f, l) ((int16_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i16(a, b, f, l) ((int16_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i16(a, f, l)    ((int16_t)(-(int64_t)(a)))
#define gs_shl_i16(a, n) ((int16_t)((uint64_t)(a) << ((n) & 15)))
#define gs_shr_i16(a, n) ((int16_t)((int64_t)(a) >> ((n) & 15)))

#define gs_add_i32(a, b, f, l) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define gs_sub_i32(a, b, f, l) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define gs_mul_i32(a, b, f, l) ((int32_t)((int64_t)(a) * (int64_t)(b)))
#define gs_neg_i32(a, f, l)    ((int32_t)(-(int64_t)(a)))
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

#define gs_add_i64(a, b, f, l) ((int64_t)((uint64_t)(a) + (uint64_t)(b)))
#define gs_sub_i64(a, b, f, l) ((int64_t)((uint64_t)(a) - (uint64_t)(b)))
#define gs_mul_i64(a, b, f, l) ((int64_t)((uint64_t)(a) * (uint64_t)(b)))
#define gs_neg_i64(a, f, l)    ((int64_t)(0u - (uint64_t)(a)))
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
    if (a == INT64_MIN && b == -1) gs_divovf(file, line);
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

/* Unsigned division by a divisor a loop does not change, in libdivide's
   form: computed once before the loop (gs_divu_gen), x / d is then the high
   half of a product, adjusted where GS_DIVU_ADD is in `more` and shifted
   (gs_divu_q). A zero divisor, and a C compiler without 128-bit products,
   get GS_DIVU_NONE, and their divisions the plain operator, which reports a
   zero divisor where the division is. */
#define GS_DIVU_NONE 255
#define GS_DIVU_ADD 64
#define GS_DIVU_SHIFT 63
#if defined(__TINYC__)
#define GS_HAVE_U128 0
#elif defined(__SIZEOF_INT128__)
#define GS_HAVE_U128 1
static uint64_t gs_mulhi_u64(uint64_t a, uint64_t b) {
    return (uint64_t)(((unsigned __int128)a * b) >> 64);
}
/* (hi * 2^64) / d and its remainder, for hi < d. Clang targeting the
   Microsoft ABI links no 128-bit division routine, so x86-64 divides with
   the instruction itself. */
static uint64_t gs_div128_u64(uint64_t hi, uint64_t d, uint64_t *rem) {
#if defined(__x86_64__)
    uint64_t q, r;
    __asm__("divq %[d]" : "=a"(q), "=d"(r) : [d] "r"(d), "a"((uint64_t)0), "d"(hi));
    *rem = r;
    return q;
#else
    unsigned __int128 n = (unsigned __int128)hi << 64;
    *rem = (uint64_t)(n % d);
    return (uint64_t)(n / d);
#endif
}
#elif defined(_MSC_VER) && _MSC_VER >= 1920 && defined(_M_X64)
#include <intrin.h>
#define GS_HAVE_U128 1
static uint64_t gs_mulhi_u64(uint64_t a, uint64_t b) { return __umulh(a, b); }
static uint64_t gs_div128_u64(uint64_t hi, uint64_t d, uint64_t *rem) {
    return _udiv128(hi, 0, d, rem);
}
#else
#define GS_HAVE_U128 0
#endif

static uint64_t gs_divu_gen(uint64_t d, uint8_t *more) {
#if GS_HAVE_U128
    int k = 63;
    uint64_t rem, m;
    if (d == 0) { *more = GS_DIVU_NONE; return 0; }
    while (!(d >> k)) k--;
    if (!(d & (d - 1))) { *more = (uint8_t)k; return 0; }
    /* floor(2^(64+k) / d), which fits since d > 2^k; then the smallest
       power that works, or the 65-bit form one past it. */
    m = gs_div128_u64((uint64_t)1 << k, d, &rem);
    if (d - rem < ((uint64_t)1 << k)) {
        *more = (uint8_t)k;
    } else {
        uint64_t twice = rem + rem;
        m += m;
        if (twice >= d || twice < rem) m++;
        *more = (uint8_t)(k | GS_DIVU_ADD);
    }
    return m + 1;
#else
    (void)d;
    *more = GS_DIVU_NONE;
    return 0;
#endif
}

static uint64_t gs_divu_q(uint64_t x, uint64_t magic, uint8_t more) {
#if GS_HAVE_U128
    uint64_t q;
    if (!magic) return x >> more;
    q = gs_mulhi_u64(magic, x);
    if (more & GS_DIVU_ADD) return (((x - q) >> 1) + q) >> (more & GS_DIVU_SHIFT);
    return q >> more;
#else
    (void)magic;
    return x >> more;   /* Never called: GS_DIVU_NONE takes the operator. */
#endif
}

/* `as!` float-to-int: truncate toward zero, wrap modulo 2^64 (§6.3). Defined
   the same on every platform, unlike a raw C cast of an out-of-range value.
   A value lies in the i64 range exactly when its truncation does, and there
   the C cast truncates by itself (one hardware conversion, where trunc() is
   a libm call on baseline x86-64), so only NaN and the values beyond the
   range take the wrap, out of line. */
static GS_NOINLINE int64_t gs_f2iwrap_slow(double d) {
    if (d != d) return 0;
    d = fmod(trunc(d), 18446744073709551616.0);
    if (d < 0) d += 18446744073709551616.0;
    return (int64_t)(uint64_t)d;
}
static int64_t gs_f2iwrap(double d) {
    if (d >= -9223372036854775808.0 && d < 9223372036854775808.0) return (int64_t)d;
    return gs_f2iwrap_slow(d);
}

/* `as` conversion checks (§6.3): abort in debug builds whenever the
   conversion would change the value, naming the value, the target type and
   the cast's file and line; identity/plain casts in release, which drop all
   three unevaluated. A conversion to a float is a plain C cast in every build
   and has none. */
#if GS_DEBUG

static int64_t gs_rangechk(int64_t v, int64_t lo, int64_t hi, const char *type,
                           const char *file, int line) {
    if (v < lo || v > hi) gs_asfail_i("out of range", v, type, file, line);
    return v;
}
static uint64_t gs_rangechk_u(uint64_t v, uint64_t hi, const char *type, const char *file,
                              int line) {
    if (v > hi) gs_asfail_u("out of range", v, type, file, line);
    return v;
}
/* To a signed type or a narrower unsigned one, whose range is lo..hi. */
static int64_t gs_f2ichk(double d, int f32, int64_t lo, int64_t hi, const char *type,
                         const char *file, int line) {
    if (!(d >= -9223372036854775808.0 && d < 9223372036854775808.0))
        gs_asfail_f("out of range", d, f32, type, file, line);
    int64_t v = (int64_t)d;
    if (v < lo || v > hi) gs_asfail_f("out of range", d, f32, type, file, line);
    if ((double)v != d) gs_asfail_f("changes the value", d, f32, type, file, line);
    return v;
}
static uint64_t gs_f2uchk(double d, int f32, const char *file, int line) {
    if (!(d >= 0 && d < 18446744073709551616.0))
        gs_asfail_f("out of range", d, f32, "u64", file, line);
    uint64_t v = (uint64_t)d;
    if ((double)v != d) gs_asfail_f("changes the value", d, f32, "u64", file, line);
    return v;
}
#define GS_RANGE(v, lo, hi, t, f, l) gs_rangechk((v), (lo), (hi), (t), (f), (l))
#define GS_RANGE_U(v, hi, t, f, l)   gs_rangechk_u((v), (hi), (t), (f), (l))
#define GS_F2I(d, s, lo, hi, t, f, l) gs_f2ichk((d), (s), (lo), (hi), (t), (f), (l))
#define GS_F2U(d, s, f, l)            gs_f2uchk((d), (s), (f), (l))

#else

#define GS_RANGE(v, lo, hi, t, f, l) (v)
#define GS_RANGE_U(v, hi, t, f, l)   (v)
/* Deterministic truncation in release too. */
#define GS_F2I(d, s, lo, hi, t, f, l) gs_f2iwrap(d)
#define GS_F2U(d, s, f, l)            ((uint64_t)gs_f2iwrap(d))

#endif

/* ---------------------------------------------------------------------------
   Data stacks (§1.2, Appendix C.1/C.4): large reserved regions, committed on
   use, bump-pointer allocation, watermark restore on scope exit. Emitted code
   holds them per thread program via gs_stks (index = the hidden gs_sp
   argument plus a per-function constant); globals own dedicated stacks. The
   regions themselves, and what tells a fault in one from a crash, are the
   runtime's (runtime_impl.h). */

typedef struct {
    uint8_t *top;
} gs_stack;

/* Starts the runtime on main's thread: the program's arguments, each
   region's usable reservation and trailing guard gap, the address space
   budgeted for regions over the whole program, and the most regions the
   main program and any one worker hold (the compiler's static counts),
   which size their registries and give hardware_threads() its cap. */
GS_API void gs_rt_start(int argc, char **argv, uint64_t reserve, uint64_t gap,
                        uint64_t budget, int64_t mainregions, int64_t workerregions);
/* A fresh region, registered to the calling thread program. */
GS_API uint8_t *gs_reserve_region(void);
/* The calling thread program's stack use, on stderr (GS_STACK_STATS):
   `stacks` is how many of its indexed data stacks exist. */
GS_API void gs_stack_stats(int64_t stacks);
/* Releases every region of the calling thread program, and what else the
   runtime keeps for its thread. */
GS_API void gs_release_regions(void);

/* ---------------------------------------------------------------------------
   Threads and typed queues (§11.2), runtime_threads.h. */

GS_API int64_t gs_hardware_threads(void);
/* Runs run(entry, args) on a new thread, args a copy of argsize bytes, and
   returns the worker's id. */
GS_API int64_t gs_thread_start(void (*run)(void (*)(uint8_t *), uint8_t *),
                               void (*entry)(uint8_t *), const void *args, int64_t argsize);
GS_API void gs_thread_wait(int64_t id, const char *file, int line);

/* One queue per flat element type the program uses (the compiler emits a
   gs_queue global per type, which main sets up before anything can use it).
   Values are contiguous byte images: gs_qget and gs_qpoll return a malloc'd
   node the caller copies from and frees, gs_qpoll NULL when the queue is
   empty. */
typedef struct gs_qnode {
    struct gs_qnode *next;
    int64_t size;
    /* Value bytes follow the header. */
} gs_qnode;

typedef struct gs_qstate *gs_queue;
#define GS_QUEUE_INIT NULL

GS_API void gs_qinit(gs_queue *q);
GS_API void gs_qput(gs_queue *q, const void *data, int64_t size);
GS_API gs_qnode *gs_qget(gs_queue *q);
GS_API gs_qnode *gs_qpoll(gs_queue *q);

#ifndef GS_RUNTIME_OBJECT

/* The current thread program's stack block: every stack the compiler
   counted for it, reserved as the program starts (gs_stack_block).
   gs_sp-relative indices resolve through it. */
static GS_TLS gs_stack *gs_stks;
static GS_TLS int64_t gs_nstks;

/* The current program instance's globals (goose_spec.md 11.1), a struct the
   compiler lays out: main's is its one static instance, a worker's a fresh
   copy of the globals its program uses, taken from the spawning instance
   at spawn like the arguments (11.2). No global is shared between program
   instances; the only C statics a program shares are read-only ones. */
static GS_TLS void *gs_gl;

#define GS(i) (&gs_stks[i])

static void gs_stack_init(gs_stack *s) {
    s->top = gs_reserve_region();
}

/* The calling thread program's block of n stacks, each with its region:
   main's from gs_rt_init, a worker's from its entry thunk. */
static void gs_stack_block(int64_t n) {
    gs_stks = (gs_stack *)calloc((size_t)(n > 0 ? n : 1), sizeof(gs_stack));
    if (!gs_stks) gs_panic("out of memory allocating stack block");
    for (int64_t i = 0; i < n; i++) gs_stack_init(&gs_stks[i]);
    gs_nstks = n;
}

/* The compiler passes the main program's stack count and the most regions
   it and any one worker hold: their stacks plus the dedicated ones of the
   globals and of a worker's arguments. */
static void gs_rt_init(int argc, char **argv, int64_t mainstacks, int64_t mainregions,
                       int64_t workerregions) {
    gs_rt_start(argc, argv, GS_STACK_RESERVE, GS_STACK_GAP, GS_STACK_BUDGET, mainregions,
                workerregions);
    gs_stack_block(mainstacks);
}

/* What a thread program reports as it ends under GS_STACK_STATS. */
static void gs_rt_stats(void) {
#if GS_STACK_STATS
    gs_stack_stats(gs_nstks);
#endif
}

/* Every region the calling thread program owns, with its stack block. No
   Goose reference to these mappings may outlive it. */
static void gs_free_thread_stacks(void) {
    gs_release_regions();
    free(gs_stks);
    gs_stks = NULL;
    gs_nstks = 0;
}

#if GS_NEED_THREADS
/* A worker's thread program: its entry thunk opens the stack block, whose
   count the compiler knows, and the runtime releases the regions after
   this returns. */
static void gs_thread_run(void (*entry)(uint8_t *), uint8_t *args) {
    entry(args);
    gs_rt_stats();
    free(gs_stks);
    gs_stks = NULL;
    gs_nstks = 0;
}

static int64_t gs_thread_spawn(void (*entry)(uint8_t *), const void *args, int64_t argsize) {
    return gs_thread_start(gs_thread_run, entry, args, argsize);
}
#endif

#endif  /* GS_RUNTIME_OBJECT */

/* ---------------------------------------------------------------------------
   Slice pools (§5.4, `reusable[]`). The freelist is a run of (index, count)
   spans on a data stack of its own, sorted by index, no two of them
   touching. The emitted code grows the element region and fills elements;
   these only keep the spans, and each takes the run's base, its span count
   and its stack's top, which stays at the end of the run. Indices and counts
   are in elements. Every span comes from a slice of the pool, so spans stay
   inside the pool's length whatever the program frees: freeing a slice twice
   leaves overlapping spans that hand the same elements out twice, never
   anything outside the pool. */

typedef struct { int64_t idx, cnt; } gs_span;

/* The number of spans starting at or before idx. */
static int64_t gs_spans_upto(const gs_span *s, int64_t n, int64_t idx) {
    int64_t lo = 0, hi = n;
    while (lo < hi) {
        int64_t mid = lo + (hi - lo) / 2;
        if (s[mid].idx <= idx) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static void gs_spans_drop(gs_span *s, int64_t *n, uint8_t **top, int64_t at) {
    memmove(s + at, s + at + 1, (size_t)(*n - at - 1) * sizeof(gs_span));
    (*n)--;
    *top -= sizeof(gs_span);
}

/* Takes cnt elements off the front of span `at`, or all of a span that
   holds no more. */
static void gs_spans_take(gs_span *s, int64_t *n, uint8_t **top, int64_t at, int64_t cnt) {
    if (s[at].cnt <= cnt) {
        gs_spans_drop(s, n, top, at);
        return;
    }
    s[at].idx += cnt;
    s[at].cnt -= cnt;
}

/* Where a run of cnt elements goes in a pool of len elements, for
   alloc_slice and for a slice realloc_slice moves: the front of the first
   span holding it, else the end of the array, where a free span that reaches
   the end starts the run. The caller grows the array to the returned index
   plus cnt. First fit rather than best: in index order it packs runs toward
   the start and leaves the end free, and it stops scanning at the fit. */
static int64_t gs_spans_alloc(uint8_t *base, int64_t *n, uint8_t **top, int64_t len,
                              int64_t cnt) {
    gs_span *s = (gs_span *)base;
    int64_t at = 0, idx;
    if (cnt == 0) return len;
    while (at < *n && s[at].cnt < cnt) at++;
    if (at == *n) {
        if (*n == 0 || s[*n - 1].idx + s[*n - 1].cnt != len) return len;
        at = *n - 1;
    }
    idx = s[at].idx;
    gs_spans_take(s, n, top, at, cnt);
    return idx;
}

/* realloc_slice growing a slice in place: whether cnt more elements can
   follow it where it ends, at `end` -- taken from a free span that starts
   there, or from past the end of the pool, directly or through such a span
   reaching it. The caller grows the array to whatever lies past its end. */
static int gs_spans_grow(uint8_t *base, int64_t *n, uint8_t **top, int64_t len, int64_t end,
                         int64_t cnt) {
    gs_span *s = (gs_span *)base;
    int64_t at;
    if (end == len) return 1;
    at = gs_spans_upto(s, *n, end) - 1;
    if (at < 0 || s[at].idx != end) return 0;
    if (s[at].cnt < cnt && end + s[at].cnt != len) return 0;
    gs_spans_take(s, n, top, at, cnt);
    return 1;
}

/* free_slice, and what realloc_slice lets go of: [idx, idx + cnt) back on
   the freelist, merged with the spans it touches. */
static void gs_spans_free(uint8_t *base, int64_t *n, uint8_t **top, int64_t idx, int64_t cnt) {
    gs_span *s = (gs_span *)base;
    int64_t at;
    int prev, next;
    if (cnt <= 0) return;
    at = gs_spans_upto(s, *n, idx);
    prev = at > 0 && s[at - 1].idx + s[at - 1].cnt == idx;
    next = at < *n && idx + cnt == s[at].idx;
    if (prev) {
        s[at - 1].cnt += cnt;
        if (next) {
            s[at - 1].cnt += s[at].cnt;
            gs_spans_drop(s, n, top, at);
        }
    } else if (next) {
        s[at].idx = idx;
        s[at].cnt += cnt;
    } else {
        memmove(s + at + 1, s + at, (size_t)(*n - at) * sizeof(gs_span));
        s[at].idx = idx;
        s[at].cnt = cnt;
        (*n)++;
        *top += sizeof(gs_span);
    }
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

/* An inline array's length prefix is one byte unless the array holds 128
   elements or more, so the two macros below are what the compiler emits for
   it: a load, a test, and the byte. The continuation is a call the caller's
   loop does not contain, and reaching it means there is a large array to
   walk, against which the call costs nothing. Both read the pointer twice,
   which the emitting sites already assume (they form the element address
   from the same text). A varint *value*, by contrast, is whatever the
   program stored, so scalar fields and relative offsets keep the loop above
   inline, where the compilers peel the first byte themselves. */

static GS_NOINLINE int64_t gs_uleb_read_slow(const uint8_t *p) {
    return gs_uleb_read(p);
}

static GS_NOINLINE int64_t gs_uleb_size_slow(const uint8_t *p) {
    return gs_uleb_size(p);
}

#define GS_ULEB_READ(p) ((*(p) & 0x80) ? gs_uleb_read_slow(p) : (int64_t)*(p))
#define GS_ULEB_SIZE(p) ((*(p) & 0x80) ? gs_uleb_size_slow(p) : (int64_t)1)

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
   Verified loading (docs/design/serialization.md): what the generated
   gs_verify_<T> walkers are built from. The bytes are untrusted until the
   walk finishes, so every read here is bounded by the image end and reports
   a malformed encoding instead of running past it. */

/* The ULEB128 at p, or 0 if it runs past `end`, past ten bytes, or carries
   payload bits above the 64th, or is not shortest. The result is the byte count. */
static int64_t gs_uleb_check(const uint8_t *p, const uint8_t *end, uint64_t *out) {
    uint64_t v = 0;
    int shift = 0;
    const uint8_t *q = p;
    for (;;) {
        uint8_t b;
        if (q >= end) return 0;
        b = *q++;
        if (shift > 63 || (shift == 63 && (b & 0x7e))) return 0;
        v |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) {
            if (shift && !b) return 0;  /* Redundant high zero group. */
            break;
        }
        shift += 7;
    }
    *out = v;
    return (int64_t)(q - p);
}

/* The same for a signed (zigzag) varint: a value field or a self-relative
   offset of varint width (3.6). */
static int64_t gs_zig_check(const uint8_t *p, const uint8_t *end, int64_t *out) {
    uint64_t u = 0;
    int64_t k = gs_uleb_check(p, end, &u);
    if (k) *out = (int64_t)((u >> 1) ^ (0u - (u & 1)));
    return k;
}

/* Element starts, one bit per image byte: the framing pass sets them and the
   link pass asks whether an offset is one. Only images of variable-size
   elements need it -- for fixed ones a start is a multiple of the size. The
   bitmap is one data stack's worth of scratch, so the largest image that can
   be verified is eight times a stack's reservation; from_bytes rejects a
   larger one rather than growing into the guard region. */
#define GS_BM_SET(bm, i) ((bm)[(uint64_t)(i) >> 3] |= (uint8_t)(1u << ((i) & 7)))
#define GS_BM_GET(bm, i) (((bm)[(uint64_t)(i) >> 3] >> ((i) & 7)) & 1)
#define GS_BM_MAX ((int64_t)(GS_STACK_RESERVE))

/* An image is little-endian by definition (serialization.md 7), which every
   target this compiles for is. The test is a constant to any optimizer; it is
   here so that a big-endian host fails loudly instead of writing bytes that
   only it can read back. */
static int gs_is_le(void) {
    const uint16_t one = 1;
    return *(const uint8_t *)&one == 1;
}

/* ---------------------------------------------------------------------------
   Text forms (§3.7): the gs_fmt_* functions write a value's text at dst and
   return the byte count (at most GS_FMT_MAX); print/str/format are built on
   them. A float takes the shortest form that still round-trips. */

#define GS_FMT_MAX 32

GS_API int64_t gs_fmt_i64(uint8_t *dst, int64_t v);
GS_API int64_t gs_fmt_u64(uint8_t *dst, uint64_t v);
GS_API int64_t gs_fmt_f64(uint8_t *dst, double v);
GS_API int64_t gs_fmt_f32(uint8_t *dst, float v);
GS_API int64_t gs_fmt_bool(uint8_t *dst, int64_t v);
/* A u8 array inside an aggregate: quoted, with the escapes Goose reads. */
GS_API int64_t gs_fmt_quoted(uint8_t *dst, const uint8_t *s, int64_t n);

/* print(...) (§3.7): each argument's text, then a newline. */
GS_API void gs_out_int(int64_t v);
GS_API void gs_out_uint(uint64_t v);
GS_API void gs_out_flt(double v);
GS_API void gs_out_f32(float v);
GS_API void gs_out_bool(int64_t v);
GS_API void gs_out_bytes(const uint8_t *p, int64_t len);
GS_API void gs_out_nl(void);

/* Byte search behind std's find_any and find_pair (runtime_impl.h): the
   first i < n with p[i] in the set, or with p[i] in a and p[i + d] in b
   (i + d < n); -1 if there is none. A set is std's ByteSet. */
GS_API int64_t gs_scan_any(const uint8_t *p, int64_t n, const void *set);
GS_API int64_t gs_scan_pair(const uint8_t *p, int64_t n, const void *a, int64_t d,
                            const void *b);
