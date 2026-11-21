/* Goose runtime: the TCP primitives behind stdlib/net.goose (spec §7.10),
   as runtime_ext.h declares them, which this follows. Blocking sockets use
   opaque handles. No borrowed argument is retained. */

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
/* TinyCC's ws2tcpip.h predates it. */
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 27
#endif
#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif
typedef SOCKET gs_net_fd;
typedef int gs_net_socklen;
#define GS_NET_INVALID_FD INVALID_SOCKET
#define GS_NET_EINTR WSAEINTR
#else
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
typedef int gs_net_fd;
typedef socklen_t gs_net_socklen;
#define GS_NET_INVALID_FD (-1)
#define GS_NET_EINTR EINTR
#endif

static int gs_net_close_fd(gs_net_fd f) {
#ifdef _WIN32
    return closesocket(f);
#else
    return close(f);
#endif
}

static int gs_net_err(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

/* Use atomic non-inheritance where the socket API offers it. On platforms
   with separate calls, set the flag before publishing the socket. */
static gs_net_fd gs_net_noinherit(gs_net_fd f) {
    if (f == GS_NET_INVALID_FD) return f;
#ifdef _WIN32
    if (SetHandleInformation((HANDLE)f, HANDLE_FLAG_INHERIT, 0)) return f;
#else
    int result;
    do {
        result = fcntl(f, F_SETFD, FD_CLOEXEC);
    } while (result == -1 && errno == EINTR);
    if (result == 0) return f;
#endif
    gs_net_close_fd(f);
    return GS_NET_INVALID_FD;
}

static gs_net_fd gs_net_socket(const struct addrinfo *addr) {
#ifdef _WIN32
    gs_net_fd f = WSASocketW(addr->ai_family, addr->ai_socktype, addr->ai_protocol,
                             NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    /* Older Winsock versions reject WSA_FLAG_NO_HANDLE_INHERIT. */
    if (f != GS_NET_INVALID_FD || gs_net_err() != WSAEINVAL) return f;
#elif defined(SOCK_CLOEXEC)
    gs_net_fd f = socket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC, addr->ai_protocol);
    if (f != GS_NET_INVALID_FD || errno != EINVAL) return f;
#endif
    return gs_net_noinherit(socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol));
}

static gs_net_fd gs_net_accept_fd(gs_net_fd f) {
    gs_net_fd c;
#if defined(__linux__) && defined(SYS_accept4)
    /* glibc declares accept4 only with _GNU_SOURCE. */
    do {
        c = (gs_net_fd)syscall(SYS_accept4, f, NULL, NULL, SOCK_CLOEXEC);
    } while (c == GS_NET_INVALID_FD && errno == EINTR);
    if (c != GS_NET_INVALID_FD || errno != ENOSYS) return c;
#endif
    do {
        c = accept(f, NULL, NULL);
    } while (c == GS_NET_INVALID_FD && gs_net_err() == GS_NET_EINTR);
    return gs_net_noinherit(c);
}

/* A handle holds a 31-bit generation and a one-based slot index. Pins keep
   the OS descriptor alive across syscalls without holding the table lock
   during blocking I/O. Slot pointers stay under the lock: the table can move. */
typedef struct {
    gs_net_fd fd;
    size_t pins;
    uint32_t generation, next_free;
} gs_net_slot;

static gs_mutex gs_net_mutex = GS_MUTEX_INIT;
static gs_net_slot *gs_net_slots;
static size_t gs_net_capacity;
static uint32_t gs_net_count, gs_net_free;

static int gs_net_grow(void) {
    size_t limit = SIZE_MAX / sizeof *gs_net_slots;
    if (limit > UINT32_MAX) limit = UINT32_MAX;
    if (gs_net_capacity == limit) return 0;
    size_t capacity = gs_net_capacity ? gs_net_capacity * 2 : 16;
    if (capacity > limit) capacity = limit;
    gs_net_slot *slots = realloc(gs_net_slots, capacity * sizeof *slots);
    if (!slots) return 0;
    gs_net_slots = slots;
    gs_net_capacity = capacity;
    return 1;
}

/* Takes ownership of f, including where a slot cannot be allocated. */
static int64_t gs_net_register(gs_net_fd f) {
    if (f == GS_NET_INVALID_FD) return -1;
    gs_mutex_lock(&gs_net_mutex);
    uint32_t index = gs_net_free;
    if (index) {
        gs_net_free = gs_net_slots[index - 1].next_free;
    } else {
        if (gs_net_count == gs_net_capacity && !gs_net_grow()) {
            gs_mutex_unlock(&gs_net_mutex);
            gs_net_close_fd(f);
            return -1;
        }
        index = ++gs_net_count;
        gs_net_slots[index - 1].generation = 0;
    }
    gs_net_slot *slot = &gs_net_slots[index - 1];
    slot->fd = f;
    slot->pins = 0;
    int64_t handle = ((int64_t)++slot->generation << 32) | index;
    gs_mutex_unlock(&gs_net_mutex);
    return handle;
}

