#ifndef CYKOS_NET_H
#define CYKOS_NET_H

typedef struct {
    int available;
    int link_up;
    unsigned char mac[6];
    unsigned int ip;       /* Host byte order: e.g. 0x0A00020F = 10.0.2.15 */
    unsigned int netmask;
    unsigned int gateway;
    unsigned int dns;
    unsigned int rx_packets;
    unsigned int tx_packets;
    char hw_name[32];
} net_info_t;

void net_init(void);
void net_poll(void);
int net_is_available(void);
int net_get_info(net_info_t* out);
int net_set_config(unsigned int ip, unsigned int netmask, unsigned int gateway);
int net_dhcp(void);
int net_ping(unsigned int dst_ip, unsigned short seq, unsigned int timeout_ms, unsigned int* rtt_ms_out);
int net_udp_send(unsigned int dst_ip, unsigned short src_port, unsigned short dst_port,
                 const unsigned char* payload, unsigned int len);

#endif /* CYKOS_NET_H */
