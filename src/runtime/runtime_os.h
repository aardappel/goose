/* Goose runtime: the OS primitives behind stdlib/os.goose (spec §7.10),
   and std's slice_pool's span tree, as runtime_ext.h declares them, which
   this follows. */

#include <time.h>
#ifndef _WIN32
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <dlfcn.h>
#include <sys/stat.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
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

/* Appends the whole file; on a failure, nothing. */
GS_API uint8_t gs_os_read_file(sl_u8 path, gs_rref out) {
    gs_os_char p[GS_OS_PATH_MAX];
    if (gs_os_native(path, p) < 0) return 0;
    FILE *f = gs_os_fopen(p, "rb");
    if (!f) return 0;
    int64_t len = out.hdr->len;
    uint8_t *top = out.stk->top;
    uint8_t buf[65536];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, f);
        if (n == 0) break;
        gs_bld_append(out, buf, (int64_t)n);
    }
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        out.hdr->len = len;
        out.stk->top = top;
        return 0;
    }
    return 1;
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
        /* The permissions of the file it replaces, which write_file leaves
           as they are, rather than the umask's. */
        struct stat st;
        if (stat(p, &st) == 0) ok = fchmod(fd, st.st_mode & 0777) == 0;
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

/* fgetc takes the stream's lock for every byte; read_line takes it once
   per line and reads the bytes without it. */
#ifdef _WIN32
#define gs_os_lock_stdin() _lock_file(stdin)
#define gs_os_unlock_stdin() _unlock_file(stdin)
#define gs_os_getc_stdin() _getc_nolock(stdin)
#else
#define gs_os_lock_stdin() flockfile(stdin)
#define gs_os_unlock_stdin() funlockfile(stdin)
#define gs_os_getc_stdin() getc_unlocked(stdin)
#endif

/* One line of stdin, without its newline or a carriage return before it;
   false at end of input. */
GS_API uint8_t gs_os_read_line(gs_rref out) {
    gs_os_lock_stdin();
    int c = gs_os_getc_stdin();
    if (c == EOF) { gs_os_unlock_stdin(); return 0; }
    int64_t start = out.hdr->len;
    while (c != EOF && c != '\n') {
        uint8_t b = (uint8_t)c;
        gs_bld_append(out, &b, 1);
        c = gs_os_getc_stdin();
    }
    gs_os_unlock_stdin();
    if (c == '\n' && out.hdr->len > start && out.hdr->base[out.hdr->len - 1] == '\r') {
        out.hdr->len--;
        out.stk->top--;
    }
    return 1;
}

/* All of stdin. */
GS_API void gs_os_read_stdin(gs_rref out) {
    uint8_t buf[65536];
    for (;;) {
        size_t n = fread(buf, 1, sizeof buf, stdin);
        if (n == 0) break;
        gs_bld_append(out, buf, (int64_t)n);
    }
}

GS_API int64_t gs_os_arg_count(void) { return gs_argc; }

/* The application's resource directory (resource_dir(), below) in native
   form with its final separator, NUL-terminated in buf (GS_OS_PATH_MAX
   characters): its length, or 0 where it is not known. JIT supplies its
   entry source directory; AOT discovers the running executable. */
static int gs_os_resource_native(gs_os_char *buf) {
#ifdef GS_JIT_RESOURCE_DIR
    sl_u8 dir = { (uint8_t *)GS_JIT_RESOURCE_DIR, (int64_t)strlen(GS_JIT_RESOURCE_DIR) };
    int n = gs_os_native(dir, buf);
    return n < 0 ? 0 : n;
#elif defined(_WIN32)
    DWORD n = GetModuleFileNameW(NULL, buf, GS_OS_PATH_MAX);
    if (!n || n >= GS_OS_PATH_MAX) return 0;
    while (n && buf[n - 1] != L'\\' && buf[n - 1] != L'/') --n;
    buf[n] = 0;
    return (int)n;
#else
    char *path = buf;
    size_t n;
    #ifdef __APPLE__
        uint32_t capacity = GS_OS_PATH_MAX;
        if (_NSGetExecutablePath(path, &capacity) != 0) return 0;
        /* Resolve the loader's possible relative path and symlinks. */
        char *resolved = realpath(path, NULL);
        if (!resolved) return 0;
        n = strlen(resolved);
        if (n >= (size_t)GS_OS_PATH_MAX) { free(resolved); return 0; }
        memcpy(path, resolved, n + 1);
        free(resolved);
    #elif defined(__linux__)
        ssize_t count = readlink("/proc/self/exe", path, (size_t)GS_OS_PATH_MAX);
        if (count <= 0 || (size_t)count >= (size_t)GS_OS_PATH_MAX) return 0;
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
            if (n + sizeof "Resources/" - 1 >= (size_t)GS_OS_PATH_MAX) return 0;
            memcpy(path + n, "Resources/", sizeof "Resources/" - 1);
            n += sizeof "Resources/" - 1;
        }
    #endif
    path[n] = 0;
    return (int)n;
