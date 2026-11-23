/* The same runtime object AOT programs link, hosted by the compiler. The JIT
   keeps its generated types, stack pointers and inline helpers in its own
   unit; platform operations and their headers are compiled here. */
#include "jit_runtime.h"
#include <setjmp.h>
#include <stdlib.h>

static jmp_buf gs_jit_jump;
static int gs_jit_status;
static const char *gs_jit_resource_dir;
static void gs_jit_exit(int code);

#define GS_SEPARATE_RUNTIME 1
#define GS_RUNTIME_OBJECT 1
#define GS_NEED_THREADS 1
#define GS_HOSTED_JIT 1
#define GS_JIT_RESOURCE_DIR gs_jit_resource_dir
#include "runtime/runtime.h"
#include "runtime/runtime_impl.h"
#include "runtime/runtime_threads.h"
#include "runtime/runtime_ext.h"
#include "runtime/runtime_os.h"

static void gs_jit_exit(int code) {
    gs_jit_status = code;
    longjmp(gs_jit_jump, 1);
}

#define GS_JIT_SYMBOL(f) { #f, (const void *)f },
static const GooseJitSymbol gs_jit_symbols[] = {
#include "jit_symbols.inc"
    GS_JIT_SYMBOL(memcpy)
    GS_JIT_SYMBOL(memmove)
    GS_JIT_SYMBOL(memset)
    GS_JIT_SYMBOL(memcmp)
    GS_JIT_SYMBOL(memchr)
    GS_JIT_SYMBOL(strlen)
    GS_JIT_SYMBOL(malloc)
    GS_JIT_SYMBOL(calloc)
    GS_JIT_SYMBOL(realloc)
    GS_JIT_SYMBOL(free)
    GS_JIT_SYMBOL(abs)
#define GS_JIT_MATH(fn) GS_JIT_SYMBOL(fn) GS_JIT_SYMBOL(fn##f)
    GS_JIT_MATH(sqrt)
    GS_JIT_MATH(sin)
    GS_JIT_MATH(cos)
    GS_JIT_MATH(tan)
    GS_JIT_MATH(asin)
    GS_JIT_MATH(acos)
    GS_JIT_MATH(atan)
    GS_JIT_MATH(atan2)
    GS_JIT_MATH(exp)
    GS_JIT_MATH(log)
    GS_JIT_MATH(log2)
    GS_JIT_MATH(log10)
    GS_JIT_MATH(pow)
    GS_JIT_MATH(floor)
    GS_JIT_MATH(ceil)
    GS_JIT_MATH(round)
    GS_JIT_MATH(trunc)
    GS_JIT_MATH(fabs)
    GS_JIT_MATH(fmod)
    GS_JIT_MATH(cbrt)
    GS_JIT_MATH(hypot)
    GS_JIT_MATH(exp2)
    GS_JIT_MATH(expm1)
    GS_JIT_MATH(log1p)
    GS_JIT_MATH(sinh)
    GS_JIT_MATH(cosh)
    GS_JIT_MATH(tanh)
    GS_JIT_MATH(copysign)
    GS_JIT_MATH(ldexp)
    GS_JIT_MATH(frexp)
    GS_JIT_MATH(acosh)
    GS_JIT_MATH(asinh)
    GS_JIT_MATH(atanh)
    GS_JIT_MATH(erf)
    GS_JIT_MATH(erfc)
    GS_JIT_MATH(tgamma)
    GS_JIT_MATH(lgamma)
    GS_JIT_MATH(logb)
    GS_JIT_MATH(rint)
    GS_JIT_MATH(nearbyint)
    GS_JIT_MATH(fmin)
    GS_JIT_MATH(fmax)
    GS_JIT_MATH(fdim)
    GS_JIT_MATH(nextafter)
    GS_JIT_MATH(remainder)
    GS_JIT_MATH(fma)
    GS_JIT_MATH(scalbn)
    GS_JIT_MATH(scalbln)
    GS_JIT_MATH(ilogb)
    GS_JIT_MATH(lrint)
    GS_JIT_MATH(llrint)
    GS_JIT_MATH(lround)
    GS_JIT_MATH(llround)
    GS_JIT_MATH(modf)
    GS_JIT_MATH(remquo)
    GS_JIT_MATH(nan)
#undef GS_JIT_MATH
    { "exit", (const void *)gs_jit_exit },
    { NULL, NULL }
};
#undef GS_JIT_SYMBOL

const GooseJitSymbol *goose_jit_symbols(void) { return gs_jit_symbols; }

int goose_jit_run(int (*entry)(int, char **), void (*cleanup)(void),
                  int argc, char **argv, const char *resource_dir) {
    gs_jit_resource_dir = resource_dir;
    gs_thread_cap = INT64_MAX;
    gs_jit_status = 0;
    if (!setjmp(gs_jit_jump)) gs_jit_status = entry(argc, argv);
    fflush(NULL);
    /* Main's regions and fault handling, then the program's stack block.
       Workers the program left running are the process's to end, as a
       built program's are (spec 11.2). */
    gs_rt_release();
    cleanup();
    gs_jit_resource_dir = NULL;
    return gs_jit_status;
}
