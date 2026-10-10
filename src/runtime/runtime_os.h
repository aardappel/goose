/* Goose runtime: the OS primitives behind stdlib/os.goose (spec §7.10), as
   runtime_ext.h declares them, which this follows. */

#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include <sys/wait.h>
#include <poll.h>
#include <signal.h>
#endif

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

/* Moves `from` to `to`, replacing `to` if there is one, in one step.
   Another process, such as a virus scanner reading a file just written, can
   hold either open for a moment: while `to` is open anywhere it cannot be
   replaced (ERROR_ACCESS_DENIED), nor `from` moved while it is open without
   delete sharing (ERROR_SHARING_VIOLATION). Those failures are tried again
   for about a second, unless `to` is a directory or read-only, which no
   wait changes. */
static int gs_os_replace(const wchar_t *from, const wchar_t *to) {
    for (DWORD wait = 0;; wait = wait ? wait * 2 : 1) {
        if (MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 1;
        DWORD e = GetLastError(), a = GetFileAttributesW(to);
        if ((e != ERROR_ACCESS_DENIED && e != ERROR_SHARING_VIOLATION) || wait > 512 ||
            (a != INVALID_FILE_ATTRIBUTES && (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_READONLY))))
            return 0;
        Sleep(wait);
    }
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

GS_API uint8_t gs_os_read_file(sl_u8 path, gs_rref out) {
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

GS_API uint8_t gs_os_write_file(sl_u8 path, sl_u8 data) { return gs_os_put_file(path, data, 0); }

GS_API uint8_t gs_os_append_file(sl_u8 path, sl_u8 data) { return gs_os_put_file(path, data, 1); }

GS_API uint8_t gs_os_file_exists(sl_u8 path) {
    gs_os_char p[GS_OS_PATH_MAX];
    return gs_os_native(path, p) >= 0 && gs_os_kind(p) == 1;
}

GS_API uint8_t gs_os_remove_file(sl_u8 path) {
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
GS_API uint8_t gs_os_rename_file(sl_u8 from, sl_u8 to) {
    gs_os_char f[GS_OS_PATH_MAX], t[GS_OS_PATH_MAX];
    if (gs_os_native(from, f) < 0 || gs_os_native(to, t) < 0) return 0;
#ifdef _WIN32
    return gs_os_replace(f, t);
#else
    if (rename(f, t) != 0) return 0;
    gs_os_sync_dir(t);
    return 1;
#endif
}

/* Writes data to a new file beside path, flushes it to the disk and renames
   it over path, so that path holds either its old contents or all of the
   new ones whatever happens meanwhile; on a failure the new file is removed.
   Its name is path's with a random tag and ".tmp" added, created only if
   nothing has that name already: one that does belongs to someone else, and
   another tag is drawn. */
GS_API uint8_t gs_os_write_file_atomic(sl_u8 path, sl_u8 data) {
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
        ok = ok && gs_os_replace(t, p);
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
GS_API uint8_t gs_os_make_dir(sl_u8 path) {
    gs_os_char p[GS_OS_PATH_MAX];
    if (gs_os_native(path, p) < 0) return 0;
#ifdef _WIN32
    if (CreateDirectoryW(p, NULL)) return 1;
#else
    if (mkdir(p, 0777) == 0) return 1;
#endif
    return gs_os_kind(p) == 2;
}

GS_API uint8_t gs_os_is_dir(sl_u8 path) {
    gs_os_char p[GS_OS_PATH_MAX];
    return gs_os_native(path, p) >= 0 && gs_os_kind(p) == 2;
}

/* An empty directory. */
GS_API uint8_t gs_os_remove_dir(sl_u8 path) {
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
GS_API uint8_t gs_os_list_dir(sl_u8 path, gs_rref out) {
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

GS_API void gs_os_write_stdout(sl_u8 s) {
    if (s.len > 0) fwrite(s.data, 1, (size_t)s.len, stdout);
}

GS_API void gs_os_write_stderr(sl_u8 s) {
    if (s.len > 0) fwrite(s.data, 1, (size_t)s.len, stderr);
}

GS_API void gs_os_flush_stdout(void) { fflush(stdout); }

/* One line of stdin, without its newline; false at end of input. */
GS_API uint8_t gs_os_read_line(gs_rref out) {
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
GS_API void gs_os_binary_stdio(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

GS_API uint8_t gs_os_read_stdin_bytes(int64_t count, gs_rref out) {
    uint8_t buf[65536];
    if (count < 0) return 0;
    while (count > 0) {
        size_t want = count < (int64_t)sizeof buf ? (size_t)count : sizeof buf;
        size_t n = fread(buf, 1, want, stdin);
        if (n == 0) return 0;
        gs_bld_append(out, buf, (int64_t)n);
        count -= (int64_t)n;
    }
    return 1;
}

GS_API void gs_os_read_stdin(gs_rref out) {
    uint8_t buf[65536];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, stdin);
        if (n == 0) break;
        gs_bld_append(out, buf, (int64_t)n);
    }
}

GS_API int64_t gs_os_arg_count(void) { return gs_argc; }

/* Application resources, independent of cwd and argv[0]. JIT supplies its
   entry source directory; AOT discovers the running executable at runtime.
   Append only on success, including a final path separator. */
GS_API uint8_t gs_os_resource_dir(gs_rref out) {
#ifdef GS_JIT_RESOURCE_DIR
    const char *dir = GS_JIT_RESOURCE_DIR;
    gs_bld_append(out, dir, (int64_t)strlen(dir));
    return 1;
#elif defined(_WIN32)
    wchar_t path[GS_OS_PATH_MAX];
    DWORD n = GetModuleFileNameW(NULL, path, GS_OS_PATH_MAX);
    if (!n || n >= GS_OS_PATH_MAX) return 0;
    while (n && path[n - 1] != L'\\' && path[n - 1] != L'/') --n;
    return n && gs_os_append_wide(out, path, (int)n);
#else
    char path[GS_OS_PATH_MAX];
    size_t n;
    #ifdef __APPLE__
        uint32_t capacity = sizeof path;
        if (_NSGetExecutablePath(path, &capacity) != 0) return 0;
        /* Resolve the loader's possible relative path and symlinks. */
        char *resolved = realpath(path, NULL);
        if (!resolved) return 0;
        n = strlen(resolved);
        if (n >= sizeof path) { free(resolved); return 0; }
        memcpy(path, resolved, n + 1);
        free(resolved);
    #elif defined(__linux__)
        ssize_t count = readlink("/proc/self/exe", path, sizeof path);
        if (count <= 0 || (size_t)count >= sizeof path) return 0;
        n = (size_t)count;
    #else
        return 0;
    #endif
    while (n && path[n - 1] != '/') --n;
    if (!n) return 0;
    #ifdef __APPLE__
        const char suffix[] = ".app/Contents/MacOS/";
        size_t suffixlen = sizeof suffix - 1;
        if (n >= suffixlen && !memcmp(path + n - suffixlen, suffix, suffixlen)) {
            n -= sizeof "MacOS/" - 1;
            if (n + sizeof "Resources/" - 1 >= sizeof path) return 0;
            memcpy(path + n, "Resources/", sizeof "Resources/" - 1);
            n += sizeof "Resources/" - 1;
        }
    #endif
    gs_bld_append(out, path, (int64_t)n);
    return 1;
#endif
}

GS_API void gs_os_arg(int64_t i, gs_rref out) {
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

GS_API uint8_t gs_os_working_directory(gs_rref out) {
#ifdef _WIN32
    wchar_t buf[GS_OS_PATH_MAX];
    DWORD len = GetCurrentDirectoryW(GS_OS_PATH_MAX, buf);
    return len > 0 && len < GS_OS_PATH_MAX && gs_os_append_wide(out, buf, (int)len);
#else
    char *path = getcwd(NULL, 0);
    if (!path) return 0;
    gs_bld_append(out, path, (int64_t)strlen(path));
    free(path);
    return 1;
#endif
}

GS_API uint8_t gs_os_getenv(sl_u8 name, gs_rref out) {
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
GS_API int64_t gs_os_time_ns(void) {
    /* Neither branch uses C11's timespec_get: the Microsoft C runtime tcc
       links against does not have it, and glibc hides it from a compiler
       announcing C99, which tcc also is. */
#ifdef _WIN32
    /* Windows counts 100 ns ticks from 1601. The offset to the Unix epoch is
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
GS_API int64_t gs_os_clock_ns(void) {
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

GS_API void gs_os_sleep_ms(int64_t ms) {
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

GS_API uint64_t gs_os_random_u64(void) {
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

/* Runs a program without a shell. argv is the program and its arguments
   joined by NUL bytes. Everything the program writes to standard output and
   standard error is appended to out, up to GS_OS_RUN_MAX bytes. The rest is
   read and dropped. Its standard input is empty. A timeout_ms of zero or
   less means no limit. Returns the exit status, 128 plus the signal number
   if a signal killed it, -1 if it could not be started, and -2 if it was
   killed for running longer than timeout_ms. */
#define GS_OS_RUN_MAX (64 * 1024 * 1024)
#define GS_OS_RUN_ARGS 256

GS_API int64_t gs_os_run(sl_u8 argv, int64_t timeout_ms, gs_rref out) {
    if (argv.len <= 0 || argv.len > 32768) return -1;
    int64_t kept = 0;
#ifdef _WIN32
    /* Build one UTF-16 command line. Each argument is quoted so the C runtime
       and CommandLineToArgvW read it back unchanged. */
    wchar_t *line = (wchar_t *)malloc(((size_t)argv.len * 4 + 8) * sizeof *line);
    if (!line) return -1;
    size_t n = 0;
    int64_t start = 0;
    for (int64_t i = 0; i <= argv.len; i++) {
        if (i < argv.len && argv.data[i]) continue;
        int64_t len = i - start;
        wchar_t *w = (wchar_t *)malloc(((size_t)len + 1) * sizeof *w);
        if (!w) { free(line); return -1; }
        int wn = 0;
        if (len > 0) {
            wn = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char *)argv.data + start,
                                     (int)len, w, (int)len);
            if (wn == 0) { free(w); free(line); return -1; }
        }
        if (n) line[n++] = L' ';
        line[n++] = L'"';
        int slashes = 0;
        for (int k = 0; k < wn; k++) {
            if (w[k] == L'\\') { slashes++; line[n++] = w[k]; continue; }
            if (w[k] == L'"') { for (; slashes > 0; slashes--) line[n++] = L'\\'; line[n++] = L'\\'; }
            slashes = 0;
            line[n++] = w[k];
        }
        for (; slashes > 0; slashes--) line[n++] = L'\\';
        line[n++] = L'"';
        free(w);
        start = i + 1;
    }
    line[n] = 0;
    SECURITY_ATTRIBUTES sa = { sizeof sa, NULL, TRUE };
    HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) { free(line); return -1; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si;
    memset(&si, 0, sizeof si);
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = nul;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi;
    BOOL started = CreateProcessW(NULL, line, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si,
                                  &pi);
    free(line);
    CloseHandle(wr);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!started) { CloseHandle(rd); return -1; }
    CloseHandle(pi.hThread);
    int64_t deadline = timeout_ms > 0 ? (int64_t)GetTickCount64() + timeout_ms : 0;
    int timed_out = 0;
    for (;;) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL)) break; /* the writer is gone */
        if (avail) {
            uint8_t buf[65536];
            DWORD got = 0;
            if (!ReadFile(rd, buf, avail < sizeof buf ? avail : (DWORD)sizeof buf, &got, NULL) || !got)
                break;
            int64_t take = GS_OS_RUN_MAX - kept < (int64_t)got ? GS_OS_RUN_MAX - kept : (int64_t)got;
            if (take > 0) { gs_bld_append(out, buf, take); kept += take; }
            continue;
        }
        if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0) {
            /* Exited. Read once more for any last output. */
            if (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) || !avail) break;
            continue;
        }
        if (deadline && (int64_t)GetTickCount64() >= deadline) { timed_out = 1; break; }
        Sleep(5);
    }
    CloseHandle(rd);
    if (timed_out) TerminateProcess(pi.hProcess, 1);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return timed_out ? -2 : (int64_t)(int32_t)code;
#else
    char *text = (char *)malloc((size_t)argv.len + 1);
    char *args[GS_OS_RUN_ARGS + 1];
    if (!text) return -1;
    memcpy(text, argv.data, (size_t)argv.len);
    text[argv.len] = 0;
    int count = 0;
    char *at = text;
    for (;;) {
        if (count == GS_OS_RUN_ARGS) { free(text); return -1; }
        args[count++] = at;
        char *end = (char *)memchr(at, 0, (size_t)(text + argv.len - at));
        if (!end) break;
        at = end + 1;
    }
    args[count] = NULL;
    int data[2], status[2];
    if (pipe(data) != 0) { free(text); return -1; }
    if (pipe(status) != 0) { close(data[0]); close(data[1]); free(text); return -1; }
    fcntl(status[1], F_SETFD, FD_CLOEXEC);
    fcntl(data[0], F_SETFD, FD_CLOEXEC);
    fflush(NULL);
    pid_t pid = fork();
    if (pid < 0) {
        close(data[0]); close(data[1]); close(status[0]); close(status[1]); free(text);
        return -1;
    }
    if (pid == 0) {
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, 0); if (devnull > 2) close(devnull); }
        dup2(data[1], 1);
        dup2(data[1], 2);
        if (data[1] > 2) close(data[1]);
        close(data[0]);
        execvp(args[0], args);
        /* exec failed. Send errno through the status pipe. After a successful
           exec the pipe closes by itself, which is how the parent tells. */
        int e = errno;
        ssize_t w = write(status[1], &e, sizeof e);
        (void)w;
        _exit(127);
    }
    free(text);
    close(data[1]);
    close(status[1]);
    int child_errno = 0;
    ssize_t got = 0;
    do { got = read(status[0], &child_errno, sizeof child_errno); } while (got < 0 && errno == EINTR);
    close(status[0]);
    int failed_start = got > 0;
    int timed_out = 0;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    while (!failed_start) {
        int wait_ms = -1;
        if (timeout_ms > 0) {
            struct timespec t1;
            clock_gettime(CLOCK_MONOTONIC, &t1);
            int64_t spent = (int64_t)(t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
            if (spent >= timeout_ms) { timed_out = 1; break; }
            wait_ms = (int)(timeout_ms - spent);
        }
        struct pollfd pfd = { data[0], POLLIN, 0 };
        int ready = poll(&pfd, 1, wait_ms);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if (ready == 0) continue;
        uint8_t buf[65536];
        ssize_t r = read(data[0], buf, sizeof buf);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) break;
        int64_t take = GS_OS_RUN_MAX - kept < (int64_t)r ? GS_OS_RUN_MAX - kept : (int64_t)r;
        if (take > 0) { gs_bld_append(out, buf, take); kept += take; }
    }
    close(data[0]);
    if (timed_out) kill(pid, SIGKILL);
    int wstatus = 0;
    pid_t waited;
    do { waited = waitpid(pid, &wstatus, 0); } while (waited < 0 && errno == EINTR);
    if (failed_start) return -1;
    if (timed_out) return -2;
    if (waited < 0) return -1;
    if (WIFEXITED(wstatus)) return WEXITSTATUS(wstatus);
    if (WIFSIGNALED(wstatus)) return 128 + WTERMSIG(wstatus);
    return -1;
#endif
}