#endif
}

/* Application resources, independent of cwd and argv[0]. Append only on
   success, including a final path separator. */
GS_API uint8_t gs_os_resource_dir(gs_rref out) {
    gs_os_char path[GS_OS_PATH_MAX];
    int n = gs_os_resource_native(path);
    if (!n) return 0;
#ifdef _WIN32
    return gs_os_append_wide(out, path, n);
#else
    gs_bld_append(out, path, (int64_t)n);
    return 1;
#endif
}

/* ---- Dynamic externs: the libraries the program's extern blocks name ---- */

/* The spelling this platform opens for a library, UTF-8 in buf (GS_OS_PATH_MAX
   bytes): the sidecar's for the platform, else the block's name. A last
   component without a dot takes the platform's library form (SDL2 is
   SDL2.dll, libSDL2.dylib, libSDL2.so); a spelling with a separator, unless
   absolute, is under the resource directory; a bare one goes to the loader's
   search as it is. 0 where it does not fit or the resource directory is not
   known. */
static int gs_dl_spelling(const gs_dllib *lib, char *buf) {
#ifdef _WIN32
    const char *s = lib->onwindows, *prefix = "", *suffix = ".dll";
#elif defined(__APPLE__)
    const char *s = lib->onmacos, *prefix = "lib", *suffix = ".dylib";
#else
    const char *s = lib->onlinux, *prefix = "lib", *suffix = ".so";
#endif
    if (!s) s = lib->name;
    size_t len = strlen(s), dir = 0;
    for (size_t i = 0; i < len; i++) {
        int sep = s[i] == '/';
#ifdef _WIN32
        sep = sep || s[i] == '\\';
#endif
        if (sep) dir = i + 1;
    }
    const char *leaf = s + dir;
    int bare = !strchr(leaf, '.');
    int absolute = s[0] == '/';
#ifdef _WIN32
    int drive = (s[0] >= 'a' && s[0] <= 'z') || (s[0] >= 'A' && s[0] <= 'Z');
    absolute = absolute || s[0] == '\\' || (len >= 2 && drive && s[1] == ':');
#endif
    char root[GS_OS_PATH_MAX];
    root[0] = 0;
    if (dir && !absolute) {
        gs_os_char native[GS_OS_PATH_MAX];
        int n = gs_os_resource_native(native);
        if (!n) return 0;
#ifdef _WIN32
        n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native, n, root, GS_OS_PATH_MAX - 1,
                                NULL, NULL);
        if (n <= 0) return 0;
        root[n] = 0;
#else
        memcpy(root, native, (size_t)n + 1);
#endif
    }
    int n = snprintf(buf, GS_OS_PATH_MAX, "%s%.*s%s%s%s", root, (int)dir, s, bare ? prefix : "",
                     leaf, bare ? suffix : "");
    return n > 0 && n < GS_OS_PATH_MAX ? n : 0;
}

