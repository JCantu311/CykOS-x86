#include "net.h"
#include "modload.h"
#include "module.h"
#include "cykastd.h"
#include "idt.h"
#include "sched.h"

#define ETH_TYPE_ARP   0x0806u
#define ETH_TYPE_IPV4  0x0800u

#define IP_PROTO_ICMP  1u
#define IP_PROTO_UDP   17u

#define ICMP_TYPE_ECHO_REPLY   0u
#define ICMP_TYPE_ECHO_REQUEST 8u

#define ARP_OP_REQUEST 1u
#define ARP_OP_REPLY   2u

#define ARP_CACHE_SIZE 16u
#define NET_FRAME_MAX  1536u

typedef struct __attribute__((packed)) {
    unsigned char dst[6];
    unsigned char src[6];
    unsigned short ethertype;
} eth_hdr_t;

typedef struct __attribute__((packed)) {
    unsigned short htype;
    unsigned short ptype;
    unsigned char hlen;
    unsigned char plen;
    unsigned short oper;
    unsigned char sha[6];
    unsigned int spa;
    unsigned char tha[6];
    unsigned int tpa;
} arp_pkt_t;

typedef struct __attribute__((packed)) {
    unsigned char ver_ihl;
    unsigned char tos;
    unsigned short total_len;
    unsigned short id;
    unsigned short frag_off;
    unsigned char ttl;
    unsigned char proto;
    unsigned short checksum;
    unsigned int src_ip;
    unsigned int dst_ip;
} ipv4_hdr_t;

typedef struct __attribute__((packed)) {
    unsigned char type;
    unsigned char code;
    unsigned short checksum;
    unsigned short id;
    unsigned short seq;
} icmp_hdr_t;

typedef struct __attribute__((packed)) {
    unsigned short src_port;
    unsigned short dst_port;
    unsigned short len;
    unsigned short checksum;
} udp_hdr_t;

typedef struct __attribute__((packed)) {
    unsigned char op;
    unsigned char htype;
    unsigned char hlen;
    unsigned char hops;
    unsigned int xid;
    unsigned short secs;
    unsigned short flags;
    unsigned int ciaddr;
    unsigned int yiaddr;
    unsigned int siaddr;
    unsigned int giaddr;
    unsigned char chaddr[16];
    unsigned char sname[64];
    unsigned char file[128];
    unsigned int magic;
    unsigned char options[308];
} dhcp_pkt_t;

typedef struct {
    unsigned int ip;
    unsigned char mac[6];
    unsigned char valid;
} arp_entry_t;

static inline unsigned short htons(unsigned short v) {
    return (unsigned short)((v << 8) | (v >> 8));
}

static inline unsigned short ntohs(unsigned short v) {
    return htons(v);
}

static inline unsigned int htonl(unsigned int v) {
    return ((v & 0x000000FFu) << 24) |
           ((v & 0x0000FF00u) << 8)  |
           ((v & 0x00FF0000u) >> 8)  |
           ((v & 0xFF000000u) >> 24);
}

static inline unsigned int ntohl(unsigned int v) {
    return htonl(v);
}

static cyk_net_driver_t* g_drv = (void*)0;
static unsigned char g_mac[6];
static unsigned int g_ip = 0;
static unsigned int g_netmask = 0;
static unsigned int g_gateway = 0;
static unsigned int g_dns = 0;
static unsigned int g_rx_packets = 0;
static unsigned int g_tx_packets = 0;
static unsigned short g_ip_id = 1;

static arp_entry_t g_arp_cache[ARP_CACHE_SIZE];

/* Ping reply tracking */
static volatile int g_ping_reply_received = 0;
static volatile unsigned short g_ping_expected_id = 0;
static volatile unsigned short g_ping_expected_seq = 0;
static volatile unsigned int g_ping_expected_ip = 0;

/* DHCP state tracking */
static volatile int g_dhcp_state = 0; /* 0=idle, 1=wait offer, 2=got offer, 3=wait ack, 4=got ack */
static unsigned int g_dhcp_xid = 0x43594B01u;
static unsigned int g_dhcp_offered_ip = 0;
static unsigned int g_dhcp_server_ip = 0;
static unsigned int g_dhcp_offered_mask = 0;
static unsigned int g_dhcp_offered_gw = 0;
static unsigned int g_dhcp_offered_dns = 0;

