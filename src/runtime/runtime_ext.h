/* Goose runtime: extern-fn support, spliced in after the generated type
   declarations, since it is written against them: sl_u8 (a u8 slice: data,
   len) and gs_rref (a reference to a resizable: its header and its data
   stack). The runtime object has no generated types, and lays out the ones
   it uses here as codegen does (CodeGen::EmitCoreTypes, CT). An --include
   header follows this and may use what it defines. The generated program
   calls the OS primitives behind stdlib/os.goose (spec §7.10), slice_pool's
   span tree (both defined in runtime_os.h) and std's byte search directly
   from `extern "gs_..." fn` declarations; no prototype is emitted for a
   function declared here. */

#ifdef GS_RUNTIME_OBJECT
#pragma pack(push, 1)
typedef struct { uint8_t *base; int64_t len; } gs_rhdr;
typedef struct { gs_rhdr *hdr; gs_stack *stk; } gs_rref;
typedef struct { uint8_t *data; int64_t len; } sl_u8;
#pragma pack(pop)
#endif

/* Appends n bytes to the u8[>..] a builder reference points at: the
   resizable's elements top its stack, so the bytes go at the stack top and
   the header's count grows. */
static void gs_bld_append(gs_rref b, const void *p, int64_t n) {
    if (n <= 0) return;
    memcpy(b.stk->top, p, (size_t)n);
    b.stk->top += n;
    b.hdr->len += n;
}

/* ffi's cstr and bytes (stdlib/ffi.goose): the bytes at a pointer a C
   library handed back as a u64, copied into a builder, so nothing foreign
   is a Goose value without a copy. The pointer is trusted as every extern
   call trusts C; a null one appends nothing and is the false result. */
static uint8_t gs_ffi_cstr(uint64_t p, gs_rref out) {
    const char *s = (const char *)(uintptr_t)p;
    if (!s) return 0;
    gs_bld_append(out, s, (int64_t)strlen(s));
    return 1;
}
static uint8_t gs_ffi_bytes(uint64_t p, int64_t n, gs_rref out) {
    if (!p) return 0;
    gs_bld_append(out, (const void *)(uintptr_t)p, n);
    return 1;
}

/* math's sqrt (stdlib/math.goose). Goose has no errno, and C's sqrt has to
   set it for a negative argument: clang and gcc then guard the square root
   instruction with a test and a library call at every use, and will not
   vectorize a loop around one. A compiler that has a square root without
   errno uses that instead; the value is the same correctly rounded root
   either way, and NaN for a negative argument. */
#if defined(__has_builtin)
#if __has_builtin(__builtin_elementwise_sqrt)
#define GS_SQRT_NO_ERRNO 1
#endif
#endif
#ifdef GS_SQRT_NO_ERRNO
static double gs_sqrt(double x) { return __builtin_elementwise_sqrt(x); }
static float gs_sqrtf(float x) { return __builtin_elementwise_sqrt(x); }
#else
static double gs_sqrt(double x) { return sqrt(x); }
static float gs_sqrtf(float x) { return sqrtf(x); }
#endif

/* math's mul_add (stdlib/math.goose): x * y + z, fused into one rounding
   where the code is built for FMA, as two roundings elsewhere
   (GS_MUL_ADD_MODE, runtime.h). A call names the macro, which picks the
   mode where the call is written: a simd function's AVX2 and AVX-512
   versions fuse whatever the target. A function value is resolved at
   compile time, so its calls name the macro too. The function of the same
   name, in the target's mode, is only for C that names it without the
   macro. The fused form is inlined into its caller, so that it is built
   with the caller's FMA. */
