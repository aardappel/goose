/* Goose runtime: extern-fn support and the OS primitives behind
   stdlib/os.goose (spec §7.10). Unlike the other runtime files this one is
   spliced in after the generated type declarations, since its functions
   are written against them: sl_u8 (a u8 slice: data, len) and gs_rref (a
   reference to a resizable: its header and its data stack). The generated
   program calls these directly from `extern "gs_os_..." fn` declarations;
   no prototype is emitted for a symbol defined here. */

#include <time.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
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

/* Paths are UTF-8 on every platform, and reach the C APIs NUL-terminated in
   a buffer of GS_OS_PATH_MAX characters. A path that would not fit, that
   holds a NUL byte (where C would end it) or, on Windows, that is not UTF-8
   is refused: cut short or read another way, it could name another file. */
#define GS_OS_PATH_MAX 4096

#ifdef _WIN32
/* Windows names files in UTF-16, which its wide APIs take as it is; the
   narrow ones read their bytes in the ANSI code page. */
typedef wchar_t gs_os_char;
#define gs_os_fopen(p, mode) _wfopen(p, L##mode)
/* Vista's; TinyCC's winnls.h predates it. */
#ifndef WC_ERR_INVALID_CHARS
#define WC_ERR_INVALID_CHARS 0x80
#endif
#else
typedef char gs_os_char;
#define gs_os_fopen(p, mode) fopen(p, mode)
#endif

/* The native form of a path, or of an environment variable's name, in buf
   (GS_OS_PATH_MAX characters): its length, or -1 where it is refused. */
static int gs_os_native(sl_u8 s, gs_os_char *buf) {
    if (s.len >= GS_OS_PATH_MAX || (s.len > 0 && memchr(s.data, 0, (size_t)s.len)))
        return -1;
    int n = (int)s.len;
#ifdef _WIN32
    /* A UTF-16 path has no more units than its UTF-8 form has bytes. */
    if (n > 0) {
        n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)s.data, n, buf,
                                GS_OS_PATH_MAX - 1);
        if (n == 0) return -1;
    }
#else
    gs_memcpy(buf, s.data, (size_t)n);
#endif
    buf[n] = 0;
    return n;
}

#ifdef _WIN32
/* Appends n UTF-16 units as UTF-8; where they are not valid UTF-16, appends
   nothing and returns 0. */
static int gs_os_append_wide(gs_rref out, const wchar_t *w, int n) {
    if (n <= 0) return 1;
    int k = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, n, NULL, 0, NULL, NULL);
    char *s = k > 0 ? (char *)malloc((size_t)k) : NULL;
    int ok = s && WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, n, s, k, NULL, NULL) == k;
    if (ok) gs_bld_append(out, s, k);
    free(s);
    return ok;
}
#else
/* An fsync that reaches the disk: on macOS a plain one stops at the drive's
   cache. */
static int gs_os_fsync(int fd) {
    #ifdef F_FULLFSYNC
        if (fcntl(fd, F_FULLFSYNC) == 0) return 1;
    #endif
    return fsync(fd) == 0;
}

/* A rename is on the disk once the directory holding the new name is (what
   MOVEFILE_WRITE_THROUGH waits for on Windows). Best effort: the rename has
   happened either way. */
static void gs_os_sync_dir(const char *path) {
    char dir[GS_OS_PATH_MAX];
    const char *slash = strrchr(path, '/');
    size_t n = slash ? (size_t)(slash - path) : 0;
    if (slash && n == 0) n = 1;
    if (n) memcpy(dir, path, n);
    else dir[n++] = '.';
    dir[n] = 0;
    int fd = open(dir, O_RDONLY);
    if (fd < 0) return;
    gs_os_fsync(fd);
    close(fd);
}
#endif

/* What a native path names: 0 nothing that can be reached, 1 something
   other than a directory, 2 a directory. */