GS_API void gs_dl_bind(const gs_dllib *libs, int64_t nlibs, const gs_dlbind *binds,
                       int64_t nbinds) {
    for (int64_t l = 0; l < nlibs; l++) {
        char spelling[GS_OS_PATH_MAX], msg[GS_OS_PATH_MAX + 512];
        if (!gs_dl_spelling(&libs[l], spelling)) {
            snprintf(msg, sizeof msg, "cannot load %s: its path does not fit or is not known",
                     libs[l].name);
            gs_panic(msg);
        }
#ifdef _WIN32
        sl_u8 utf8 = { (uint8_t *)spelling, (int64_t)strlen(spelling) };
        wchar_t wide[GS_OS_PATH_MAX];
        HMODULE h = NULL;
        if (gs_os_native(utf8, wide) >= 0) {
            /* A path, the resource directory's or an absolute one: the
               libraries it depends on are looked for beside it first, as
               a library shipped with its dependencies under lib/ needs.
               That search wants backslashes. A bare name keeps the
               loader's own search. */
            int path = 0;
            for (wchar_t *c = wide; *c; c++)
                if (*c == L'/' || *c == L'\\') { *c = L'\\'; path = 1; }
            h = path ? LoadLibraryExW(wide, NULL, LOAD_WITH_ALTERED_SEARCH_PATH) : LoadLibraryW(wide);
        }
        if (!h) {
            snprintf(msg, sizeof msg, "cannot load %s (%s): Windows error %u", libs[l].name,
                     spelling, (unsigned)GetLastError());
            gs_panic(msg);
        }
#else
        void *h = dlopen(spelling, RTLD_NOW | RTLD_LOCAL);
        if (!h) {
            const char *why = dlerror();
            snprintf(msg, sizeof msg, "cannot load %s (%s): %s", libs[l].name, spelling,
                     why ? why : "unknown error");
            gs_panic(msg);
        }
#endif
        for (int64_t b = 0; b < nbinds; b++) {
            if (binds[b].lib != l) continue;
#ifdef _WIN32
            void *f = (void *)GetProcAddress(h, binds[b].sym);
#else
            void *f = dlsym(h, binds[b].sym);
#endif
            if (!f) {
                snprintf(msg, sizeof msg, "%s (%s) has no symbol %s", libs[l].name, spelling,
                         binds[b].sym);
                gs_panic(msg);
            }
            *binds[b].slot = f;
        }
    }
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
    /* Sleep takes 32 bits, the largest of them meaning INFINITE: a longer
       sleep goes in parts. */
    while (ms > 0) {
        DWORD part = ms > 0x7fffffff ? 0x7fffffff : (DWORD)ms;
        Sleep(part);
        ms -= part;
    }
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
    /* Which call this is, counted atomically: workers calling at once each
       get a count of their own. */
#ifdef _WIN32
    static volatile LONG64 counter;
    uint64_t count = (uint64_t)InterlockedIncrement64(&counter);
#else
    static uint64_t counter;
    uint64_t count = __atomic_add_fetch(&counter, 1, __ATOMIC_SEQ_CST);
#endif
    uint64_t seed = (uint64_t)gs_os_time_ns();
    seed = gs_os_mix64(seed ^ (uint64_t)gs_os_clock_ns());
#ifdef _WIN32
    seed = gs_os_mix64(seed ^ (uint64_t)GetCurrentProcessId());
#endif
    seed = gs_os_mix64(seed ^ (uint64_t)(uintptr_t)&counter);
    return gs_os_mix64(seed ^ count);
}

/* ---------------------------------------------------------------------------
   std's slice_pool (stdlib/slice_pool_internal.goose) keeps the span tree
   of runtime_impl.h's gs_spans_* in nodes of its own and a `tree` field for
   the header, both of which the program can write. These are those
   functions, taking the nodes as its externs pass them and the header
   apart from them, as `hd`, and with everything read from the tree checked
   before it is used: a node number must be one of the nodes past node 0
   (GS_SPBADNUM), an entry count must fit the node it is read from as a leaf
   or inner node, a height must fit a path, and an entry moved into or out
   of a node must be one it has room for or holds. A node reached at two
   levels of a path, or one the chain hands out while it is in the tree,
   keeps every access inside the nodes: each count these write fits a leaf,
   and the counts that index an inner node's children are checked where
   they are read. Sums of stored indices and counts wrap. A check that fails
   aborts (GS_E_SPANTREE); a wrong tree that passes them hands out wrong
   spans, which slice_pool checks against its length. New nodes start
   zeroed, as a pushed `node { .. }` would. reusable[]'s freelist is out of
   the program's reach, so it keeps the unchecked functions, and this copy
   goes when reusable[] does. It is here, not beside them, because the
   externs take a gs_rref, which runtime_ext.h defines. */

