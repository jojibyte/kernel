#include "net.h"
#include "heap.h"
#include "console.h"

static struct NetInterface *interfaces = NULL;

#define ARP_CACHE_SIZE 64
static struct {
    struct Ipv4Addr ip;
    uint8_t mac[ETH_ALEN];
    uint64_t timestamp;
    bool valid;
} arp_cache[ARP_CACHE_SIZE];

static struct {
    struct Ipv4Addr ip;
    uint64_t request_time;
    bool pending;
} arp_pending[16];

static void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = dest;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

static void *memset(void *s, int c, size_t n) {
    uint8_t *p = s;
    while (n--) *p++ = c;
    return s;
}

static int memcmp(const void *s1, const void *s2, size_t n) {
    const uint8_t *p1 = s1, *p2 = s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

uint16_t htons(uint16_t val) {
    return (val >> 8) | (val << 8);
}

uint16_t ntohs(uint16_t val) {
    return htons(val);
}

uint32_t htonl(uint32_t val) {
    return ((val >> 24) & 0x000000FF) |
           ((val >> 8)  & 0x0000FF00) |
           ((val << 8)  & 0x00FF0000) |
           ((val << 24) & 0xFF000000);
}

uint32_t ntohl(uint32_t val) {
    return htonl(val);
}

uint16_t ip_checksum(const void *data, size_t len) {
    const uint16_t *ptr = data;
    uint32_t sum = 0;
    
    while (len > 1) {
        sum += *ptr++;
        len -= 2;
    }
    
    if (len == 1) {
        sum += *(const uint8_t *)ptr;
    }
    
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    return ~sum;
}

void net_init(void) {
    memset(arp_cache, 0, sizeof(arp_cache));
    memset(arp_pending, 0, sizeof(arp_pending));
    
    struct NetInterface *lo = kzalloc(sizeof(struct NetInterface));
    if (lo) {
        memcpy(lo->name, "lo", 3);
        lo->ip.addr = IP4(127, 0, 0, 1);
        lo->netmask.addr = IP4(255, 0, 0, 0);
        lo->up = true;
        net_register_interface(lo);
    }
}

void net_register_interface(struct NetInterface *iface) {
    if (!iface) return;
    
    iface->next = interfaces;
    interfaces = iface;
    
    kprintf("[NET]  Registered interface %s\n", iface->name);
    kprintf("       IP: %d.%d.%d.%d\n",
            iface->ip.bytes[0], iface->ip.bytes[1],
            iface->ip.bytes[2], iface->ip.bytes[3]);
}

struct NetInterface *net_get_default_interface(void) {
    struct NetInterface *iface = interfaces;
    while (iface) {
        if (iface->up && iface->gateway.addr != 0) {
            return iface;
        }
        iface = iface->next;
    }
    return interfaces;
}

void net_receive_packet(struct NetInterface *iface, void *data, size_t len) {
    if (len < sizeof(struct EthHeader)) return;
    
    struct EthHeader *eth = data;
    uint16_t type = ntohs(eth->type);
    void *payload = (uint8_t *)data + sizeof(struct EthHeader);
    size_t payload_len = len - sizeof(struct EthHeader);
    
    switch (type) {
    case ETH_TYPE_ARP:
        if (payload_len >= sizeof(struct ArpHeader)) {
            arp_receive(iface, payload);
        }
        break;
        
    case ETH_TYPE_IP:
        if (payload_len >= sizeof(struct IpHeader)) {
            struct IpHeader *ip = payload;
            uint8_t ihl = (ip->version_ihl & 0x0F) * 4;
            void *ip_payload = (uint8_t *)payload + ihl;
            size_t ip_payload_len = ntohs(ip->total_length) - ihl;
            
            switch (ip->protocol) {
            case IP_PROTO_ICMP:
                icmp_receive(ip, ip_payload, ip_payload_len);
                break;
            case IP_PROTO_TCP:
                break;
            case IP_PROTO_UDP:
                break;
            }
        }
        break;
    }
}

int net_send_packet(struct NetInterface *iface, const void *data, size_t len) {
    if (!iface || !iface->send) return -ENODEV;
    return iface->send(iface, data, len);
}

void arp_receive(struct NetInterface *iface, struct ArpHeader *arp) {
    uint16_t op = ntohs(arp->operation);
    
    if (memcmp(arp->target_ip, &iface->ip.bytes, 4) != 0) {
        return;
    }
    
    struct Ipv4Addr sender_ip;
    memcpy(&sender_ip.bytes, arp->sender_ip, 4);
    
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) {
            arp_cache[i].ip = sender_ip;
            memcpy(arp_cache[i].mac, arp->sender_hw, ETH_ALEN);
            arp_cache[i].valid = true;
            break;
        }
    }
    
    if (op == 1) {
        uint8_t reply[sizeof(struct EthHeader) + sizeof(struct ArpHeader)];
        struct EthHeader *eth = (struct EthHeader *)reply;
        struct ArpHeader *arp_reply = (struct ArpHeader *)(reply + sizeof(struct EthHeader));
        
        memcpy(eth->dest, arp->sender_hw, ETH_ALEN);
        memcpy(eth->src, iface->mac, ETH_ALEN);
        eth->type = htons(ETH_TYPE_ARP);
        
        arp_reply->hw_type = htons(1);
        arp_reply->proto_type = htons(ETH_TYPE_IP);
        arp_reply->hw_len = ETH_ALEN;
        arp_reply->proto_len = 4;
        arp_reply->operation = htons(2);
        memcpy(arp_reply->sender_hw, iface->mac, ETH_ALEN);
        memcpy(arp_reply->sender_ip, &iface->ip.bytes, 4);
        memcpy(arp_reply->target_hw, arp->sender_hw, ETH_ALEN);
        memcpy(arp_reply->target_ip, arp->sender_ip, 4);
        
        net_send_packet(iface, reply, sizeof(reply));
    }
}

