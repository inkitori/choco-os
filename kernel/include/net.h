#ifndef NET_H
#define NET_H

#include <stdint.h>
#include <stdbool.h>

// IPv4 addresses are kept in network byte order (as they appear on the wire).

void net_init(void); // probe NIC + start the RX thread (safe if no NIC)
bool net_up(void);	 // NIC present
bool net_configured(void); // DHCP completed

bool net_dhcp(uint32_t timeout_ms);

uint32_t net_ip(void);
uint32_t net_mask(void);
uint32_t net_gateway(void);
uint32_t net_dns_server(void);
void net_mac(uint8_t out[6]);

// Returns RTT in ms, or -1 on timeout / error.
int net_ping(uint32_t ip, uint16_t seq, uint32_t timeout_ms);

bool net_resolve(const char *name, uint32_t *ip_out, uint32_t timeout_ms);

void ip_to_str(uint32_t ip, char *buf); // buf >= 16 bytes
bool str_to_ip(const char *s, uint32_t *ip_out);

#endif