typedef struct {
    gs_sphead *hd;
    uint8_t *base;
    int64_t *n;
    uint8_t **top;
    const gs_loc *at;    /* The call that a corrupt tree is reported at. */
    int64_t h, node[GS_SPAN_DEPTH], pos[GS_SPAN_DEPTH];
} gs_spcpath;

#define GS_SPCHEAD(p) ((p)->hd)
#define GS_SPSUM(a, b) ((int64_t)((uint64_t)(a) + (uint64_t)(b)))

/* Macros rather than functions, which TinyCC would call at every level. */
#define GS_SPBADNUM(p, i) ((uint64_t)(i) - 1 >= (uint64_t)*(p)->n - 1)
#define GS_SPBADIN(p, i) (GS_SPBADNUM(p, i) || (uint64_t)GS_SPINNER(p, i)->n - 1 >= GS_SPAN_FAN)
#define GS_SPBADLEAF(p, i) (GS_SPBADNUM(p, i) || (uint64_t)GS_SPLEAF(p, i)->n > GS_SPAN_LEAF)
#define GS_SPBADH(h) ((uint64_t)(h) >= GS_SPAN_DEPTH)

static GS_NORETURN void gs_spc_corrupt(const gs_spcpath *p) {
    gs_abort(GS_E_SPANTREE, p->at->file, p->at->line);
}

#define GS_SPSTART(p, tree, nodes, loc) \
    ((p)->hd = (gs_sphead *)(tree), (p)->base = (nodes).hdr->base, (p)->n = &(nodes).hdr->len, \
     (p)->top = &(nodes).stk->top, (p)->at = (loc))

static int64_t gs_spc_newnode(gs_spcpath *p) {
    int64_t i = GS_SPCHEAD(p)->chain;
    if (i) {
        if (GS_SPBADNUM(p, i)) gs_spc_corrupt(p);
        GS_SPCHEAD(p)->chain = GS_SPLEAF(p, i)->link;
        return i;
    }
    i = (*p->n)++;
    memset(GS_SPLEAF(p, i), 0, GS_SPAN_NODE);
    *p->top += GS_SPAN_NODE;
    return i;
}

static void gs_spc_dropnode(gs_spcpath *p, int64_t i) {
    GS_SPLEAF(p, i)->link = GS_SPCHEAD(p)->chain;
    GS_SPCHEAD(p)->chain = i;
}

static void gs_spc_open(const gs_spcpath *p, gs_spleaf *lf, int64_t s) {
    int64_t j;
    if ((uint64_t)lf->n >= GS_SPAN_LEAF || (uint64_t)s > (uint64_t)lf->n) gs_spc_corrupt(p);
    for (j = lf->n; j > s; j--) {
        lf->idx[j] = lf->idx[j - 1];
        lf->cnt[j] = lf->cnt[j - 1];
    }
    lf->n++;
}

static void gs_spc_close(const gs_spcpath *p, gs_spleaf *lf, int64_t s) {
    int64_t j;
    if ((uint64_t)s >= (uint64_t)lf->n) gs_spc_corrupt(p);
    lf->n--;
    for (j = s; j < lf->n; j++) {
        lf->idx[j] = lf->idx[j + 1];
        lf->cnt[j] = lf->cnt[j + 1];
    }
}

static void gs_spc_openin(const gs_spcpath *p, gs_spinner *in, int64_t s) {
    int64_t j;
    if ((uint64_t)in->n >= GS_SPAN_FAN || (uint64_t)s > (uint64_t)in->n) gs_spc_corrupt(p);
    for (j = in->n; j > s; j--) {
        in->key[j] = in->key[j - 1];
        in->max[j] = in->max[j - 1];
        in->child[j] = in->child[j - 1];
    }
    in->n++;
}

