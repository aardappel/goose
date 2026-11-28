/* Native half of the opt-in TCP package. */
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#elif !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif

#include "goose.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#pragma pack(push, 1)
typedef struct { const uint8_t *data; int64_t len; } tcp_bytes;
#pragma pack(pop)

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

static SRWLOCK tcp_mutex = SRWLOCK_INIT;
#define tcp_lock() AcquireSRWLockExclusive(&tcp_mutex)
#define tcp_unlock() ReleaseSRWLockExclusive(&tcp_mutex)
#ifndef WSA_FLAG_NO_HANDLE_INHERIT
#define WSA_FLAG_NO_HANDLE_INHERIT 0x80
#endif
typedef SOCKET tcp_fd;
typedef int tcp_socklen;
#define TCP_INVALID_FD INVALID_SOCKET
#define TCP_EINTR WSAEINTR
#define TCP_WOULD_BLOCK WSAEWOULDBLOCK
#define TCP_CONNECTING WSAEWOULDBLOCK
#else
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

static pthread_mutex_t tcp_mutex = PTHREAD_MUTEX_INITIALIZER;
#define tcp_lock() pthread_mutex_lock(&tcp_mutex)
#define tcp_unlock() pthread_mutex_unlock(&tcp_mutex)
typedef int tcp_fd;
typedef socklen_t tcp_socklen;
#define TCP_INVALID_FD (-1)
#define TCP_EINTR EINTR
#define TCP_WOULD_BLOCK EWOULDBLOCK
#define TCP_CONNECTING EINPROGRESS
#endif

static int tcp_close_fd(tcp_fd f) {
#ifdef _WIN32
    return closesocket(f);
#else
    return close(f);
#endif
}