static inline double gs_mul_add_separate(double x, double y, double z) { return x * y + z; }
static inline float gs_mul_addf_separate(float x, float y, float z) { return x * y + z; }
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
static inline __attribute__((always_inline)) double gs_mul_add_fused(double x, double y, double z) {
    return __builtin_fma(x, y, z);
}
static inline __attribute__((always_inline)) float gs_mul_addf_fused(float x, float y, float z) {
    return __builtin_fmaf(x, y, z);
}
#else
static inline double gs_mul_add_fused(double x, double y, double z) { return fma(x, y, z); }
static inline float gs_mul_addf_fused(float x, float y, float z) { return fmaf(x, y, z); }
#endif
#define GS_MUL_ADD_CAT2(a, b) a##b
#define GS_MUL_ADD_CAT(a, b) GS_MUL_ADD_CAT2(a, b)
static double gs_mul_add(double x, double y, double z) {
    return GS_MUL_ADD_CAT(gs_mul_add_, GS_MUL_ADD_BASE)(x, y, z);
}
static float gs_mul_addf(float x, float y, float z) {
    return GS_MUL_ADD_CAT(gs_mul_addf_, GS_MUL_ADD_BASE)(x, y, z);
}
#define gs_mul_add(x, y, z) GS_MUL_ADD_CAT(gs_mul_add_, GS_MUL_ADD_MODE)(x, y, z)
#define gs_mul_addf(x, y, z) GS_MUL_ADD_CAT(gs_mul_addf_, GS_MUL_ADD_MODE)(x, y, z)

/* math's other libm functions (stdlib/math.goose). C has them set errno on a
   domain or range error, so a C compiler takes each call for a write to
   memory: a loop around one loads again after it whatever it had hoisted
   out or kept in registers. Goose has no errno, so where the compiler has
   asm labels and the const attribute each is libm's own function declared
   under a name of its own as depending on its arguments alone; elsewhere
   the name forwards to libm. */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
#define GS_LIBM_SYM2(p, f) #p #f
#define GS_LIBM_SYM(p, f) GS_LIBM_SYM2(p, f)
#define GS_LIBM
#define GS_LIBM1(f) __asm__(GS_LIBM_SYM(__USER_LABEL_PREFIX__, f)) __attribute__((const));
#define GS_LIBM2(f) GS_LIBM1(f)
#else
#define GS_LIBM static
#define GS_LIBM1(f) { return f(x); }
#define GS_LIBM2(f) { return f(x, y); }
#endif
GS_LIBM double gs_sin(double x) GS_LIBM1(sin)
GS_LIBM double gs_cos(double x) GS_LIBM1(cos)
GS_LIBM double gs_tan(double x) GS_LIBM1(tan)
GS_LIBM double gs_asin(double x) GS_LIBM1(asin)
GS_LIBM double gs_acos(double x) GS_LIBM1(acos)
GS_LIBM double gs_atan(double x) GS_LIBM1(atan)
GS_LIBM double gs_atan2(double x, double y) GS_LIBM2(atan2)
GS_LIBM double gs_exp(double x) GS_LIBM1(exp)
GS_LIBM double gs_log(double x) GS_LIBM1(log)
GS_LIBM double gs_log2(double x) GS_LIBM1(log2)
GS_LIBM double gs_log10(double x) GS_LIBM1(log10)
GS_LIBM double gs_pow(double x, double y) GS_LIBM2(pow)
GS_LIBM float gs_sinf(float x) GS_LIBM1(sinf)
GS_LIBM float gs_cosf(float x) GS_LIBM1(cosf)
GS_LIBM float gs_tanf(float x) GS_LIBM1(tanf)
GS_LIBM float gs_asinf(float x) GS_LIBM1(asinf)
GS_LIBM float gs_acosf(float x) GS_LIBM1(acosf)
GS_LIBM float gs_atanf(float x) GS_LIBM1(atanf)
GS_LIBM float gs_atan2f(float x, float y) GS_LIBM2(atan2f)
GS_LIBM float gs_expf(float x) GS_LIBM1(expf)
GS_LIBM float gs_logf(float x) GS_LIBM1(logf)
GS_LIBM float gs_log2f(float x) GS_LIBM1(log2f)
GS_LIBM float gs_log10f(float x) GS_LIBM1(log10f)
GS_LIBM float gs_powf(float x, float y) GS_LIBM2(powf)

/* std's find_any and find_pair over a slice (gs_scan_any, gs_scan_pair); a
   set is a pointer to std's ByteSet. */
static int64_t gs_find_any(sl_u8 s, const void *set) { return gs_scan_any(s.data, s.len, set); }
static int64_t gs_find_pair(sl_u8 s, const void *a, int64_t d, const void *b) {
    return gs_scan_pair(s.data, s.len, a, d, b);
}