static void gs_spc_closein(const gs_spcpath *p, gs_spinner *in, int64_t s) {
    int64_t j;
    if (in->n > GS_SPAN_FAN || (uint64_t)s >= (uint64_t)in->n) gs_spc_corrupt(p);
    in->n--;
    for (j = s; j < in->n; j++) {
        in->key[j] = in->key[j + 1];
        in->max[j] = in->max[j + 1];
        in->child[j] = in->child[j + 1];
    }
}

static int64_t gs_spc_max(gs_spcpath *p, int64_t i, int leaf, int64_t stop) {
    return leaf ? gs_sp_most(GS_SPLEAF(p, i)->cnt, GS_SPLEAF(p, i)->n, stop)
                : gs_sp_most(GS_SPINNER(p, i)->max, GS_SPINNER(p, i)->n, stop);
}

static int64_t gs_spc_key(gs_spcpath *p, int64_t i, int leaf) {
    return leaf ? GS_SPLEAF(p, i)->idx[0] : GS_SPINNER(p, i)->key[0];
}

static void gs_spc_fix(gs_spcpath *p, int64_t l, int64_t was, int64_t now) {
    for (; l > 0; l--) {
        gs_spinner *in = GS_SPINNER(p, p->node[l - 1]);
        int64_t k = p->pos[l - 1], c = p->node[l], m = in->max[k];
        int leaf = l == p->h;
        int64_t key = gs_spc_key(p, c, leaf);
        int64_t nm = now >= m ? now : was < m ? m : gs_spc_max(p, c, leaf, was);
        if (in->key[k] == key && nm == m) return;
        in->key[k] = key;
        in->max[k] = nm;
        was = m;
        now = nm;
    }
}

static int64_t gs_spc_find(gs_spcpath *p, int64_t idx) {
    int64_t l, i = GS_SPCHEAD(p)->root, k;
    gs_spleaf *lf;
    p->h = GS_SPCHEAD(p)->height;
    if (GS_SPBADH(p->h)) gs_spc_corrupt(p);
    for (l = 0; l < p->h; l++) {
        gs_spinner *in;
        if (GS_SPBADIN(p, i)) gs_spc_corrupt(p);
        in = GS_SPINNER(p, i);
        for (k = 1; k < in->n && in->key[k] <= idx; k++) {}
        p->node[l] = i;
        p->pos[l] = k - 1;
        i = in->child[k - 1];
    }
    p->node[p->h] = i;
    if (GS_SPBADLEAF(p, i)) gs_spc_corrupt(p);
    lf = GS_SPLEAF(p, i);
    for (k = 0; k < lf->n && lf->idx[k] <= idx; k++) {}
    p->pos[p->h] = k - 1;
    return k - 1;
}

static void gs_spc_newroot(gs_spcpath *p, int64_t a, int64_t b, int leaf) {
    int64_t r = gs_spc_newnode(p);
    gs_spinner *rn = GS_SPINNER(p, r);
    rn->n = 2;
    rn->key[0] = gs_spc_key(p, a, leaf);
    rn->max[0] = gs_spc_max(p, a, leaf, INT64_MAX);
    rn->child[0] = a;
    rn->key[1] = gs_spc_key(p, b, leaf);
    rn->max[1] = gs_spc_max(p, b, leaf, INT64_MAX);
    rn->child[1] = b;
    GS_SPCHEAD(p)->root = r;
    GS_SPCHEAD(p)->height++;
}

static void gs_spc_addchild(gs_spcpath *p, int64_t l, int64_t k, int64_t child, int leaf) {
    for (;;) {
        gs_spinner *in = GS_SPINNER(p, p->node[l]), *o = in;
        int64_t key = gs_spc_key(p, child, leaf), max = gs_spc_max(p, child, leaf, INT64_MAX);
        int64_t s = k, nn = 0;
        if (in->n == GS_SPAN_FAN) {
            int64_t half = GS_SPAN_FAN / 2;
            gs_spinner *r;
            nn = gs_spc_newnode(p);
            if (nn == p->node[l]) gs_spc_corrupt(p);
            r = GS_SPINNER(p, nn);
            r->n = in->n - half;
            memcpy(r->key, in->key + half, (size_t)r->n * 8);
            memcpy(r->max, in->max + half, (size_t)r->n * 8);
            memcpy(r->child, in->child + half, (size_t)r->n * 8);
            in->n = half;
            if (k > half) {
                o = r;
                s = k - half;
            }
        }
        gs_spc_openin(p, o, s);
        o->key[s] = key;
        o->max[s] = max;
        o->child[s] = child;
        if (!nn) {
            gs_spc_fix(p, l, 0, max);
            return;
        }
        if (l == 0) {
            gs_spc_newroot(p, p->node[0], nn, 0);
            return;
        }
        gs_spc_fix(p, l, INT64_MAX, 0);
        child = nn;
        leaf = 0;
        k = p->pos[l - 1] + 1;
        l--;
    }
}

