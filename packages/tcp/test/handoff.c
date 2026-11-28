#include <stdint.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

uint8_t test_tcp_handoff(uint64_t native) {
    int one = 1;
#ifdef _WIN32
    SOCKET s = (SOCKET)native;
    DWORD flags;
    int protected = GetHandleInformation((HANDLE)s, &flags) && !(flags & HANDLE_FLAG_INHERIT);
    int configured = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one) == 0;
    return closesocket(s) == 0 && protected && configured;
#else
    int s = (int)native;
    int flags = fcntl(s, F_GETFD);
    int status = fcntl(s, F_GETFL);
    int configured = setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) == 0;
    return close(s) == 0 && flags >= 0 && (flags & FD_CLOEXEC) &&
           status >= 0 && (status & O_NONBLOCK) && configured;
#endif
}
