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
    GS_JIT_MATH(ldexp)
    GS_JIT_MATH(frexp)
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
    gs_release_regions();
    cleanup();
#ifdef _WIN32
    if (gs_jit_fault_handler) RemoveVectoredExceptionHandler(gs_jit_fault_handler);
    gs_jit_fault_handler = NULL;
#else
    if (gs_jit_fault_installed) {
        sigaction(SIGSEGV, &gs_prev_segv, NULL);
#ifdef SIGBUS
        if (gs_jit_fault_installed & 2) sigaction(SIGBUS, &gs_prev_bus, NULL);
#endif
    }
    gs_jit_fault_installed = 0;
#endif
    gs_argc = 0;
    gs_argv = NULL;
    gs_jit_resource_dir = NULL;
    return gs_jit_status;
}