static void gs_spc_insert(gs_spcpath *p, int64_t k, int64_t idx, int64_t cnt) {
    gs_spleaf *lf = GS_SPLEAF(p, p->node[p->h]), *o = lf;
    int64_t s = k, nn = 0;
    if (lf->n == GS_SPAN_LEAF) {
        int64_t half = GS_SPAN_LEAF / 2;
        gs_spleaf *r;
        nn = gs_spc_newnode(p);
        if (nn == p->node[p->h]) gs_spc_corrupt(p);
        r = GS_SPLEAF(p, nn);
        r->n = lf->n - half;
        memcpy(r->idx, lf->idx + half, (size_t)r->n * 8);
        memcpy(r->cnt, lf->cnt + half, (size_t)r->n * 8);
        lf->n = half;
        if (k > half) {
            o = r;
            s = k - half;
        }
    }
    gs_spc_open(p, o, s);
    o->idx[s] = idx;
    o->cnt[s] = cnt;
    if (!nn) {
        gs_spc_fix(p, p->h, 0, cnt);
    } else if (p->h == 0) {
        gs_spc_newroot(p, p->node[0], nn, 1);
    } else {
        gs_spc_fix(p, p->h, INT64_MAX, 0);
        gs_spc_addchild(p, p->h - 1, p->pos[p->h - 1] + 1, nn, 1);
    }
}

static void gs_spc_delete(gs_spcpath *p, int64_t k) {
    gs_spleaf *lf = GS_SPLEAF(p, p->node[p->h]);
    int64_t c = lf->cnt[k], l;
    gs_spc_close(p, lf, k);
    if (lf->n > 0 || p->h == 0) {
        gs_spc_fix(p, p->h, c, 0);
        return;
    }
    gs_spc_dropnode(p, p->node[p->h]);
    for (l = p->h - 1;; l--) {
        gs_spinner *in = GS_SPINNER(p, p->node[l]);
        int64_t e = p->pos[l], m = in->max[e];
        gs_spc_closein(p, in, e);
        if (in->n > 0) {
            gs_spc_fix(p, l, m, 0);
            break;
        }
        /* A root of one child, which the functions never leave. */
        if (l == 0) gs_spc_corrupt(p);
        gs_spc_dropnode(p, p->node[l]);
    }
    while (GS_SPCHEAD(p)->height > 0) {
        int64_t r = GS_SPCHEAD(p)->root;
        if (GS_SPBADNUM(p, r)) gs_spc_corrupt(p);
        if (GS_SPINNER(p, r)->n != 1) break;
        GS_SPCHEAD(p)->root = GS_SPINNER(p, r)->child[0];
        GS_SPCHEAD(p)->height--;
        gs_spc_dropnode(p, r);
    }
}

static void gs_spc_take(gs_spcpath *p, int64_t k, int64_t cnt) {
    gs_spleaf *lf = GS_SPLEAF(p, p->node[p->h]);
    int64_t c = lf->cnt[k];
    if (c <= cnt) {
        gs_spc_delete(p, k);
        return;
    }
    lf->idx[k] = GS_SPSUM(lf->idx[k], cnt);
    lf->cnt[k] = c - cnt;
    gs_spc_fix(p, p->h, c, c - cnt);
}