static unsigned short inet_checksum(const void* data, unsigned int len) {
    const unsigned short* p = (const unsigned short*)data;
    unsigned int sum = 0;
    while (len > 1u) {
        sum += *p++;
        len -= 2u;
    }
    if (len == 1u) {
        sum += *(const unsigned char*)p;
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (unsigned short)(~sum);
}

static void arp_cache_update(unsigned int ip, const unsigned char mac[6]) {
    if (ip == 0) return;
    for (unsigned int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            memcpy(g_arp_cache[i].mac, mac, 6u);
            return;
        }
    }
    for (unsigned int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!g_arp_cache[i].valid) {
            g_arp_cache[i].ip = ip;
            memcpy(g_arp_cache[i].mac, mac, 6u);
            g_arp_cache[i].valid = 1;
            return;
        }
    }
    g_arp_cache[0].ip = ip;
    memcpy(g_arp_cache[0].mac, mac, 6u);
    g_arp_cache[0].valid = 1;
}

static int arp_cache_lookup(unsigned int ip, unsigned char out_mac[6]) {
    for (unsigned int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (g_arp_cache[i].valid && g_arp_cache[i].ip == ip) {
            memcpy(out_mac, g_arp_cache[i].mac, 6u);
            return 0;
        }
    }
    return -1;
}

static int net_send_raw(const unsigned char dst_mac[6], unsigned short ethertype,
                        const unsigned char* payload, unsigned int len) {
    if (!g_drv || !g_drv->send_frame) return -1;
    if (sizeof(eth_hdr_t) + len > NET_FRAME_MAX) return -1;

    unsigned char frame[NET_FRAME_MAX];
    eth_hdr_t* eth = (eth_hdr_t*)frame;
    memcpy(eth->dst, dst_mac, 6u);
    memcpy(eth->src, g_mac, 6u);
    eth->ethertype = htons(ethertype);
    memcpy(frame + sizeof(eth_hdr_t), payload, len);

    unsigned int total = sizeof(eth_hdr_t) + len;
    if (total < 60u) {
        memset(frame + total, 0, 60u - total);
        total = 60u;
    }

    if (g_drv->send_frame(frame, total) == 0) {
        g_tx_packets++;
        return 0;
    }
    return -1;
}

static void net_send_arp_request(unsigned int target_ip) {
    unsigned char bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    arp_pkt_t arp;
    arp.htype = htons(1u);
    arp.ptype = htons(ETH_TYPE_IPV4);
    arp.hlen = 6u;
    arp.plen = 4u;
    arp.oper = htons(ARP_OP_REQUEST);
    memcpy(arp.sha, g_mac, 6u);
    arp.spa = htonl(g_ip);
    memset(arp.tha, 0, 6u);
    arp.tpa = htonl(target_ip);
    (void)net_send_raw(bcast, ETH_TYPE_ARP, (const unsigned char*)&arp, sizeof(arp));
}

static int net_resolve_mac(unsigned int dst_ip, unsigned char out_mac[6]) {
    if (dst_ip == 0xFFFFFFFFu) {
        memset(out_mac, 0xFF, 6u);
        return 0;
    }

    unsigned int next_hop = dst_ip;
    if (g_netmask != 0 && (dst_ip & g_netmask) != (g_ip & g_netmask)) {
        if (g_gateway == 0) return -1;
        next_hop = g_gateway;
    }

    if (arp_cache_lookup(next_hop, out_mac) == 0) {
        return 0;
    }

    for (int attempt = 0; attempt < 3; attempt++) {
        net_send_arp_request(next_hop);
        unsigned int start = idt_get_millis();
        while ((idt_get_millis() - start) < 400u) {
            net_poll();
            if (arp_cache_lookup(next_hop, out_mac) == 0) {
                return 0;
            }
            sched_sleep_ms(5u);
        }
    }
    return -1;
}