/* The caller holds gs_net_mutex. */
static gs_net_slot *gs_net_lookup(int64_t handle) {
    uint32_t index = (uint32_t)handle;
    if (handle <= 0 || index == 0 || index > gs_net_count) return NULL;
    gs_net_slot *slot = &gs_net_slots[index - 1];
    if (slot->fd == GS_NET_INVALID_FD || slot->generation != (uint64_t)handle >> 32) return NULL;
    return slot;
}

static gs_net_fd gs_net_acquire(int64_t handle) {
    gs_mutex_lock(&gs_net_mutex);
    gs_net_slot *slot = gs_net_lookup(handle);
    gs_net_fd f = GS_NET_INVALID_FD;
    if (slot) {
        ++slot->pins;
        f = slot->fd;
    }
    gs_mutex_unlock(&gs_net_mutex);
    return f;
}

/* A successful acquire keeps this slot occupied until the matching release. */
static void gs_net_release(int64_t handle) {
    gs_mutex_lock(&gs_net_mutex);
    --gs_net_slots[(uint32_t)handle - 1].pins;
    gs_mutex_unlock(&gs_net_mutex);
}

/* WSAStartup once per process, never WSACleanup: the JIT runs many
   programs in one process (jit_runtime.c). 0 unstarted, 1 starting,
   2 ready, 3 failed; the winner of 0->1 initializes and publishes the
   result with a full barrier, as gs_os_entropy publishes its pointers
   (ARM64 too). A no-op elsewhere. */
static int gs_net_startup(void) {
#ifdef _WIN32
    static volatile LONG state;
    LONG s = InterlockedCompareExchange(&state, 0, 0);
    if (s == 0 && InterlockedCompareExchange(&state, 1, 0) == 0) {
        WSADATA d;
        int ok = WSAStartup(MAKEWORD(2, 2), &d) == 0;
        InterlockedExchange(&state, ok ? 2 : 3);
        return ok;
    }
    /* Lost the 0->1 race, or arrived late: wait for the winner's result.
       The local s may still read 0 here, so re-read rather than trust it. */
    for (;;) {
        s = InterlockedCompareExchange(&state, 0, 0);
        if (s != 1) return s == 2;
        Sleep(0);
    }
#else
    return 1;
#endif
}

/* A host for getaddrinfo, NUL-terminated in buf (GS_NET_HOST_MAX bytes):
   its length, or -1 where it is refused. Narrow on every platform --
   hostnames and numeric literals are ASCII. Refused empty, over the cap
   or holding a NUL, as gs_os_native refuses paths. */
#define GS_NET_HOST_MAX 256
static int gs_net_host(sl_u8 s, char *buf) {
    if (s.len <= 0 || s.len >= GS_NET_HOST_MAX || memchr(s.data, 0, (size_t)s.len))
        return -1;
    int n = (int)s.len;
    gs_memcpy(buf, s.data, (size_t)n);
    buf[n] = 0;
    return n;
}

/* connect(host, port): resolves the host and tries each address until one
   connects. Port 1..65535. */
GS_API int64_t gs_net_connect(sl_u8 host, int64_t port) {
    if (!gs_net_startup()) return -1;
    char h[GS_NET_HOST_MAX];
    if (port < 1 || port > 65535 || gs_net_host(host, h) < 0) return -1;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", (int)port);
    struct addrinfo hints, *list = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(h, ps, &hints, &list) != 0) return -1;
    gs_net_fd connected = GS_NET_INVALID_FD;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        for (;;) {
            gs_net_fd f = gs_net_socket(a);
            if (f == GS_NET_INVALID_FD) break;
#ifdef SO_NOSIGPIPE
            /* A send to a closed peer fails instead of killing the
               program; accepted sockets need it too (gs_net_accept). */
            int one = 1;
            setsockopt(f, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
            if (connect(f, a->ai_addr, (gs_net_socklen)a->ai_addrlen) == 0) {
                connected = f;
                break;
            }
            int err = gs_net_err();
            gs_net_close_fd(f);
            /* POSIX leaves an interrupted connect in progress: the retry
               needs a fresh socket. */
            if (err == GS_NET_EINTR) continue;
            break;
        }
        if (connected != GS_NET_INVALID_FD) break;
    }
    freeaddrinfo(list);
    return gs_net_register(connected);
}

static gs_net_fd gs_net_bind(const char *node, const char *service, int family) {
    struct addrinfo hints, *list = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(node, service, &hints, &list) != 0) return GS_NET_INVALID_FD;
    gs_net_fd listener = GS_NET_INVALID_FD;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        gs_net_fd f = gs_net_socket(a);
        if (f == GS_NET_INVALID_FD) continue;
        if (a->ai_family == AF_INET6) {
            int off = 0;
            if (setsockopt(f, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof off) != 0 &&
                !node) {
                gs_net_close_fd(f);
                continue;
            }
        }