static int tcp_err(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

/* Use atomic non-inheritance where the socket API offers it. On platforms
   with separate calls, set the flag before publishing the socket. */
static tcp_fd tcp_noinherit(tcp_fd f) {
    if (f == TCP_INVALID_FD) return f;
#ifdef _WIN32
    if (SetHandleInformation((HANDLE)f, HANDLE_FLAG_INHERIT, 0)) return f;
#else
    int result;
    do {
        result = fcntl(f, F_SETFD, FD_CLOEXEC);
    } while (result == -1 && errno == EINTR);
    if (result == 0) return f;
#endif
    tcp_close_fd(f);
    return TCP_INVALID_FD;
}

static int tcp_nonblocking(tcp_fd f) {
#ifdef _WIN32
    u_long on = 1;
    return ioctlsocket(f, FIONBIO, &on) == 0;
#else
    int flags = fcntl(f, F_GETFL);
    return flags >= 0 && fcntl(f, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

static tcp_fd tcp_prepare(tcp_fd f) {
    if (f == TCP_INVALID_FD || tcp_nonblocking(f)) return f;
    tcp_close_fd(f);
    return TCP_INVALID_FD;
}

static tcp_fd tcp_socket(const struct addrinfo *addr) {
#ifdef _WIN32
    tcp_fd f = WSASocketW(addr->ai_family, addr->ai_socktype, addr->ai_protocol,
                             NULL, 0, WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    /* Older Winsock versions reject WSA_FLAG_NO_HANDLE_INHERIT. */
    if (f != TCP_INVALID_FD || tcp_err() != WSAEINVAL) return tcp_prepare(f);
#elif defined(SOCK_CLOEXEC) && defined(SOCK_NONBLOCK)
    tcp_fd f = socket(addr->ai_family, addr->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK,
                      addr->ai_protocol);
    if (f != TCP_INVALID_FD || errno != EINVAL) return f;
#endif
    return tcp_prepare(tcp_noinherit(socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)));
}

static tcp_fd tcp_accept_fd(tcp_fd f, int *error) {
    tcp_fd c;
#if defined(__linux__) && defined(SYS_accept4)
    /* glibc declares accept4 only with _GNU_SOURCE. */
    c = (tcp_fd)syscall(SYS_accept4, f, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
    if (c != TCP_INVALID_FD) return c;
    *error = errno;
    if (*error != ENOSYS) return c;
#endif
    c = accept(f, NULL, NULL);
    if (c == TCP_INVALID_FD) {
        *error = tcp_err();
        return c;
    }
    *error = 0;
    return tcp_prepare(tcp_noinherit(c));
}

/* A handle holds a 31-bit generation and a one-based slot index. Pins keep
   the OS descriptor alive across syscalls without holding the table lock
   during blocking I/O. Slot pointers stay under the lock: the table can move. */
typedef struct {
    tcp_fd fd;
    size_t pins;
    uint32_t generation, next_free;
    uint8_t closing;
} tcp_slot;

static tcp_slot *tcp_slots;
static size_t tcp_capacity;
static uint32_t tcp_count, tcp_free;

static int tcp_grow(void) {
    size_t limit = SIZE_MAX / sizeof *tcp_slots;
    if (limit > UINT32_MAX) limit = UINT32_MAX;
    if (tcp_capacity == limit) return 0;
    size_t capacity = tcp_capacity ? tcp_capacity * 2 : 16;
    if (capacity > limit) capacity = limit;
    tcp_slot *slots = realloc(tcp_slots, capacity * sizeof *slots);
    if (!slots) return 0;
    tcp_slots = slots;
    tcp_capacity = capacity;
    return 1;
}

/* Takes ownership of f, including where a slot cannot be allocated. */
static int64_t tcp_register(tcp_fd f) {
    if (f == TCP_INVALID_FD) return -1;
    tcp_lock();
    uint32_t index = tcp_free;
    if (index) {
        tcp_free = tcp_slots[index - 1].next_free;
    } else {
        if (tcp_count == tcp_capacity && !tcp_grow()) {
            tcp_unlock();
            tcp_close_fd(f);
            return -1;
        }
        index = ++tcp_count;
        tcp_slots[index - 1].generation = 0;
    }
    tcp_slot *slot = &tcp_slots[index - 1];
    slot->fd = f;
    slot->pins = 0;
    slot->closing = 0;
    int64_t handle = ((int64_t)++slot->generation << 32) | index;
    tcp_unlock();
    return handle;
}

/* The caller holds tcp_mutex. */
static tcp_slot *tcp_lookup(int64_t handle) {
    uint32_t index = (uint32_t)handle;
    if (handle <= 0 || index == 0 || index > tcp_count) return NULL;
    tcp_slot *slot = &tcp_slots[index - 1];
    if (slot->closing || slot->fd == TCP_INVALID_FD ||
        slot->generation != ((uint64_t)handle >> 32)) return NULL;
    return slot;
}

static tcp_fd tcp_acquire(int64_t handle) {
    tcp_lock();
    tcp_slot *slot = tcp_lookup(handle);
    tcp_fd f = TCP_INVALID_FD;
    if (slot) {
        ++slot->pins;
        f = slot->fd;
    }
    tcp_unlock();
    return f;
}

/* The caller holds the mutex and the slot has no pins. */
static tcp_fd tcp_retire(tcp_slot *slot, uint32_t index) {
    tcp_fd f = slot->fd;
    slot->fd = TCP_INVALID_FD;
    /* Retire the slot instead of reissuing a handle after wraparound. */
    if (slot->generation < INT32_MAX) {
        slot->next_free = tcp_free;
        tcp_free = index;
    }
    return f;
}

/* A successful acquire keeps this slot occupied until the matching release.
   Report whether the operation finished before another thread closed it. */
static int tcp_release(int64_t handle) {
    uint32_t index = (uint32_t)handle;
    tcp_lock();
    tcp_slot *slot = &tcp_slots[index - 1];
    int closing = slot->closing;
    tcp_fd f = --slot->pins == 0 && closing ? tcp_retire(slot, index) : TCP_INVALID_FD;
    tcp_unlock();
    if (f != TCP_INVALID_FD) tcp_close_fd(f);
    return !closing;
}

static int tcp_is_open(int64_t handle) {
    tcp_lock();
    int open = tcp_lookup(handle) != NULL;
    tcp_unlock();
    return open;
}

static int tcp_startup(void) {
#ifdef _WIN32
    /* Keep Winsock initialized for the lifetime of sockets handed to C. */
    static int state;
    tcp_lock();
    if (state == 0) {
        WSADATA d;
        state = WSAStartup(MAKEWORD(2, 2), &d) == 0 ? 1 : -1;
    }
    int ok = state > 0;
    tcp_unlock();
    return ok;
#else
    return 1;
#endif
}

static int tcp_retry(int error) {
#ifndef _WIN32
    if (error == EAGAIN) return 1;
#endif
    return error == TCP_EINTR || error == TCP_WOULD_BLOCK;
}

static uint64_t tcp_now_ms(void) {
#ifdef _WIN32
    return GetTickCount64();
#else
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return UINT64_MAX;
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
#endif
}

static uint64_t tcp_deadline(int64_t timeout_ms) {
    uint64_t now = tcp_now_ms();
    if (timeout_ms < 0 || (uint64_t)timeout_ms > UINT64_MAX - now) return UINT64_MAX;
    return now + (uint64_t)timeout_ms;
}

static int tcp_poll(tcp_fd f, int writing, int timeout_ms) {
#ifdef _WIN32
    fd_set ready, errors;
    FD_ZERO(&ready);
    FD_SET(f, &ready);
    FD_ZERO(&errors);
    FD_SET(f, &errors);
    struct timeval timeout = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    /* A failed nonblocking connect is reported in the exception set. */
    return select(0, writing ? NULL : &ready, writing ? &ready : NULL,
                  writing ? &errors : NULL, &timeout);
#else
    struct pollfd p = { f, writing ? POLLOUT : POLLIN, 0 };
    return poll(&p, 1, timeout_ms);
#endif
}

/* A registered socket checks for cancellation between short readiness waits.
   A connecting socket has no handle yet and only uses its deadline. */
static int tcp_wait(int64_t handle, tcp_fd f, int writing, uint64_t deadline) {
    for (;;) {
        if (handle && !tcp_is_open(handle)) return -1;
        uint64_t now = tcp_now_ms();
        if (now >= deadline) return 0;
        uint64_t left = deadline - now;
        int cap = handle ? 50 : INT_MAX;
        int ready = tcp_poll(f, writing, left < (uint64_t)cap ? (int)left : cap);
        if (ready > 0) return 1;
        if (ready < 0 && tcp_err() != TCP_EINTR) return -1;
    }
}

/* A host for getaddrinfo, NUL-terminated in buf (TCP_HOST_MAX bytes):
   its length, or -1 where it is refused. Narrow on every platform --
   hostnames and numeric literals are ASCII. Refused empty, over the cap
   or holding a NUL, as gs_os_native refuses paths. */
#define TCP_HOST_MAX 256
static int tcp_host(tcp_bytes s, char *buf) {
    if (s.len <= 0 || s.len >= TCP_HOST_MAX || memchr(s.data, 0, (size_t)s.len))
        return -1;
    int n = (int)s.len;
    memcpy(buf, s.data, (size_t)n);
    buf[n] = 0;
    return n;
}

/* The connection deadline starts after the synchronous DNS lookup. */
int64_t goose_tcp_connect(tcp_bytes host, int64_t port, int64_t timeout_ms) {
    if (timeout_ms < -1 || !tcp_startup()) return -1;
    char h[TCP_HOST_MAX];
    if (port < 1 || port > 65535 || tcp_host(host, h) < 0) return -1;
    char ps[16];
    snprintf(ps, sizeof ps, "%d", (int)port);
    struct addrinfo hints, *list = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    if (getaddrinfo(h, ps, &hints, &list) != 0) return -1;
    uint64_t deadline = tcp_deadline(timeout_ms);
    tcp_fd connected = TCP_INVALID_FD;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        tcp_fd f = tcp_socket(a);
        if (f == TCP_INVALID_FD) continue;
#ifdef SO_NOSIGPIPE
        int one = 1;
        setsockopt(f, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
        int ok = connect(f, a->ai_addr, (tcp_socklen)a->ai_addrlen) == 0;
        if (!ok) {
            int error = tcp_err();
            if (error == TCP_CONNECTING) {
                int ready = timeout_ms == 0 ? tcp_poll(f, 1, 0) : tcp_wait(0, f, 1, deadline);
                if (ready > 0) {
                    tcp_socklen size = sizeof error;
                    ok = getsockopt(f, SOL_SOCKET, SO_ERROR, (char *)&error, &size) == 0 && error == 0;
                }
            }
        }
        if (ok) {
            connected = f;
            break;
        }
        tcp_close_fd(f);
        if (timeout_ms > 0 && tcp_now_ms() >= deadline) break;
    }
    freeaddrinfo(list);
    return tcp_register(connected);
}

static tcp_fd tcp_bind(const char *node, const char *service, int family) {
    struct addrinfo hints, *list = NULL;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = family;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    if (getaddrinfo(node, service, &hints, &list) != 0) return TCP_INVALID_FD;
    tcp_fd listener = TCP_INVALID_FD;
    for (struct addrinfo *a = list; a; a = a->ai_next) {
        tcp_fd f = tcp_socket(a);
        if (f == TCP_INVALID_FD) continue;
        if (a->ai_family == AF_INET6) {
            int off = 0;
            if (setsockopt(f, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&off, sizeof off) != 0 &&
                !node) {
                tcp_close_fd(f);
                continue;
            }
        }
#ifndef _WIN32
        /* Fixed-port restarts. Not on Windows, where SO_REUSEADDR lets
           another socket hijack the port. */
        int one = 1;
        setsockopt(f, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
#endif
        if (bind(f, a->ai_addr, (tcp_socklen)a->ai_addrlen) == 0 &&
            listen(f, SOMAXCONN) == 0) {
            listener = f;
            break;
        }
        tcp_close_fd(f);
    }
    freeaddrinfo(list);
    return listener;
}

/* listen(host, port): binds and listens. An empty host is the wildcard
   address -- IPv6 dual-stack where the OS allows it, else IPv4. Port
   0..65535; 0 picks an ephemeral port (tcp_local_port reports it). */
int64_t goose_tcp_listen(tcp_bytes host, int64_t port) {
    if (!tcp_startup() || port < 0 || port > 65535) return -1;
    char h[TCP_HOST_MAX];
    const char *node = NULL;
    if (host.len != 0) {
        if (tcp_host(host, h) < 0) return -1;
        node = h;
    }
    char ps[16];
    snprintf(ps, sizeof ps, "%d", (int)port);
    tcp_fd f = TCP_INVALID_FD;
    if (!node) f = tcp_bind(NULL, ps, AF_INET6);
    if (f == TCP_INVALID_FD) f = tcp_bind(node, ps, node ? AF_UNSPEC : AF_INET);
    return tcp_register(f);
}

/* accept(s): one pending connection on a listening socket. */
int64_t goose_tcp_accept(int64_t s, int64_t timeout_ms) {
    if (timeout_ms < -1) return -1;
    tcp_fd f = tcp_acquire(s);
    if (f == TCP_INVALID_FD) return -1;
    uint64_t deadline = tcp_deadline(timeout_ms);
    tcp_fd c;
    int error;
    do {
        c = tcp_accept_fd(f, &error);
    } while (c == TCP_INVALID_FD && tcp_retry(error) && tcp_wait(s, f, 0, deadline) > 0);
    if (!tcp_release(s)) {
        if (c != TCP_INVALID_FD) tcp_close_fd(c);
        return -1;
    }
    if (c == TCP_INVALID_FD) return -1;
#ifdef SO_NOSIGPIPE
    /* The accepted socket does not inherit the listener's. */
    int one = 1;
    setsockopt(c, SOL_SOCKET, SO_NOSIGPIPE, (const char *)&one, sizeof one);
#endif
    return tcp_register(c);
}

/* send(s, data): all the bytes or false. On false some may already be
   sent -- there is no rollback. An empty slice is true, without a call. */
uint8_t goose_tcp_send(int64_t s, tcp_bytes data, int64_t timeout_ms) {
    if (timeout_ms < -1) return 0;
    tcp_fd f = tcp_acquire(s);
    if (f == TCP_INVALID_FD) return 0;
    uint64_t deadline = tcp_deadline(timeout_ms);
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
        if (w < 0 && tcp_retry(tcp_err()) && tcp_wait(s, f, 1, deadline) > 0) continue;
        if (w <= 0) break;
        d += w;
        left -= w;
        if (left > 0 && (tcp_now_ms() >= deadline || !tcp_is_open(s))) break;
    }
    return tcp_release(s) && left == 0;
}

/* recv appends one read's bytes, 0 at EOF, -1 for error or cancellation,
   -2 for timeout. Only a positive result appends bytes. */
int64_t goose_tcp_recv(int64_t s, gs_rref out, int64_t timeout_ms) {
    if (timeout_ms < -1) return -1;
    tcp_fd f = tcp_acquire(s);
    if (f == TCP_INVALID_FD) return -1;
    uint64_t deadline = tcp_deadline(timeout_ms);
    uint8_t buf[65536];
    int64_t n;
    for (;;) {
        n = recv(f, (char *)buf, (int)sizeof buf, 0);
        if (n >= 0 || !tcp_retry(tcp_err())) break;
        int ready = tcp_wait(s, f, 0, deadline);
        if (ready > 0) continue;
        n = ready == 0 ? -2 : -1;
        break;
    }
    if (!tcp_release(s)) return -1;
    if (n > 0) gs_bld_append(out, buf, n);
    return n;
}

/* Invalidate the handle now. The last pinned operation closes the descriptor. */
uint8_t goose_tcp_close(int64_t s) {
    tcp_lock();
    tcp_slot *slot = tcp_lookup(s);
    if (!slot) {
        tcp_unlock();
        return 0;
    }
    slot->closing = 1;
    tcp_fd f = slot->pins == 0 ? tcp_retire(slot, (uint32_t)s) : TCP_INVALID_FD;
    tcp_unlock();
    if (f != TCP_INVALID_FD) tcp_close_fd(f);
    return 1;
}

/* Transfer an idle socket into the caller's native ownership. */
uint64_t goose_tcp_take_native(int64_t s) {
    tcp_lock();
    tcp_slot *slot = tcp_lookup(s);
    tcp_fd f = slot && slot->pins == 0 ? tcp_retire(slot, (uint32_t)s) : TCP_INVALID_FD;
    tcp_unlock();
    return f == TCP_INVALID_FD ? UINT64_MAX : (uint64_t)f;
}

/* local_port(s): the port the socket is bound to, or -1. */
int64_t goose_tcp_local_port(int64_t s) {
    tcp_fd f = tcp_acquire(s);
    if (f == TCP_INVALID_FD) return -1;
    struct sockaddr_storage addr;
    tcp_socklen alen = (tcp_socklen)sizeof addr;
    int result = getsockname(f, (struct sockaddr *)&addr, &alen);
    if (!tcp_release(s) || result != 0) return -1;
    if (addr.ss_family == AF_INET)
        return ntohs(((struct sockaddr_in *)&addr)->sin_port);
    if (addr.ss_family == AF_INET6)
        return ntohs(((struct sockaddr_in6 *)&addr)->sin6_port);
    return -1;
}
