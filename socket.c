#include "types.h"
#include "net.h"
#include "heap.h"
#include "console.h"
#include "process.h"

static void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = c;
    return s;
}

#define SOCKET_MAX      64
#define SOCKET_TYPE_UDP 1
#define SOCKET_TYPE_RAW 2

struct Socket {
    int type;
    int protocol;
    int udp_fd;
    bool bound;
    struct Ipv4Addr local_addr;
    uint16_t local_port;
    struct Ipv4Addr remote_addr;
    uint16_t remote_port;
};

static struct Socket socket_table[SOCKET_MAX];

void socket_init(void) {
    memset(socket_table, 0, sizeof(socket_table));
}

static int socket_alloc(void) {
    for (int i = 0; i < SOCKET_MAX; i++) {
        if (socket_table[i].type == 0) {
            return i;
        }
    }
    return -ENOMEM;
}

int sys_socket(int domain, int type, int protocol) {
    (void)protocol;

    if (domain != AF_INET) return -EAFNOSUPPORT;

    int idx = socket_alloc();
    if (idx < 0) return idx;

    struct Socket *sock = &socket_table[idx];
    memset(sock, 0, sizeof(*sock));

    switch (type) {
    case SOCK_DGRAM:
        sock->type = SOCKET_TYPE_UDP;
        sock->udp_fd = udp_socket_create();
        if (sock->udp_fd < 0) {
            sock->type = 0;
            return sock->udp_fd;
        }
        break;
    case SOCK_RAW:
        sock->type = SOCKET_TYPE_RAW;
        sock->protocol = protocol;
        break;
    default:
        return -EPROTONOSUPPORT;
    }

    return idx + 256;
}

int sys_bind(int sockfd, const struct SockaddrIn *addr) {
    int idx = sockfd - 256;
    if (idx < 0 || idx >= SOCKET_MAX) return -EBADF;

    struct Socket *sock = &socket_table[idx];
    if (sock->type == 0) return -EBADF;

    sock->local_addr = addr->sin_addr;
    sock->local_port = ntohs(addr->sin_port);
    sock->bound = true;

    if (sock->type == SOCKET_TYPE_UDP) {
        return udp_socket_bind(sock->udp_fd, sock->local_addr, sock->local_port);
    }

    return 0;
}

int sys_sendto(int sockfd, const void *buf, size_t len,
               int flags, const struct SockaddrIn *dest_addr) {
    (void)flags;

    int idx = sockfd - 256;
    if (idx < 0 || idx >= SOCKET_MAX) return -EBADF;

    struct Socket *sock = &socket_table[idx];
    if (sock->type == 0) return -EBADF;

    struct Ipv4Addr dest = dest_addr->sin_addr;
    uint16_t port = ntohs(dest_addr->sin_port);

    if (sock->type == SOCKET_TYPE_UDP) {
        return udp_socket_sendto(sock->udp_fd, buf, len, dest, port);
    }

    return -ENOSYS;
}

int sys_recvfrom(int sockfd, void *buf, size_t len,
                 int flags, struct SockaddrIn *src_addr) {
    (void)flags;

    int idx = sockfd - 256;
    if (idx < 0 || idx >= SOCKET_MAX) return -EBADF;

    struct Socket *sock = &socket_table[idx];
    if (sock->type == 0) return -EBADF;

    if (sock->type == SOCKET_TYPE_UDP) {
        struct Ipv4Addr from_addr;
        uint16_t from_port;

        int result = udp_socket_recvfrom(sock->udp_fd, buf, len,
                                         &from_addr, &from_port);

        if (result >= 0 && src_addr) {
            src_addr->sin_family = AF_INET;
            src_addr->sin_port = htons(from_port);
            src_addr->sin_addr = from_addr;
        }

        return result;
    }

    return -ENOSYS;
}

int sys_socket_close(int sockfd) {
    int idx = sockfd - 256;
    if (idx < 0 || idx >= SOCKET_MAX) return -EBADF;

    struct Socket *sock = &socket_table[idx];
    if (sock->type == 0) return -EBADF;

    if (sock->type == SOCKET_TYPE_UDP) {
        udp_socket_close(sock->udp_fd);
    }

    memset(sock, 0, sizeof(*sock));
    return 0;
}