static int net_send_ipv4(unsigned int dst_ip, unsigned char proto,
                         const unsigned char* payload, unsigned int len) {
    if (sizeof(ipv4_hdr_t) + len > 1500u) return -1;

    unsigned char dst_mac[6];
    if (net_resolve_mac(dst_ip, dst_mac) != 0) return -1;

    unsigned char buf[1500];
    ipv4_hdr_t* ip = (ipv4_hdr_t*)buf;
    ip->ver_ihl = 0x45u;
    ip->tos = 0;
    ip->total_len = htons((unsigned short)(sizeof(ipv4_hdr_t) + len));
    ip->id = htons(g_ip_id++);
    ip->frag_off = 0;
    ip->ttl = 64u;
    ip->proto = proto;
    ip->checksum = 0;
    ip->src_ip = htonl(g_ip);
    ip->dst_ip = htonl(dst_ip);
    ip->checksum = inet_checksum(ip, sizeof(ipv4_hdr_t));
    memcpy(buf + sizeof(ipv4_hdr_t), payload, len);

    return net_send_raw(dst_mac, ETH_TYPE_IPV4, buf, sizeof(ipv4_hdr_t) + len);
}

static void net_handle_arp(const unsigned char* pkt, unsigned int len) {
    if (len < sizeof(arp_pkt_t)) return;
    const arp_pkt_t* arp = (const arp_pkt_t*)pkt;
    if (ntohs(arp->htype) != 1u || ntohs(arp->ptype) != ETH_TYPE_IPV4) return;
    if (arp->hlen != 6u || arp->plen != 4u) return;

    unsigned int spa = ntohl(arp->spa);
    unsigned int tpa = ntohl(arp->tpa);
    arp_cache_update(spa, arp->sha);

    if (ntohs(arp->oper) == ARP_OP_REQUEST && g_ip != 0 && tpa == g_ip) {
        arp_pkt_t reply;
        reply.htype = htons(1u);
        reply.ptype = htons(ETH_TYPE_IPV4);
        reply.hlen = 6u;
        reply.plen = 4u;
        reply.oper = htons(ARP_OP_REPLY);
        memcpy(reply.sha, g_mac, 6u);
        reply.spa = htonl(g_ip);
        memcpy(reply.tha, arp->sha, 6u);
        reply.tpa = arp->spa;
        (void)net_send_raw(arp->sha, ETH_TYPE_ARP, (const unsigned char*)&reply, sizeof(reply));
    }
}

static void net_handle_icmp(unsigned int src_ip, const unsigned char* pkt, unsigned int len) {
    if (len < sizeof(icmp_hdr_t)) return;
    const icmp_hdr_t* icmp = (const icmp_hdr_t*)pkt;

    if (icmp->type == ICMP_TYPE_ECHO_REPLY) {
        unsigned short id = ntohs(icmp->id);
        unsigned short seq = ntohs(icmp->seq);
        if (src_ip == g_ping_expected_ip && id == g_ping_expected_id && seq == g_ping_expected_seq) {
            g_ping_reply_received = 1;
        }
    } else if (icmp->type == ICMP_TYPE_ECHO_REQUEST && g_ip != 0) {
        if (len > 1480u) return;
        unsigned char reply_buf[1480];
        memcpy(reply_buf, pkt, len);
        icmp_hdr_t* reply = (icmp_hdr_t*)reply_buf;
        reply->type = ICMP_TYPE_ECHO_REPLY;
        reply->code = 0;
        reply->checksum = 0;
        reply->checksum = inet_checksum(reply_buf, len);
        (void)net_send_ipv4(src_ip, IP_PROTO_ICMP, reply_buf, len);
    }
}

