/* Goose runtime: extern-fn support and the OS primitives behind
   stdlib/os.goose (spec §7.10). Unlike the other runtime files this one is
   spliced in after the generated type declarations, since its functions
   are written against them: sl_u8 (a u8 slice: data, len) and gs_rref (a
   reference to a resizable: its header and its data stack). The generated
   program calls these directly from `extern "gs_os_..." fn` declarations;
   no prototype is emitted for a symbol defined here. */

#include <time.h>

/* Appends n bytes to the u8[>..] a builder reference points at: the
   resizable's elements top its stack, so the bytes go at the stack top and
   the header's count grows. */
static void gs_bld_append(gs_rref b, const void *p, int64_t n) {
    if (n <= 0) return;
    memcpy(b.stk->top, p, (size_t)n);
    b.stk->top += n;
    b.hdr->len += n;
}

/* A NUL-terminated copy of a slice, for C APIs; the buffer is per call. */
static char *gs_os_cstr(sl_u8 s, char *buf, size_t cap) {
    size_t n = (size_t)(s.len < 0 ? 0 : s.len);
    if (n >= cap) n = cap - 1;
    memcpy(buf, s.data, n);
    buf[n] = 0;
    return buf;
}

static uint8_t gs_os_read_file(sl_u8 path, gs_rref out) {
    char pb[4096];
    FILE *f = fopen(gs_os_cstr(path, pb, sizeof pb), "rb");
    if (!f) return 0;
    uint8_t buf[65536];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, f);
        if (n == 0) break;
        gs_bld_append(out, buf, (int64_t)n);
    }
    int bad = ferror(f);
    fclose(f);
    return bad ? 0 : 1;
}

static uint8_t gs_os_write_file(sl_u8 path, sl_u8 data) {
    char pb[4096];
    FILE *f = fopen(gs_os_cstr(path, pb, sizeof pb), "wb");
    if (!f) return 0;
    size_t n = (size_t)(data.len < 0 ? 0 : data.len);
    int ok = fwrite(data.data, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    return ok ? 1 : 0;
}

static uint8_t gs_os_append_file(sl_u8 path, sl_u8 data) {
    char pb[4096];
    FILE *f = fopen(gs_os_cstr(path, pb, sizeof pb), "ab");
    if (!f) return 0;
    size_t n = (size_t)(data.len < 0 ? 0 : data.len);
    int ok = fwrite(data.data, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    return ok ? 1 : 0;
}

static uint8_t gs_os_file_exists(sl_u8 path) {
    char pb[4096];
    FILE *f = fopen(gs_os_cstr(path, pb, sizeof pb), "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static uint8_t gs_os_remove_file(sl_u8 path) {
    char pb[4096];
    return remove(gs_os_cstr(path, pb, sizeof pb)) == 0 ? 1 : 0;
}

static void gs_os_write_stdout(sl_u8 s) {
    if (s.len > 0) fwrite(s.data, 1, (size_t)s.len, stdout);
}

static void gs_os_write_stderr(sl_u8 s) {
    if (s.len > 0) fwrite(s.data, 1, (size_t)s.len, stderr);
}

static void gs_os_flush_stdout(void) { fflush(stdout); }

/* One line of stdin, without its newline; false at end of input. */
static uint8_t gs_os_read_line(gs_rref out) {
    int c = fgetc(stdin);
    if (c == EOF) return 0;
    while (c != EOF && c != '\n') {
        uint8_t b = (uint8_t)c;
        gs_bld_append(out, &b, 1);
        c = fgetc(stdin);
    }
    if (out.hdr->len > 0 && out.hdr->base[out.hdr->len - 1] == '\r') {
        out.hdr->len--;
        out.stk->top--;
    }
    return 1;
}

/* All of stdin. */
static void gs_os_read_stdin(gs_rref out) {
    uint8_t buf[65536];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, stdin);
        if (n == 0) break;
        gs_bld_append(out, buf, (int64_t)n);
    }
}

static int64_t gs_os_arg_count(void) { return gs_argc; }

static void gs_os_arg(int64_t i, gs_rref out) {
    if (i < 0 || i >= gs_argc) return;
    gs_bld_append(out, gs_argv[i], (int64_t)strlen(gs_argv[i]));
}

static uint8_t gs_os_getenv(sl_u8 name, gs_rref out) {
    char nb[1024];
    const char *v = getenv(gs_os_cstr(name, nb, sizeof nb));
    if (!v) return 0;
    gs_bld_append(out, v, (int64_t)strlen(v));
    return 1;
}

/* Wall-clock time in nanoseconds since the Unix epoch. */
static int64_t gs_os_time_ns(void) {
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

/* A monotonic clock in nanoseconds, for measuring intervals. */
static int64_t gs_os_clock_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (int64_t)((double)c.QuadPart * (1000000000.0 / (double)f.QuadPart));
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
}

static void gs_os_sleep_ms(int64_t ms) {
    if (ms <= 0) return;
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts;
    ts.tv_sec = (time_t)(ms / 1000);
    ts.tv_nsec = (long)((ms % 1000) * 1000000);
    nanosleep(&ts, NULL);
#endif
}

/* Entropy for seeding a PRNG (not cryptographic): /dev/urandom where there
   is one; on Windows a splitmix64 mix of the clocks, the process id and an
   address, which needs no library beyond the runtime's. */
static uint64_t gs_os_mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15u;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9u;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebu;
    return x ^ (x >> 31);
}

static uint64_t gs_os_random_u64(void) {
#ifndef _WIN32
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        uint64_t v = 0;
        size_t n = fread(&v, 1, sizeof v, f);
        fclose(f);
        if (n == sizeof v) return v;
    }
#endif
    static uint64_t counter;
    uint64_t seed = (uint64_t)gs_os_time_ns();
    seed = gs_os_mix64(seed ^ (uint64_t)gs_os_clock_ns());
#ifdef _WIN32
    seed = gs_os_mix64(seed ^ (uint64_t)GetCurrentProcessId());
#endif
    seed = gs_os_mix64(seed ^ (uint64_t)(uintptr_t)&counter);
    return gs_os_mix64(seed ^ ++counter);
}