GS_API int64_t gs_slice_pool_place(gs_rref nodes, void *tree, int64_t len, int64_t cnt,
                                   const gs_loc *at) {
    gs_spcpath p;
    int64_t l, i, k = 0, j, m, c, now, idx, before[GS_SPAN_DEPTH];
    gs_spleaf *lf = NULL;
    if (cnt == 0 || nodes.hdr->len == 0) return len;
    GS_SPSTART(&p, tree, nodes, at);
    p.h = GS_SPCHEAD(&p)->height;
    if (GS_SPBADH(p.h)) gs_spc_corrupt(&p);
    i = GS_SPCHEAD(&p)->root;
    for (l = 0; l < p.h; l++) {
        gs_spinner *in;
        if (GS_SPBADIN(&p, i)) gs_spc_corrupt(&p);
        in = GS_SPINNER(&p, i);
        for (k = 0, m = 0; k < in->n && in->max[k] < cnt; k++)
            if (in->max[k] > m) m = in->max[k];
        if (k == in->n) break;
        p.node[l] = i;
        p.pos[l] = k;
        before[l] = m;
        i = in->child[k];
    }
    if (l == p.h) {
        if (GS_SPBADLEAF(&p, i)) gs_spc_corrupt(&p);
        lf = GS_SPLEAF(&p, i);
        for (k = 0, m = 0; k < lf->n && lf->cnt[k] < cnt; k++)
            if (lf->cnt[k] > m) m = lf->cnt[k];
        before[l] = m;
    }
    if (l < p.h || k == lf->n) {
        for (l = 0, i = GS_SPCHEAD(&p)->root; l < p.h; l++) {
            gs_spinner *in;
            if (GS_SPBADIN(&p, i)) gs_spc_corrupt(&p);
            in = GS_SPINNER(&p, i);
            p.node[l] = i;
            p.pos[l] = in->n - 1;
            i = in->child[in->n - 1];
        }
        p.node[p.h] = i;
        if (GS_SPBADLEAF(&p, i)) gs_spc_corrupt(&p);
        lf = GS_SPLEAF(&p, i);
        k = lf->n - 1;
        if (k < 0 || GS_SPSUM(lf->idx[k], lf->cnt[k]) != len) return len;
        idx = lf->idx[k];
        gs_spc_take(&p, k, cnt);
        return idx;
    }
    p.node[p.h] = i;
    p.pos[p.h] = k;
    idx = lf->idx[k];
    c = lf->cnt[k];
    if (c > cnt) {
        lf->idx[k] = GS_SPSUM(idx, cnt);
        lf->cnt[k] = now = c - cnt;
        j = k + 1;
    } else if (lf->n > 1 || p.h == 0) {
        gs_spc_close(&p, lf, k);
        now = 0;
        j = k;
    } else {
        gs_spc_delete(&p, k);
        return idx;
    }
    for (l = p.h; l > 0; l--) {
        gs_spinner *in = GS_SPINNER(&p, p.node[l - 1]);
        int64_t e = p.pos[l - 1], mx = in->max[e], nm = mx;
        int64_t key = gs_spc_key(&p, p.node[l], l == p.h);
        if (c == mx) {
            const int64_t *v = l == p.h ? lf->cnt : GS_SPINNER(&p, p.node[l])->max;
            int64_t w = l == p.h ? lf->n : GS_SPINNER(&p, p.node[l])->n;
            nm = before[l] > now ? before[l] : now;
            for (; j < w && nm < c; j++)
                if (v[j] > nm) nm = v[j];
        }
        if (in->key[e] == key && nm == mx) break;
        in->key[e] = key;
        in->max[e] = nm;
        c = mx;
        now = nm;
        j = e + 1;
    }
    return idx;
}

typedef struct {
    gs_spleaf *lf, *nl;
    int64_t k, nk;
    gs_spcpath *np;
} gs_spcnear;