static void net_handle_dhcp_packet(const unsigned char* payload, unsigned int len) {
    if (len < 240u) return;
    const dhcp_pkt_t* dhcp = (const dhcp_pkt_t*)payload;
    if (dhcp->op != 2u) return; /* BOOTREPLY */
    if (ntohl(dhcp->xid) != g_dhcp_xid) return;
    if (ntohl(dhcp->magic) != 0x63825363u) return;

    unsigned char msg_type = 0;
    unsigned int mask = 0;
    unsigned int gw = 0;
    unsigned int dns = 0;
    unsigned int srv = 0;

    unsigned int opt_len = len - 240u;
    unsigned int i = 0;
    while (i < opt_len) {
        unsigned char opt = dhcp->options[i++];
        if (opt == 0u) continue;
        if (opt == 255u) break;
        if (i >= opt_len) break;
        unsigned char olen = dhcp->options[i++];
        if (i + olen > opt_len) break;

        if (opt == 53u && olen >= 1u) {
            msg_type = dhcp->options[i];
        } else if (opt == 1u && olen >= 4u) {
            mask = ((unsigned int)dhcp->options[i] << 24) |
                   ((unsigned int)dhcp->options[i + 1] << 16) |
                   ((unsigned int)dhcp->options[i + 2] << 8) |
                   (unsigned int)dhcp->options[i + 3];
        } else if (opt == 3u && olen >= 4u) {
            gw = ((unsigned int)dhcp->options[i] << 24) |
                 ((unsigned int)dhcp->options[i + 1] << 16) |
                 ((unsigned int)dhcp->options[i + 2] << 8) |
                 (unsigned int)dhcp->options[i + 3];
        } else if (opt == 6u && olen >= 4u) {
            dns = ((unsigned int)dhcp->options[i] << 24) |
                  ((unsigned int)dhcp->options[i + 1] << 16) |
                  ((unsigned int)dhcp->options[i + 2] << 8) |
                  (unsigned int)dhcp->options[i + 3];
        } else if (opt == 54u && olen >= 4u) {
            srv = ((unsigned int)dhcp->options[i] << 24) |
                  ((unsigned int)dhcp->options[i + 1] << 16) |
                  ((unsigned int)dhcp->options[i + 2] << 8) |
                  (unsigned int)dhcp->options[i + 3];
        }
        i += olen;
    }

    if (msg_type == 2u && g_dhcp_state == 1) { /* DHCPOFFER */
        g_dhcp_offered_ip = ntohl(dhcp->yiaddr);
        g_dhcp_server_ip = srv ? srv : ntohl(dhcp->siaddr);
        g_dhcp_offered_mask = mask ? mask : 0xFFFFFF00u;
        g_dhcp_offered_gw = gw;
        g_dhcp_offered_dns = dns;
        g_dhcp_state = 2;
    } else if (msg_type == 5u && g_dhcp_state == 3) { /* DHCPACK */
        g_dhcp_offered_ip = ntohl(dhcp->yiaddr);
        if (mask) g_dhcp_offered_mask = mask;
        if (gw) g_dhcp_offered_gw = gw;
        if (dns) g_dhcp_offered_dns = dns;
        g_dhcp_state = 4;
    }
}

static void net_handle_udp(const unsigned char* pkt, unsigned int len) {
    if (len < sizeof(udp_hdr_t)) return;
    const udp_hdr_t* udp = (const udp_hdr_t*)pkt;
    unsigned short dst_port = ntohs(udp->dst_port);
    unsigned short src_port = ntohs(udp->src_port);
    unsigned short udp_len = ntohs(udp->len);
    if (udp_len < sizeof(udp_hdr_t) || udp_len > len) return;

    if (dst_port == 68u && src_port == 67u) {
        net_handle_dhcp_packet(pkt + sizeof(udp_hdr_t), udp_len - sizeof(udp_hdr_t));
    }
}

static void net_handle_ipv4(const unsigned char* pkt, unsigned int len) {
    if (len < sizeof(ipv4_hdr_t)) return;
    const ipv4_hdr_t* ip = (const ipv4_hdr_t*)pkt;
    unsigned int ihl = (unsigned int)(ip->ver_ihl & 0x0Fu) * 4u;
    if ((ip->ver_ihl >> 4) != 4u || ihl < sizeof(ipv4_hdr_t) || ihl > len) return;

    unsigned short total_len = ntohs(ip->total_len);
    if (total_len < ihl || total_len > len) return;

    unsigned int src_ip = ntohl(ip->src_ip);
    unsigned int dst_ip = ntohl(ip->dst_ip);
    if (g_ip != 0 && dst_ip != g_ip && dst_ip != 0xFFFFFFFFu) {
        return;
    }

    const unsigned char* payload = pkt + ihl;
    unsigned int payload_len = total_len - ihl;

    if (ip->proto == IP_PROTO_ICMP) {
        net_handle_icmp(src_ip, payload, payload_len);
    } else if (ip->proto == IP_PROTO_UDP) {
        net_handle_udp(payload, payload_len);
    }
}

