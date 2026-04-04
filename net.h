#ifndef _NET_H
#define _NET_H

#include "types.h"

#define ETH_ALEN        6
#define ETH_MTU         1500
#define ETH_FRAME_LEN   (ETH_MTU + 14)

#define ETH_TYPE_IP     0x0800
#define ETH_TYPE_ARP    0x0806
#define ETH_TYPE_IPV6   0x86DD

#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

#define ICMP_ECHO_REPLY     0
#define ICMP_ECHO_REQUEST   8

#define TCP_FIN     0x01
#define TCP_SYN     0x02
#define TCP_RST     0x04
#define TCP_PSH     0x08
#define TCP_ACK     0x10
#define TCP_URG     0x20

#define SOCK_STREAM     1
#define SOCK_DGRAM      2
#define SOCK_RAW        3

#define AF_INET         2
#define AF_INET6        10

struct __packed EthHeader {
    uint8_t dest[ETH_ALEN];
    uint8_t src[ETH_ALEN];
    uint16_t type;
};

struct __packed ArpHeader {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t hw_len;
    uint8_t proto_len;
    uint16_t operation;
    uint8_t sender_hw[ETH_ALEN];
    uint8_t sender_ip[4];
    uint8_t target_hw[ETH_ALEN];
    uint8_t target_ip[4];
};

struct __packed IpHeader {
    uint8_t version_ihl;
    uint8_t tos;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t ttl;
    uint8_t protocol;
    uint16_t checksum;
    uint32_t src_addr;
    uint32_t dest_addr;
};

struct __packed IcmpHeader {
    uint8_t type;
    uint8_t code;
    uint16_t checksum;
    uint16_t identifier;
    uint16_t sequence;
};

struct __packed UdpHeader {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t length;
    uint16_t checksum;
};

struct __packed TcpHeader {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t data_offset;
    uint8_t flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
};

struct Ipv4Addr {
    union {
        uint32_t addr;
        uint8_t bytes[4];
    };
};

struct SockaddrIn {
    uint16_t sin_family;
    uint16_t sin_port;
    struct Ipv4Addr sin_addr;
    uint8_t pad[8];
};

struct NetInterface {
    char name[16];
    uint8_t mac[ETH_ALEN];
    struct Ipv4Addr ip;
    struct Ipv4Addr netmask;
    struct Ipv4Addr gateway;
    bool up;
    
    int (*send)(struct NetInterface *, const void *, size_t);
    void *driver_data;
    
    struct NetInterface *next;
};

struct Packet {
    uint8_t *data;
    size_t len;
    struct NetInterface *iface;
    struct Packet *next;
};

void net_init(void);
void net_register_interface(struct NetInterface *iface);
struct NetInterface *net_get_default_interface(void);

void net_receive_packet(struct NetInterface *iface, void *data, size_t len);
int net_send_packet(struct NetInterface *iface, const void *data, size_t len);

uint16_t ip_checksum(const void *data, size_t len);
int ip_send(struct Ipv4Addr dest, uint8_t protocol, const void *data, size_t len);

void icmp_receive(struct IpHeader *ip, void *data, size_t len);
int icmp_send_echo_request(struct Ipv4Addr dest, uint16_t id, uint16_t seq);

void arp_receive(struct NetInterface *iface, struct ArpHeader *arp);
int arp_resolve(struct Ipv4Addr ip, uint8_t *mac);

void udp_receive(struct IpHeader *ip, void *data, size_t len);
int udp_send(struct Ipv4Addr dest, uint16_t src_port, uint16_t dest_port, const void *data, size_t len);

void udp_init(void);
int udp_socket_create(void);
int udp_socket_bind(int sockfd, struct Ipv4Addr addr, uint16_t port);
int udp_socket_sendto(int sockfd, const void *data, size_t len,
                      struct Ipv4Addr dest_addr, uint16_t dest_port);
int udp_socket_recvfrom(int sockfd, void *buf, size_t len,
                        struct Ipv4Addr *src_addr, uint16_t *src_port);
void udp_socket_close(int sockfd);

void socket_init(void);
int sys_socket(int domain, int type, int protocol);
int sys_bind(int sockfd, const struct SockaddrIn *addr);
int sys_sendto(int sockfd, const void *buf, size_t len,
               int flags, const struct SockaddrIn *dest_addr);
int sys_recvfrom(int sockfd, void *buf, size_t len,
                 int flags, struct SockaddrIn *src_addr);
int sys_socket_close(int sockfd);

uint16_t htons(uint16_t val);
uint16_t ntohs(uint16_t val);
uint32_t htonl(uint32_t val);
uint32_t ntohl(uint32_t val);

#define IP4(a, b, c, d) ((uint32_t)((a) | ((b) << 8) | ((c) << 16) | ((d) << 24)))

#endif