/* std's popcount, clz and ctz (64 for zero): the instruction where the C
   compiler has one, and counting under TinyCC. MSVC's popcount is an
   instruction past x86-64's baseline, which /arch:AVX and up promise. */
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif
static int64_t gs_popcount(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return __builtin_popcountll(x);
#elif defined(_MSC_VER) && defined(_M_X64) && defined(__AVX__)
    return (int64_t)__popcnt64(x);
#else
    int64_t n = 0;
    while (x) {
        x &= x - 1;
        n++;
    }
    return n;
#endif
}
static int64_t gs_clz(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return x ? __builtin_clzll(x) : 64;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    return _BitScanReverse64(&i, x) ? 63 - (int64_t)i : 64;
#else
    int64_t n = 0;
    if (!x) return 64;
    while (!(x >> 63)) {
        x <<= 1;
        n++;
    }
    return n;
#endif
}
static int64_t gs_ctz(uint64_t x) {
#if (defined(__GNUC__) || defined(__clang__)) && !defined(__TINYC__)
    return x ? __builtin_ctzll(x) : 64;
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_ARM64))
    unsigned long i;
    return _BitScanForward64(&i, x) ? (int64_t)i : 64;
#else
    int64_t n = 0;
    if (!x) return 64;
    while (!(x & 1)) {
        x >>= 1;
        n++;
    }
    return n;
#endif
}

GS_API uint8_t gs_os_read_file(sl_u8 path, gs_rref out);
GS_API uint8_t gs_os_write_file(sl_u8 path, sl_u8 data);
GS_API uint8_t gs_os_write_file_atomic(sl_u8 path, sl_u8 data);
GS_API uint8_t gs_os_append_file(sl_u8 path, sl_u8 data);
GS_API uint8_t gs_os_file_exists(sl_u8 path);
GS_API uint8_t gs_os_remove_file(sl_u8 path);
GS_API uint8_t gs_os_rename_file(sl_u8 from, sl_u8 to);
GS_API uint8_t gs_os_make_dir(sl_u8 path);
GS_API uint8_t gs_os_is_dir(sl_u8 path);
GS_API uint8_t gs_os_remove_dir(sl_u8 path);
GS_API uint8_t gs_os_list_dir(sl_u8 path, gs_rref out);
GS_API void gs_os_write_stdout(sl_u8 s);
GS_API void gs_os_write_stderr(sl_u8 s);
GS_API void gs_os_flush_stdout(void);
GS_API uint8_t gs_os_read_line(gs_rref out);
GS_API void gs_os_read_stdin(gs_rref out);
GS_API int64_t gs_os_arg_count(void);
GS_API void gs_os_arg(int64_t i, gs_rref out);
GS_API uint8_t gs_os_getenv(sl_u8 name, gs_rref out);
GS_API uint8_t gs_os_resource_dir(gs_rref out);
GS_API int64_t gs_os_time_ns(void);
GS_API int64_t gs_os_clock_ns(void);
GS_API void gs_os_sleep_ms(int64_t ms);
GS_API uint64_t gs_os_random_u64(void);
/* os.trim_memory: discards unused complete pages of this thread program's
   data stacks. Defined with the region registry it walks (runtime_impl.h),
   but declared here like the other os primitives, so that no prototype is
   emitted for it. For generated code only, whose callers write back their
   cached tops first; C called from Goose must not call it. 0 means an OS
   request failed; a top outside its region panics. */
GS_API uint8_t gs_trim_memory(void);
/* std's slice_pool (stdlib/slice_pool_internal.goose) keeps its freelist in
   a checked copy of the runtime's span tree (runtime_os.h): `nodes` is a
   node[>..] of GS_SPAN_NODE-byte nodes, and `tree` its header, a field
   beside it. A corrupt tree is reported at `at`, where the program called
   slice_pool (SFunction::takesloc). */
GS_API int64_t gs_slice_pool_place(gs_rref nodes, void *tree, int64_t len, int64_t cnt,
                                   const gs_loc *at);
GS_API void gs_slice_pool_free(gs_rref nodes, void *tree, int64_t idx, int64_t cnt,
                               const gs_loc *at);
GS_API int64_t gs_slice_pool_regrow(gs_rref nodes, void *tree, int64_t len, int64_t idx,
                                    int64_t ol, int64_t cnt, const gs_loc *at);
