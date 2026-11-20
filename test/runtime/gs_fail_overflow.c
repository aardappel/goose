/* Direct runtime regression: gs_abort_msg's length is a C caller's to
   choose, and a hostile one (a negative length, or one whose message
   cannot fit memory once the lead and location are added) must end the
   program as a plain failure, not overflow the size gs_fail allocates and
   copy out of bounds. A child process takes the abort with its stderr on
   a pipe; the parent requires a clean exit 1 with nothing printed, since
   without the guard the size wraps, the allocation fails, and the
   no-memory path prints the lead and copies `len` bytes out of a 2-byte
   message. POSIX only: Windows keeps the code unexercised here, as the
   sanitizer CI that would catch the overflow runs on Linux. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

#define GS_NEED_THREADS 1
#define GS_STACK_RESERVE (256u << 10)
#define GS_STACK_GAP (64u << 10)
#include "../../src/runtime/runtime.h"
#include "../../src/runtime/runtime_impl.h"
#include "../../src/runtime/runtime_threads.h"

static void check(int ok, const char *what) {
    if (!ok) {
        fprintf(stderr, "gs_fail overflow check failed: %s\n", what);
        exit(2);
    }
}

/* Whether the child's abort with a hostile length is a clean exit 1 that
   printed nothing. */
static int clean_abort(int64_t len) {
#ifdef _WIN32
    (void)len;
    return 1;
#else
    int p[2];
    if (pipe(p)) exit(2);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(p[1], 2);
        close(p[0]);
        close(p[1]);
        gs_abort_msg((const uint8_t *)"x", len, "gs_fail_overflow.c", 42);
        _exit(0);  /* Not reached: gs_abort_msg does not return. */
    }
    close(p[1]);
    char buf[256];
    size_t total = 0;
    for (;;) {
        ssize_t n = read(p[0], buf, sizeof buf);
        if (n <= 0) break;
        total += (size_t)n;
    }
    close(p[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    return total == 0 && WIFEXITED(status) && WEXITSTATUS(status) == 1;
#endif
}

int main(void) {
    check(clean_abort(INT64_MAX / 2), "huge message length exits 1 silently");
    check(clean_abort(INT64_MIN), "negative message length exits 1 silently");
    printf("gs_fail overflow: ok\n");
    return 0;
}