void net_poll(void) {
    if (!g_drv || !g_drv->receive_frame) return;
    unsigned char frame[NET_FRAME_MAX];
    for (int i = 0; i < 16; i++) {
        int len = g_drv->receive_frame(frame, sizeof(frame));
        if (len <= 0) break;
        g_rx_packets++;
        if ((unsigned int)len < sizeof(eth_hdr_t)) continue;
        const eth_hdr_t* eth = (const eth_hdr_t*)frame;
        unsigned short etype = ntohs(eth->ethertype);
        const unsigned char* payload = frame + sizeof(eth_hdr_t);
        unsigned int plen = (unsigned int)len - sizeof(eth_hdr_t);

        if (etype == ETH_TYPE_ARP) {
            net_handle_arp(payload, plen);
        } else if (etype == ETH_TYPE_IPV4) {
            net_handle_ipv4(payload, plen);
        }
    }
}

int net_udp_send(unsigned int dst_ip, unsigned short src_port, unsigned short dst_port,
                 const unsigned char* payload, unsigned int len) {
    if (sizeof(udp_hdr_t) + len > 1480u) return -1;
    unsigned char buf[1480];
    udp_hdr_t* udp = (udp_hdr_t*)buf;
    udp->src_port = htons(src_port);
    udp->dst_port = htons(dst_port);
    udp->len = htons((unsigned short)(sizeof(udp_hdr_t) + len));
    udp->checksum = 0; /* Optional in IPv4 UDP */
    if (payload && len > 0) {
        memcpy(buf + sizeof(udp_hdr_t), payload, len);
    }
    return net_send_ipv4(dst_ip, IP_PROTO_UDP, buf, sizeof(udp_hdr_t) + len);
}

static int net_send_dhcp_msg(unsigned char msg_type, unsigned int req_ip, unsigned int srv_ip) {
    dhcp_pkt_t* pkt = (dhcp_pkt_t*)calloc(1u, sizeof(dhcp_pkt_t));
    if (!pkt) return -1;

    pkt->op = 1u; /* BOOTREQUEST */
    pkt->htype = 1u;
    pkt->hlen = 6u;
    pkt->xid = htonl(g_dhcp_xid);
    pkt->flags = htons(0x8000u); /* Broadcast flag */
    memcpy(pkt->chaddr, g_mac, 6u);
    pkt->magic = htonl(0x63825363u);

    unsigned int idx = 0;
    pkt->options[idx++] = 53u; /* DHCP Message Type */
    pkt->options[idx++] = 1u;
    pkt->options[idx++] = msg_type;

    if (msg_type == 3u && req_ip != 0) {
        pkt->options[idx++] = 50u; /* Requested IP */
        pkt->options[idx++] = 4u;
        pkt->options[idx++] = (unsigned char)(req_ip >> 24);
        pkt->options[idx++] = (unsigned char)(req_ip >> 16);
        pkt->options[idx++] = (unsigned char)(req_ip >> 8);
        pkt->options[idx++] = (unsigned char)(req_ip);

        if (srv_ip != 0) {
            pkt->options[idx++] = 54u; /* Server ID */
            pkt->options[idx++] = 4u;
            pkt->options[idx++] = (unsigned char)(srv_ip >> 24);
            pkt->options[idx++] = (unsigned char)(srv_ip >> 16);
            pkt->options[idx++] = (unsigned char)(srv_ip >> 8);
            pkt->options[idx++] = (unsigned char)(srv_ip);
        }
    }

    pkt->options[idx++] = 55u; /* Parameter Request List */
    pkt->options[idx++] = 3u;
    pkt->options[idx++] = 1u;  /* Subnet Mask */
    pkt->options[idx++] = 3u;  /* Router */
    pkt->options[idx++] = 6u;  /* DNS */
    pkt->options[idx++] = 255u;

    int res = net_udp_send(0xFFFFFFFFu, 68u, 67u, (const unsigned char*)pkt, 240u + idx);
    free(pkt);
    return res;
}