#ifndef _WIN32
        /* Fixed-port restarts. Not on Windows, where SO_REUSEADDR lets
           another socket hijack the port. */
        int one = 1;
        setsockopt(f, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
#endif
        if (bind(f, a->ai_addr, (gs_net_socklen)a->ai_addrlen) == 0 &&
            listen(f, SOMAXCONN) == 0) {
            listener = f;
            break;
        }
        gs_net_close_fd(f);
    }
    freeaddrinfo(list);
    return listener;
}

/* listen(host, port): binds and listens. An empty host is the wildcard
   address -- IPv6 dual-stack where the OS allows it, else IPv4. Port
   0..65535; 0 picks an ephemeral port (gs_net_local_port reports it). */
GS_API int64_t gs_net_listen(sl_u8 host, int64_t port) {
    if (!gs_net_startup() || port < 0 || port > 65535) return -1;
    char h[GS_NET_HOST_MAX];
    const char *node = NULL;
    if (host.len != 0) {
        if (gs_net_host(host, h) < 0) return -1;
        node = h;
    }
    char ps[16];
    snprintf(ps, sizeof ps, "%d", (int)port);
    gs_net_fd f = GS_NET_INVALID_FD;
    if (!node) f = gs_net_bind(NULL, ps, AF_INET6);
    if (f == GS_NET_INVALID_FD) f = gs_net_bind(node, ps, node ? AF_UNSPEC : AF_INET);
    return gs_net_register(f);
}

/* accept(s): one pending connection on a listening socket. */
GS_API int64_t gs_net_accept(int64_t s) {
    gs_net_fd f = gs_net_acquire(s);
    if (f == GS_NET_INVALID_FD) return -1;
    gs_net_fd c = gs_net_accept_fd(f);
    gs_net_release(s);
    if (c == GS_NET_INVALID_FD) return -1;
#ifdef SO_NOSIGPIPE
    /* The accepted socket does not inherit the listener's. */
    int one = 1;
    setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
    return gs_net_register(c);
}

/* send(s, data): all the bytes or false. On false some may already be
   sent -- there is no rollback. An empty slice is true, without a call. */
GS_API uint8_t gs_net_send(int64_t s, sl_u8 data) {
    gs_net_fd f = gs_net_acquire(s);
    if (f == GS_NET_INVALID_FD) return 0;
    const char *d = (const char *)data.data;
    int64_t left = data.len;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    while (left > 0) {
        /* int lengths on Windows, and write's chunk limit elsewhere. */
        int n = left > (1 << 30) ? (1 << 30) : (int)left;
        int64_t w = send(f, d, n, flags);
        if (w < 0 && gs_net_err() == GS_NET_EINTR) continue;
        if (w <= 0) break;
        d += w;
        left -= w;
    }
    gs_net_release(s);
    return left == 0;
}

/* recv(s, out): appends one read's bytes, at most 65536; their count, 0 at
   the peer's orderly close, -1 on error. Neither appends anything. */
GS_API int64_t gs_net_recv(int64_t s, gs_rref out) {
    gs_net_fd f = gs_net_acquire(s);
    if (f == GS_NET_INVALID_FD) return -1;
    uint8_t buf[65536];
    int64_t n;
    do {
        n = recv(f, (char *)buf, (int)sizeof buf, 0);
    } while (n < 0 && gs_net_err() == GS_NET_EINTR);
    gs_net_release(s);
    if (n > 0) gs_bld_append(out, buf, n);
    return n;
}

/* A busy socket remains valid. Otherwise consume its handle before calling
   close, with no EINTR retry since the descriptor may already be closed. */
GS_API uint8_t gs_net_close(int64_t s) {
    gs_net_fd f = GS_NET_INVALID_FD;
    gs_mutex_lock(&gs_net_mutex);
    gs_net_slot *slot = gs_net_lookup(s);
    if (slot && slot->pins == 0) {
        f = slot->fd;
        slot->fd = GS_NET_INVALID_FD;
        /* Retire the slot instead of reissuing a handle after wraparound. */
        if (slot->generation < INT32_MAX) {
            slot->next_free = gs_net_free;
            gs_net_free = (uint32_t)s;
        }
    }
    gs_mutex_unlock(&gs_net_mutex);
    return f != GS_NET_INVALID_FD && gs_net_close_fd(f) == 0;
}

/* local_port(s): the port the socket is bound to, or -1. */
GS_API int64_t gs_net_local_port(int64_t s) {
    gs_net_fd f = gs_net_acquire(s);
    if (f == GS_NET_INVALID_FD) return -1;
    struct sockaddr_storage addr;
    gs_net_socklen alen = (gs_net_socklen)sizeof addr;
    int result = getsockname(f, (struct sockaddr *)&addr, &alen);
    gs_net_release(s);
    if (result != 0) return -1;
    if (addr.ss_family == AF_INET)
        return ntohs(((struct sockaddr_in *)&addr)->sin_port);
    if (addr.ss_family == AF_INET6)
        return ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    return -1;
}