static int gs_os_kind(const gs_os_char *p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesW(p);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;
    return (a & FILE_ATTRIBUTE_DIRECTORY) ? 2 : 1;
#else
    struct stat st;
    if (stat(p, &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 2 : 1;
#endif
}

static uint8_t gs_os_read_file(sl_u8 path, gs_rref out) {
    gs_os_char p[GS_OS_PATH_MAX];
    if (gs_os_native(path, p) < 0) return 0;
    FILE *f = gs_os_fopen(p, "rb");
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

/* write_file and append_file. */
static uint8_t gs_os_put_file(sl_u8 path, sl_u8 data, int append) {
    gs_os_char p[GS_OS_PATH_MAX];
    if (gs_os_native(path, p) < 0) return 0;
    FILE *f = append ? gs_os_fopen(p, "ab") : gs_os_fopen(p, "wb");
    if (!f) return 0;
    size_t n = (size_t)(data.len < 0 ? 0 : data.len);
    int ok = fwrite(data.data, 1, n, f) == n;
    if (fclose(f) != 0) ok = 0;
    return ok ? 1 : 0;
}

static uint8_t gs_os_write_file(sl_u8 path, sl_u8 data) { return gs_os_put_file(path, data, 0); }

static uint8_t gs_os_append_file(sl_u8 path, sl_u8 data) { return gs_os_put_file(path, data, 1); }

static uint8_t gs_os_file_exists(sl_u8 path) {
    gs_os_char p[GS_OS_PATH_MAX];
    return gs_os_native(path, p) >= 0 && gs_os_kind(p) == 1;
}

static uint8_t gs_os_remove_file(sl_u8 path) {
    gs_os_char p[GS_OS_PATH_MAX];
    if (gs_os_native(path, p) < 0) return 0;
#ifdef _WIN32
    return DeleteFileW(p) != 0;
#else
    /* Not remove, which takes an empty directory as well. */
    return unlink(p) == 0;
#endif
}

/* Replaces `to` if it exists, in one step: `to` names the old file or the
   moved one, never neither. */
static uint8_t gs_os_rename_file(sl_u8 from, sl_u8 to) {
    gs_os_char f[GS_OS_PATH_MAX], t[GS_OS_PATH_MAX];
    if (gs_os_native(from, f) < 0 || gs_os_native(to, t) < 0) return 0;
#ifdef _WIN32
    return MoveFileExW(f, t, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (rename(f, t) != 0) return 0;
    gs_os_sync_dir(t);
    return 1;
#endif
}

static uint64_t gs_os_random_u64(void);

/* Writes data to a new file beside path, flushes it to the disk and renames
   it over path, so that path holds either its old contents or all of the
   new ones whatever happens meanwhile; on a failure the new file is removed.
   Its name is path's with a random tag and ".tmp" added, created only if
   nothing has that name already: one that does belongs to someone else, and
   another tag is drawn. */
static uint8_t gs_os_write_file_atomic(sl_u8 path, sl_u8 data) {
    gs_os_char p[GS_OS_PATH_MAX], t[GS_OS_PATH_MAX + 21];
    int n = gs_os_native(path, p);
    if (n <= 0) return 0;
    const uint8_t *d = data.data;
    int64_t left = data.len;
    for (int tries = 0; tries < 8; tries++) {
        uint64_t tag = gs_os_random_u64();
        int k = n;
        memcpy(t, p, (size_t)n * sizeof *t);
        t[k++] = '.';
        for (int s = 60; s >= 0; s -= 4) t[k++] = "0123456789abcdef"[(tag >> s) & 15];
        t[k++] = '.';
        t[k++] = 't';
        t[k++] = 'm';
        t[k++] = 'p';
        t[k] = 0;
        int ok = 1;
#ifdef _WIN32
        HANDLE h = CreateFileW(t, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS) continue;
            return 0;
        }
        while (ok && left > 0) {
            DWORD chunk = left > (1 << 30) ? (DWORD)1 << 30 : (DWORD)left, wrote = 0;
            ok = WriteFile(h, d, chunk, &wrote, NULL) && wrote == chunk;
            d += chunk;
            left -= chunk;
        }
        ok = ok && FlushFileBuffers(h);
        if (!CloseHandle(h)) ok = 0;
        ok = ok && MoveFileExW(t, p, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        if (!ok) DeleteFileW(t);
#else
        int fd = open(t, O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd < 0) {
            if (errno == EEXIST) continue;
            return 0;
        }
        while (ok && left > 0) {
            ssize_t w = write(fd, d, left > (1 << 30) ? (size_t)1 << 30 : (size_t)left);
            if (w < 0 && errno == EINTR) continue;
            ok = w > 0;
            if (ok) {
                d += w;
                left -= w;
            }
        }
        ok = ok && gs_os_fsync(fd);
        if (close(fd) != 0) ok = 0;
        ok = ok && rename(t, p) == 0;
        if (ok) gs_os_sync_dir(p);
        else unlink(t);
#endif
        return ok ? 1 : 0;
    }
    return 0;
}

/* True where path is a directory afterwards, one that was there already
   included. */
static uint8_t gs_os_make_dir(sl_u8 path) {
    gs_os_char p[GS_OS_PATH_MAX];
    if (gs_os_native(path, p) < 0) return 0;
#ifdef _WIN32
    if (CreateDirectoryW(p, NULL)) return 1;
#else
    if (mkdir(p, 0777) == 0) return 1;
#endif
    return gs_os_kind(p) == 2;
}

static uint8_t gs_os_is_dir(sl_u8 path) {
    gs_os_char p[GS_OS_PATH_MAX];
    return gs_os_native(path, p) >= 0 && gs_os_kind(p) == 2;
}

/* An empty directory. */
static uint8_t gs_os_remove_dir(sl_u8 path) {
    gs_os_char p[GS_OS_PATH_MAX];
    if (gs_os_native(path, p) < 0) return 0;
#ifdef _WIN32
    return RemoveDirectoryW(p) != 0;
#else
    return rmdir(p) == 0;
#endif
}

/* The names a listing collects before sorting them: NUL-terminated, one
   after another. */
typedef struct {
    char *text;
    size_t len, cap, count;
} gs_os_names;

/* A name with a newline in it cannot be a line of the listing, and is left
   out. */
static int gs_os_names_add(gs_os_names *ns, const char *name, size_t n) {
    if (memchr(name, '\n', n)) return 1;
    if (ns->len + n + 1 > ns->cap) {
        size_t cap = ns->cap ? ns->cap * 2 : 4096;
        while (cap < ns->len + n + 1) cap *= 2;
        char *text = (char *)realloc(ns->text, cap);
        if (!text) return 0;
        ns->text = text;
        ns->cap = cap;
    }
    memcpy(ns->text + ns->len, name, n);
    ns->text[ns->len + n] = 0;
    ns->len += n + 1;
    ns->count++;
    return 1;
}

static int gs_os_name_order(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* Appends the names in bytewise order, each followed by a newline. */
static int gs_os_names_put(gs_os_names *ns, gs_rref out) {
    if (!ns->count) return 1;
    const char **v = (const char **)malloc(ns->count * sizeof *v);
    if (!v) return 0;
    size_t i = 0;
    for (size_t at = 0; at < ns->len; at += strlen(ns->text + at) + 1) v[i++] = ns->text + at;
    qsort(v, ns->count, sizeof *v, gs_os_name_order);
    for (i = 0; i < ns->count; i++) {
        gs_bld_append(out, v[i], (int64_t)strlen(v[i]));
        gs_bld_append(out, "\n", 1);
    }
    free(v);
    return 1;
}

/* Appends the name of every entry of a directory but "." and "..", sorted
   so that a listing does not depend on the file system; on a failure
   appends nothing. */
static uint8_t gs_os_list_dir(sl_u8 path, gs_rref out) {
    gs_os_char p[GS_OS_PATH_MAX + 2];
    int n = gs_os_native(path, p);
    if (n <= 0) return 0;
    gs_os_names ns = { 0 };
    int ok = 1;
#ifdef _WIN32
    /* FindFirstFileW takes a pattern, which "*" after the directory makes
       every entry. */
    int dirlen = n;
    if (p[n - 1] != '/' && p[n - 1] != '\\' && p[n - 1] != ':') p[n++] = '\\';
    p[n++] = '*';
    p[n] = 0;
    WIN32_FIND_DATAW e;
    HANDLE h = FindFirstFileW(p, &e);
    if (h == INVALID_HANDLE_VALUE) {
        /* No entry at all, not even "." and "..": the empty root of a drive. */
        int empty = GetLastError() == ERROR_FILE_NOT_FOUND;
        p[dirlen] = 0;
        return empty && gs_os_kind(p) == 2;
    }
    do {
        const wchar_t *w = e.cFileName;
        if (w[0] == '.' && (!w[1] || (w[1] == '.' && !w[2]))) continue;
        /* A name that is not valid UTF-16 has no UTF-8 spelling, and is left
           out. */
        char name[1024];
        int k = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, w, -1, name, sizeof name,
                                    NULL, NULL);
        if (k > 1 && !gs_os_names_add(&ns, name, (size_t)k - 1)) ok = 0;
    } while (ok && FindNextFileW(h, &e));
    if (ok && GetLastError() != ERROR_NO_MORE_FILES) ok = 0;
    FindClose(h);
#else
    DIR *d = opendir(p);
    if (!d) return 0;
    for (;;) {
        errno = 0;
        struct dirent *e = readdir(d);
        if (!e) {
            ok = errno == 0;
            break;
        }
        const char *w = e->d_name;
        if (w[0] == '.' && (!w[1] || (w[1] == '.' && !w[2]))) continue;
        if (!gs_os_names_add(&ns, w, strlen(w))) {
            ok = 0;
            break;
        }
    }
    closedir(d);
#endif
    ok = ok && gs_os_names_put(&ns, out);
    free(ns.text);
    return ok ? 1 : 0;
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
    const char *a = gs_argv[i];
#ifdef _WIN32
    /* main's argv is in the ANSI code page. The program gets it in UTF-8,
       the encoding of its paths, by way of UTF-16, which holds every
       character of the code page. */
    int n = MultiByteToWideChar(CP_ACP, 0, a, -1, NULL, 0);
    wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof *w) : NULL;
    int ok = w && MultiByteToWideChar(CP_ACP, 0, a, -1, w, n) == n &&
             gs_os_append_wide(out, w, n - 1);
    free(w);
    if (ok) return;
#endif
    gs_bld_append(out, a, (int64_t)strlen(a));
}

static uint8_t gs_os_getenv(sl_u8 name, gs_rref out) {
    gs_os_char nm[GS_OS_PATH_MAX];
    if (gs_os_native(name, nm) < 0) return 0;
#ifdef _WIN32
    /* The environment holds UTF-16, which getenv would hand out in the ANSI
       code page. A value can change size between the calls. */
    wchar_t small[512], *v = small;
    DWORD cap = 512, len;
    for (;;) {
        SetLastError(0);
        len = GetEnvironmentVariableW(nm, v, cap);
        if (len < cap) break;
        if (v != small) free(v);
        cap = len;
        v = (wchar_t *)malloc(cap * sizeof *v);
        if (!v) return 0;
    }
    /* Zero is also the length of an empty value. */
    int ok = (len > 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND) &&
             gs_os_append_wide(out, v, (int)len);
    if (v != small) free(v);
    return ok ? 1 : 0;
#else
    const char *v = getenv(nm);
    if (!v) return 0;
    gs_bld_append(out, v, (int64_t)strlen(v));
    return 1;
#endif
}

/* Wall-clock time in nanoseconds since the Unix epoch. */
static int64_t gs_os_time_ns(void) {
    /* Neither branch uses C11's timespec_get: the Microsoft C runtime tcc
       links against does not have it, and glibc hides it from a compiler
       announcing C99, which tcc also is. */
#ifdef _WIN32
    /* Windows counts 100 ns ticks from 1601; the offset to the Unix epoch is
       a constant. */
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (int64_t)(t - 116444736000000000ull) * 100;
#else
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#endif
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