int net_dhcp(void) {
    if (!g_drv) return -1;

    g_dhcp_xid++;
    g_dhcp_offered_ip = 0;
    g_dhcp_server_ip = 0;
    g_dhcp_offered_mask = 0;
    g_dhcp_offered_gw = 0;
    g_dhcp_offered_dns = 0;
    g_dhcp_state = 1;

    if (net_send_dhcp_msg(1u, 0, 0) != 0) { /* DHCPDISCOVER */
        g_dhcp_state = 0;
        return -1;
    }

    unsigned int start = idt_get_millis();
    while ((idt_get_millis() - start) < 2000u) {
        net_poll();
        if (g_dhcp_state == 2) break;
        sched_sleep_ms(5u);
    }

    if (g_dhcp_state != 2 || g_dhcp_offered_ip == 0) {
        g_dhcp_state = 0;
        return -1;
    }

    g_dhcp_state = 3;
    if (net_send_dhcp_msg(3u, g_dhcp_offered_ip, g_dhcp_server_ip) != 0) { /* DHCPREQUEST */
        g_dhcp_state = 0;
        return -1;
    }

    start = idt_get_millis();
    while ((idt_get_millis() - start) < 2000u) {
        net_poll();
        if (g_dhcp_state == 4) break;
        sched_sleep_ms(5u);
    }

    if (g_dhcp_state != 4) {
        g_dhcp_state = 0;
        return -1;
    }

    g_ip = g_dhcp_offered_ip;
    g_netmask = g_dhcp_offered_mask ? g_dhcp_offered_mask : 0xFFFFFF00u;
    g_gateway = g_dhcp_offered_gw;
    g_dns = g_dhcp_offered_dns;
    g_dhcp_state = 0;
    return 0;
}

int net_ping(unsigned int dst_ip, unsigned short seq, unsigned int timeout_ms, unsigned int* rtt_ms_out) {
    if (!g_drv || g_ip == 0) return -1;

    unsigned char pkt[64];
    icmp_hdr_t* icmp = (icmp_hdr_t*)pkt;
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    icmp->checksum = 0;
    icmp->id = htons(0x4359u);
    icmp->seq = htons(seq);
    for (unsigned int i = sizeof(icmp_hdr_t); i < sizeof(pkt); i++) {
        pkt[i] = (unsigned char)(i & 0xFFu);
    }
    icmp->checksum = inet_checksum(pkt, sizeof(pkt));

    g_ping_expected_ip = dst_ip;
    g_ping_expected_id = 0x4359u;
    g_ping_expected_seq = seq;
    g_ping_reply_received = 0;

    unsigned int start = idt_get_millis();
    if (net_send_ipv4(dst_ip, IP_PROTO_ICMP, pkt, sizeof(pkt)) != 0) {
        return -1;
    }

    while ((idt_get_millis() - start) < timeout_ms) {
        net_poll();
        if (g_ping_reply_received) {
            if (rtt_ms_out) {
                *rtt_ms_out = idt_get_millis() - start;
            }
            return 0;
        }
        sched_sleep_ms(2u);
    }
    return -1;
}

int net_set_config(unsigned int ip, unsigned int netmask, unsigned int gateway) {
    if (!g_drv) return -1;
    g_ip = ip;
    g_netmask = netmask;
    g_gateway = gateway;
    return 0;
}

int net_is_available(void) {
    return g_drv != (void*)0;
}

int net_get_info(net_info_t* out) {
    if (!out) return -1;
    memset(out, 0, sizeof(*out));
    if (!g_drv) return -1;

    out->available = 1;
    out->link_up = g_drv->link_up ? g_drv->link_up() : 0;
    memcpy(out->mac, g_mac, 6u);
    out->ip = g_ip;
    out->netmask = g_netmask;
    out->gateway = g_gateway;
    out->dns = g_dns;
    out->rx_packets = g_rx_packets;
    out->tx_packets = g_tx_packets;
    memcpy(out->hw_name, g_drv->hardware_name, sizeof(out->hw_name));
    out->hw_name[sizeof(out->hw_name) - 1u] = '\0';
    return 0;
}

void net_init(void) {
    memset(g_arp_cache, 0, sizeof(g_arp_cache));
    g_drv = modload_get_net_driver();
    if (!g_drv) {
        print_string("[NET] No network driver loaded.\n");
        return;
    }

    if (g_drv->get_mac) {
        g_drv->get_mac(g_mac);
    }

    print_string("[NET] Network interface ready: ");
    print_string(g_drv->hardware_name);
    print_string("\n");

    if (g_drv->link_up && g_drv->link_up()) {
        if (net_dhcp() == 0) {
            print_color_string("[NET] DHCP lease acquired.\n", VGA_GREEN);
        } else {
            print_color_string("[NET] DHCP timed out; use 'ifconfig' or 'dhcp'.\n", VGA_YELLOW);
        }
    }
}