static void gs_spc_near(gs_spcpath *p, gs_spcpath *q, int64_t idx, int64_t end, gs_spcnear *s) {
    int64_t l;
    s->k = gs_spc_find(p, idx);
    s->lf = GS_SPLEAF(p, p->node[p->h]);
    s->nl = NULL;
    s->nk = -1;
    s->np = p;
    if (s->k + 1 < s->lf->n) {
        s->nl = s->lf;
        s->nk = s->k + 1;
        return;
    }
    for (l = p->h - 1; l >= 0 && p->pos[l] + 1 >= GS_SPINNER(p, p->node[l])->n; l--) {}
    if (l < 0 || GS_SPINNER(p, p->node[l])->key[p->pos[l] + 1] != end) return;
    q->hd = p->hd;
    q->base = p->base;
    q->n = p->n;
    q->top = p->top;
    q->at = p->at;
    q->h = p->h;
    memcpy(q->node, p->node, (size_t)(l + 1) * 8);
    memcpy(q->pos, p->pos, (size_t)(l + 1) * 8);
    for (q->pos[l]++; l < q->h; l++) {
        int64_t c = GS_SPINNER(q, q->node[l])->child[q->pos[l]];
        if (l + 1 < q->h ? GS_SPBADIN(q, c) : GS_SPBADLEAF(q, c)) gs_spc_corrupt(q);
        q->node[l + 1] = c;
        q->pos[l + 1] = 0;
    }
    s->np = q;
    s->nl = GS_SPLEAF(q, q->node[q->h]);
    s->nk = 0;
}

static void gs_spc_release(gs_spcpath *p, gs_spcnear *s, int64_t idx, int64_t cnt) {
    gs_spleaf *lf = s->lf, *nl = s->nl;
    int64_t k = s->k, nk = s->nk;
    int prev = k >= 0 && GS_SPSUM(lf->idx[k], lf->cnt[k]) == idx;
    int next = nl && idx + cnt == nl->idx[nk];
    if (prev) {
        int64_t c = lf->cnt[k], m = GS_SPSUM(GS_SPSUM(c, cnt), next ? nl->cnt[nk] : 0);
        lf->cnt[k] = m;
        if (next && nl == lf) gs_spc_close(p, lf, nk);
        gs_spc_fix(p, p->h, c, m);
        if (next && nl != lf) gs_spc_delete(s->np, nk);
    } else if (next) {
        int64_t c = nl->cnt[nk];
        nl->idx[nk] = idx;
        nl->cnt[nk] = GS_SPSUM(c, cnt);
        gs_spc_fix(s->np, s->np->h, c, nl->cnt[nk]);
    } else {
        gs_spc_insert(p, k + 1, idx, cnt);
    }
}

GS_API void gs_slice_pool_free(gs_rref nodes, void *tree, int64_t idx, int64_t cnt,
                               const gs_loc *at) {
    gs_spcpath p, q;
    gs_spcnear s;
    if (cnt <= 0) return;
    GS_SPSTART(&p, tree, nodes, at);
    if (*p.n == 0) {
        /* Node 0 stays zero, the chain's end. */
        memset(p.base, 0, GS_SPAN_NODE);
        *p.n = 1;
        *p.top = p.base + GS_SPAN_NODE;
        p.hd->chain = 0;
        p.hd->height = 0;
        p.hd->root = gs_spc_newnode(&p);
    }
    gs_spc_near(&p, &q, idx, idx + cnt, &s);
    gs_spc_release(&p, &s, idx, cnt);
}

GS_API int64_t gs_slice_pool_regrow(gs_rref nodes, void *tree, int64_t len, int64_t idx,
                                    int64_t ol, int64_t cnt, const gs_loc *at) {
    gs_spcpath p, q;
    gs_spcnear s;
    int64_t end = idx + ol;
    if (nodes.hdr->len == 0) {
        gs_slice_pool_free(nodes, tree, idx, ol, at);
        return gs_slice_pool_place(nodes, tree, len, cnt, at);
    }
    GS_SPSTART(&p, tree, nodes, at);
    gs_spc_near(&p, &q, idx, end, &s);
    if (s.nl && s.nl->idx[s.nk] == end &&
        (s.nl->cnt[s.nk] >= cnt - ol || GS_SPSUM(end, s.nl->cnt[s.nk]) == len)) {
        gs_spc_take(s.np, s.nk, cnt - ol);
        return idx;
    }
    gs_spc_release(&p, &s, idx, ol);
    return gs_slice_pool_place(nodes, tree, len, cnt, at);
}