int arp_resolve(struct Ipv4Addr ip, uint8_t *mac) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip.addr == ip.addr) {
            memcpy(mac, arp_cache[i].mac, ETH_ALEN);
            return 0;
        }
    }
    
    return -EAGAIN;
}

void icmp_receive(struct IpHeader *ip, void *data, size_t len) {
    if (len < sizeof(struct IcmpHeader)) return;
    
    struct IcmpHeader *icmp = data;
    
    if (icmp->type == ICMP_ECHO_REQUEST) {
        struct NetInterface *iface = net_get_default_interface();
        if (!iface) return;
        
        size_t reply_len = sizeof(struct EthHeader) + sizeof(struct IpHeader) + len;
        uint8_t *reply = kmalloc(reply_len);
        if (!reply) return;
        
        struct EthHeader *eth = (struct EthHeader *)reply;
        struct IpHeader *ip_reply = (struct IpHeader *)(reply + sizeof(struct EthHeader));
        struct IcmpHeader *icmp_reply = (struct IcmpHeader *)(reply + sizeof(struct EthHeader) + sizeof(struct IpHeader));
        
        uint8_t dest_mac[ETH_ALEN];
        struct Ipv4Addr src_ip;
        src_ip.addr = ip->src_addr;
        
        if (arp_resolve(src_ip, dest_mac) < 0) {
            memset(dest_mac, 0xFF, ETH_ALEN);
        }
        
        memcpy(eth->dest, dest_mac, ETH_ALEN);
        memcpy(eth->src, iface->mac, ETH_ALEN);
        eth->type = htons(ETH_TYPE_IP);
        
        ip_reply->version_ihl = 0x45;
        ip_reply->tos = 0;
        ip_reply->total_length = htons(sizeof(struct IpHeader) + len);
        ip_reply->identification = 0;
        ip_reply->flags_fragment = 0;
        ip_reply->ttl = 64;
        ip_reply->protocol = IP_PROTO_ICMP;
        ip_reply->checksum = 0;
        ip_reply->src_addr = iface->ip.addr;
        ip_reply->dest_addr = ip->src_addr;
        ip_reply->checksum = ip_checksum(ip_reply, sizeof(struct IpHeader));
        
        memcpy(icmp_reply, data, len);
        icmp_reply->type = ICMP_ECHO_REPLY;
        icmp_reply->checksum = 0;
        icmp_reply->checksum = ip_checksum(icmp_reply, len);
        
        net_send_packet(iface, reply, reply_len);
        kfree(reply);
    } else if (icmp->type == ICMP_ECHO_REPLY) {
        kprintf("[NET]  ICMP echo reply from %d.%d.%d.%d\n",
                (ip->src_addr) & 0xFF,
                (ip->src_addr >> 8) & 0xFF,
                (ip->src_addr >> 16) & 0xFF,
                (ip->src_addr >> 24) & 0xFF);
    }
}

int icmp_send_echo_request(struct Ipv4Addr dest, uint16_t id, uint16_t seq) {
    struct NetInterface *iface = net_get_default_interface();
    if (!iface) return -ENODEV;
    
    size_t packet_len = sizeof(struct EthHeader) + sizeof(struct IpHeader) + 
                        sizeof(struct IcmpHeader) + 56;
    
    uint8_t *packet = kzalloc(packet_len);
    if (!packet) return -ENOMEM;
    
    struct EthHeader *eth = (struct EthHeader *)packet;
    struct IpHeader *ip = (struct IpHeader *)(packet + sizeof(struct EthHeader));
    struct IcmpHeader *icmp = (struct IcmpHeader *)(packet + sizeof(struct EthHeader) + sizeof(struct IpHeader));
    
    memset(eth->dest, 0xFF, ETH_ALEN);
    memcpy(eth->src, iface->mac, ETH_ALEN);
    eth->type = htons(ETH_TYPE_IP);
    
    ip->version_ihl = 0x45;
    ip->tos = 0;
    ip->total_length = htons(sizeof(struct IpHeader) + sizeof(struct IcmpHeader) + 56);
    ip->identification = htons(id);
    ip->flags_fragment = 0;
    ip->ttl = 64;
    ip->protocol = IP_PROTO_ICMP;
    ip->checksum = 0;
    ip->src_addr = iface->ip.addr;
    ip->dest_addr = dest.addr;
    ip->checksum = ip_checksum(ip, sizeof(struct IpHeader));
    
    icmp->type = ICMP_ECHO_REQUEST;
    icmp->code = 0;
    icmp->identifier = htons(id);
    icmp->sequence = htons(seq);
    icmp->checksum = 0;
    icmp->checksum = ip_checksum(icmp, sizeof(struct IcmpHeader) + 56);
    
    int ret = net_send_packet(iface, packet, packet_len);
    kfree(packet);
    
    return ret;
}
