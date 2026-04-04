#include "types.h"
#include "net.h"
#include "heap.h"
#include "console.h"

static void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = c;
    return s;
}

static void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

#define UDP_MAX_SOCKETS     32
#define UDP_RECV_QUEUE_SIZE 16

struct UdpDatagram {
    struct Ipv4Addr src_addr;
    uint16_t src_port;
    uint8_t *data;
    size_t len;
    struct UdpDatagram *next;
};

struct UdpSocket {
    bool bound;
    uint16_t local_port;
    struct Ipv4Addr local_addr;
    struct UdpDatagram *recv_head;
    struct UdpDatagram *recv_tail;
    uint32_t recv_count;
};

static struct UdpSocket udp_sockets[UDP_MAX_SOCKETS];

void udp_init(void) {
    memset(udp_sockets, 0, sizeof(udp_sockets));
}

static struct UdpSocket *udp_find_socket(uint16_t port) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].bound && udp_sockets[i].local_port == port) {
            return &udp_sockets[i];
        }
    }
    return NULL;
}

static struct UdpSocket *udp_alloc_socket(void) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!udp_sockets[i].bound) {
            return &udp_sockets[i];
        }
    }
    return NULL;
}

int udp_socket_create(void) {
    struct UdpSocket *sock = udp_alloc_socket();
    if (!sock) return -ENOMEM;

    sock->bound = false;
    sock->local_port = 0;
    sock->local_addr.addr = 0;
    sock->recv_head = NULL;
    sock->recv_tail = NULL;
    sock->recv_count = 0;

    return (int)(sock - udp_sockets);
}

int udp_socket_bind(int sockfd, struct Ipv4Addr addr, uint16_t port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -EBADF;

    struct UdpSocket *sock = &udp_sockets[sockfd];

    if (udp_find_socket(port)) return -EADDRINUSE;

    sock->local_addr = addr;
    sock->local_port = port;
    sock->bound = true;

    return 0;
}

int udp_socket_sendto(int sockfd, const void *data, size_t len,
                      struct Ipv4Addr dest_addr, uint16_t dest_port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -EBADF;

    struct UdpSocket *sock = &udp_sockets[sockfd];

    if (!sock->bound) {
        sock->local_port = 49152 + (uint16_t)(sockfd * 37 + 7);
        sock->local_addr.addr = 0;
        sock->bound = true;
    }

    return udp_send(dest_addr, sock->local_port, dest_port, data, len);
}

int udp_socket_recvfrom(int sockfd, void *buf, size_t len,
                        struct Ipv4Addr *src_addr, uint16_t *src_port) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return -EBADF;

    struct UdpSocket *sock = &udp_sockets[sockfd];
    if (!sock->bound) return -EINVAL;

    struct UdpDatagram *dgram = sock->recv_head;
    if (!dgram) return -EAGAIN;

    sock->recv_head = dgram->next;
    if (!sock->recv_head) sock->recv_tail = NULL;
    sock->recv_count--;

    size_t copy_len = dgram->len < len ? dgram->len : len;
    memcpy(buf, dgram->data, copy_len);

    if (src_addr) *src_addr = dgram->src_addr;
    if (src_port) *src_port = dgram->src_port;

    kfree(dgram->data);
    kfree(dgram);

    return (int)copy_len;
}

void udp_socket_close(int sockfd) {
    if (sockfd < 0 || sockfd >= UDP_MAX_SOCKETS) return;

    struct UdpSocket *sock = &udp_sockets[sockfd];

    struct UdpDatagram *dgram = sock->recv_head;
    while (dgram) {
        struct UdpDatagram *next = dgram->next;
        kfree(dgram->data);
        kfree(dgram);
        dgram = next;
    }

    memset(sock, 0, sizeof(*sock));
}

void udp_receive(struct IpHeader *ip, void *data, size_t len) {
    if (len < sizeof(struct UdpHeader)) return;

    struct UdpHeader *udp = data;
    uint16_t dest_port = ntohs(udp->dest_port);
    uint16_t src_port = ntohs(udp->src_port);
    uint16_t udp_len = ntohs(udp->length);

    if (udp_len < sizeof(struct UdpHeader) || udp_len > len) return;

    struct UdpSocket *sock = udp_find_socket(dest_port);
    if (!sock) return;

    size_t payload_len = udp_len - sizeof(struct UdpHeader);
    uint8_t *payload = (uint8_t *)data + sizeof(struct UdpHeader);

    if (sock->recv_count >= UDP_RECV_QUEUE_SIZE) return;

    struct UdpDatagram *dgram = kzalloc(sizeof(struct UdpDatagram));
    if (!dgram) return;

    dgram->data = kmalloc(payload_len);
    if (!dgram->data) {
        kfree(dgram);
        return;
    }

    memcpy(dgram->data, payload, payload_len);
    dgram->len = payload_len;
    dgram->src_addr.addr = ip->src_addr;
    dgram->src_port = src_port;
    dgram->next = NULL;

    if (sock->recv_tail) {
        sock->recv_tail->next = dgram;
    } else {
        sock->recv_head = dgram;
    }
    sock->recv_tail = dgram;
    sock->recv_count++;
}

int udp_send(struct Ipv4Addr dest, uint16_t src_port, uint16_t dest_port,
             const void *data, size_t len) {
    size_t total = sizeof(struct UdpHeader) + len;
    uint8_t *packet = kmalloc(total);
    if (!packet) return -ENOMEM;

    struct UdpHeader *udp = (struct UdpHeader *)packet;
    udp->src_port = htons(src_port);
    udp->dest_port = htons(dest_port);
    udp->length = htons((uint16_t)total);
    udp->checksum = 0;

    memcpy(packet + sizeof(struct UdpHeader), data, len);

    int result = ip_send(dest, IP_PROTO_UDP, packet, total);
    kfree(packet);

    return result;
}
