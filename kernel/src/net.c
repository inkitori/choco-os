// Small IPv4 stack over the e1000: Ethernet, ARP, ICMP echo, UDP,
// a DHCP client and a DNS A-record resolver. One RX thread handles all
// incoming frames; blocking calls poll shared state while yielding.
#include "net.h"
#include "e1000.h"
#include "string.h"
#include "kprintf.h"
#include "sched.h"
#include "timer.h"
#include "malloc.h"

#define ETH_ARP 0x0806
#define ETH_IP 0x0800

#define IP_PROTO_ICMP 1
#define IP_PROTO_UDP 17

#define FRAME_MAX 1600

static bool nic_up = false;
static bool configured = false;
static uint8_t our_mac[6];
static uint32_t our_ip = 0, our_mask = 0, our_gw = 0, our_dns = 0;

static inline uint16_t htons16(uint16_t v) { return (v >> 8) | (v << 8); }
static inline uint32_t htonl32(uint32_t v)
{
	return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v >> 8) & 0xFF00) |
		   (v >> 24);
}

// ---------------- wire formats ----------------

typedef struct
{
	uint8_t dst[6], src[6];
	uint16_t ethertype;
} __attribute__((packed)) EthHdr;

typedef struct
{
	uint16_t htype, ptype;
	uint8_t hlen, plen;
	uint16_t op;
	uint8_t sha[6];
	uint32_t spa;
	uint8_t tha[6];
	uint32_t tpa;
} __attribute__((packed)) ArpPkt;

typedef struct
{
	uint8_t ver_ihl, tos;
	uint16_t total_len, id, flags_frag;
	uint8_t ttl, proto;
	uint16_t csum;
	uint32_t src, dst;
} __attribute__((packed)) IpHdr;

typedef struct
{
	uint8_t type, code;
	uint16_t csum, id, seq;
} __attribute__((packed)) IcmpHdr;

typedef struct
{
	uint16_t sport, dport, len, csum;
} __attribute__((packed)) UdpHdr;

static uint16_t checksum16(const void *data, int len, uint32_t start)
{
	uint32_t sum = start;
	const uint8_t *p = data;
	while (len > 1)
	{
		sum += ((uint16_t)p[0] << 8) | p[1];
		p += 2;
		len -= 2;
	}
	if (len)
		sum += (uint16_t)p[0] << 8;
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);
	return htons16(~sum & 0xFFFF);
}

// ---------------- ARP ----------------

#define ARP_CACHE_SIZE 8
static struct
{
	uint32_t ip;
	uint8_t mac[6];
	bool valid;
} arp_cache[ARP_CACHE_SIZE];

static const uint8_t bcast_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

static void arp_cache_put(uint32_t ip, const uint8_t *mac)
{
	for (int i = 0; i < ARP_CACHE_SIZE; i++)
		if (arp_cache[i].valid && arp_cache[i].ip == ip)
		{
			memcpy(arp_cache[i].mac, mac, 6);
			return;
		}
	for (int i = 0; i < ARP_CACHE_SIZE; i++)
		if (!arp_cache[i].valid)
		{
			arp_cache[i].ip = ip;
			memcpy(arp_cache[i].mac, mac, 6);
			arp_cache[i].valid = true;
			return;
		}
	arp_cache[0].ip = ip;
	memcpy(arp_cache[0].mac, mac, 6);
}

static bool arp_cache_get(uint32_t ip, uint8_t *mac)
{
	for (int i = 0; i < ARP_CACHE_SIZE; i++)
		if (arp_cache[i].valid && arp_cache[i].ip == ip)
		{
			memcpy(mac, arp_cache[i].mac, 6);
			return true;
		}
	return false;
}

static void eth_send(const uint8_t *dst_mac, uint16_t ethertype,
					 const void *payload, int len)
{
	uint8_t frame[FRAME_MAX];
	EthHdr *eth = (EthHdr *)frame;
	memcpy(eth->dst, dst_mac, 6);
	memcpy(eth->src, our_mac, 6);
	eth->ethertype = htons16(ethertype);
	memcpy(frame + sizeof(EthHdr), payload, len);
	int total = sizeof(EthHdr) + len;
	if (total < 60)
	{
		memset(frame + total, 0, 60 - total);
		total = 60;
	}
	e1000_send(frame, total);
}

static void arp_request(uint32_t ip)
{
	ArpPkt a = {0};
	a.htype = htons16(1);
	a.ptype = htons16(ETH_IP);
	a.hlen = 6;
	a.plen = 4;
	a.op = htons16(1);
	memcpy(a.sha, our_mac, 6);
	a.spa = our_ip;
	a.tpa = ip;
	eth_send(bcast_mac, ETH_ARP, &a, sizeof(a));
}

// Resolve via cache or ARP request; off-subnet IPs resolve the gateway.
static bool resolve_mac(uint32_t dst_ip, uint8_t *mac, uint32_t timeout_ms)
{
	uint32_t hop = dst_ip;
	if (our_mask && ((dst_ip & our_mask) != (our_ip & our_mask)))
		hop = our_gw;
	if (hop == 0xFFFFFFFF || hop == 0)
	{
		memcpy(mac, bcast_mac, 6);
		return true;
	}

	if (arp_cache_get(hop, mac))
		return true;

	uint64_t deadline = timer_get_ticks() + timeout_ms;
	arp_request(hop);
	while (timer_get_ticks() < deadline)
	{
		if (arp_cache_get(hop, mac))
			return true;
		thread_sleep_ms(5);
	}
	return false;
}

// ---------------- IP / ICMP / UDP send ----------------

static uint16_t ip_id = 1;

static void ip_send(uint32_t dst, uint8_t proto, const void *payload, int len)
{
	uint8_t mac[6];
	if (!resolve_mac(dst, mac, 1000))
		return;

	uint8_t pkt[FRAME_MAX];
	IpHdr *ip = (IpHdr *)pkt;
	ip->ver_ihl = 0x45;
	ip->tos = 0;
	ip->total_len = htons16(sizeof(IpHdr) + len);
	ip->id = htons16(ip_id++);
	ip->flags_frag = htons16(0x4000); // DF
	ip->ttl = 64;
	ip->proto = proto;
	ip->csum = 0;
	ip->src = our_ip;
	ip->dst = dst;
	ip->csum = checksum16(ip, sizeof(IpHdr), 0);
	memcpy(pkt + sizeof(IpHdr), payload, len);
	eth_send(mac, ETH_IP, pkt, sizeof(IpHdr) + len);
}

static void udp_send(uint32_t dst_ip, uint16_t sport, uint16_t dport,
					 const void *payload, int len)
{
	uint8_t pkt[FRAME_MAX];
	UdpHdr *udp = (UdpHdr *)pkt;
	udp->sport = htons16(sport);
	udp->dport = htons16(dport);
	udp->len = htons16(sizeof(UdpHdr) + len);
	udp->csum = 0; // optional for IPv4
	memcpy(pkt + sizeof(UdpHdr), payload, len);
	ip_send(dst_ip, IP_PROTO_UDP, pkt, sizeof(UdpHdr) + len);
}

// ---------------- RX-side shared state ----------------

// ICMP echo replies
static volatile struct
{
	bool waiting;
	uint16_t id, seq;
	volatile bool got;
} icmp_state;

// One UDP "socket": last datagram for a given local port
#define UDP_BUF_SIZE 1024
static volatile struct
{
	uint16_t port; // local port we listen on (0 = off)
	uint8_t data[UDP_BUF_SIZE];
	volatile int len; // 0 = empty
	uint32_t src_ip;
} udp_sock;

static void handle_arp(const uint8_t *payload, int len)
{
	if (len < (int)sizeof(ArpPkt))
		return;
	const ArpPkt *a = (const ArpPkt *)payload;

	arp_cache_put(a->spa, a->sha);

	if (htons16(a->op) == 1 && our_ip && a->tpa == our_ip) // request for us
	{
		ArpPkt r = {0};
		r.htype = htons16(1);
		r.ptype = htons16(ETH_IP);
		r.hlen = 6;
		r.plen = 4;
		r.op = htons16(2);
		memcpy(r.sha, our_mac, 6);
		r.spa = our_ip;
		memcpy(r.tha, a->sha, 6);
		r.tpa = a->spa;
		eth_send(a->sha, ETH_ARP, &r, sizeof(r));
	}
}

static void handle_icmp(const IpHdr *ip, const uint8_t *payload, int len)
{
	if (len < (int)sizeof(IcmpHdr))
		return;
	const IcmpHdr *icmp = (const IcmpHdr *)payload;

	if (icmp->type == 8) // echo request: reply
	{
		uint8_t pkt[FRAME_MAX];
		memcpy(pkt, payload, len);
		IcmpHdr *r = (IcmpHdr *)pkt;
		r->type = 0;
		r->csum = 0;
		r->csum = checksum16(pkt, len, 0);
		ip_send(ip->src, IP_PROTO_ICMP, pkt, len);
		return;
	}

	if (icmp->type == 0 && icmp_state.waiting &&
		icmp->id == icmp_state.id && icmp->seq == icmp_state.seq)
	{
		icmp_state.got = true;
	}
}

static void handle_udp(const IpHdr *ip, const uint8_t *payload, int len)
{
	if (len < (int)sizeof(UdpHdr))
		return;
	const UdpHdr *udp = (const UdpHdr *)payload;
	uint16_t dport = htons16(udp->dport);

	if (udp_sock.port && dport == udp_sock.port && udp_sock.len == 0)
	{
		int dlen = htons16(udp->len) - sizeof(UdpHdr);
		if (dlen > 0 && dlen <= UDP_BUF_SIZE && dlen <= len - (int)sizeof(UdpHdr))
		{
			memcpy((void *)udp_sock.data, payload + sizeof(UdpHdr), dlen);
			udp_sock.src_ip = ip->src;
			udp_sock.len = dlen; // written last: marks ready
		}
	}
}

static void handle_frame(const uint8_t *frame, int len)
{
	if (len < (int)sizeof(EthHdr))
		return;
	const EthHdr *eth = (const EthHdr *)frame;
	uint16_t type = htons16(eth->ethertype);
	const uint8_t *payload = frame + sizeof(EthHdr);
	int plen = len - sizeof(EthHdr);

	if (type == ETH_ARP)
	{
		handle_arp(payload, plen);
		return;
	}
	if (type != ETH_IP || plen < (int)sizeof(IpHdr))
		return;

	const IpHdr *ip = (const IpHdr *)payload;
	int ihl = (ip->ver_ihl & 0xF) * 4;
	int dlen = htons16(ip->total_len) - ihl;
	if (dlen < 0 || dlen > plen - ihl)
		dlen = plen - ihl;

	if (our_ip && ip->dst != our_ip && ip->dst != 0xFFFFFFFF)
		return;

	if (ip->proto == IP_PROTO_ICMP)
		handle_icmp(ip, payload + ihl, dlen);
	else if (ip->proto == IP_PROTO_UDP)
		handle_udp(ip, payload + ihl, dlen);
}

static void net_rx_thread(void *arg)
{
	(void)arg;
	uint8_t frame[FRAME_MAX];
	for (;;)
	{
		int n;
		while ((n = e1000_recv(frame, sizeof(frame))) > 0)
			handle_frame(frame, n);
		thread_sleep_ms(3);
	}
}

// ---------------- DHCP client ----------------

typedef struct
{
	uint8_t op, htype, hlen, hops;
	uint32_t xid;
	uint16_t secs, flags;
	uint32_t ciaddr, yiaddr, siaddr, giaddr;
	uint8_t chaddr[16];
	uint8_t sname[64];
	uint8_t file[128];
	uint32_t magic;
} __attribute__((packed)) DhcpPkt;

#define DHCP_MAGIC 0x63825363

static int dhcp_build(uint8_t *buf, uint8_t msg_type, uint32_t xid,
					  uint32_t req_ip, uint32_t server_ip)
{
	DhcpPkt *d = (DhcpPkt *)buf;
	memset(d, 0, sizeof(DhcpPkt));
	d->op = 1;
	d->htype = 1;
	d->hlen = 6;
	d->xid = xid;
	d->flags = htons16(0x8000); // broadcast replies
	memcpy(d->chaddr, our_mac, 6);
	d->magic = htonl32(DHCP_MAGIC);

	uint8_t *o = buf + sizeof(DhcpPkt);
	*o++ = 53; // message type
	*o++ = 1;
	*o++ = msg_type;
	if (req_ip)
	{
		*o++ = 50; // requested IP
		*o++ = 4;
		memcpy(o, &req_ip, 4);
		o += 4;
	}
	if (server_ip)
	{
		*o++ = 54; // server id
		*o++ = 4;
		memcpy(o, &server_ip, 4);
		o += 4;
	}
	*o++ = 55; // parameter request: mask, router, dns
	*o++ = 3;
	*o++ = 1;
	*o++ = 3;
	*o++ = 6;
	*o++ = 255;
	return o - buf;
}

static bool dhcp_wait(uint32_t xid, uint8_t want_type, DhcpPkt *out,
					  uint8_t *opts, int *opts_len, uint32_t timeout_ms)
{
	uint64_t deadline = timer_get_ticks() + timeout_ms;
	while (timer_get_ticks() < deadline)
	{
		if (udp_sock.len > 0)
		{
			int len = udp_sock.len;
			const DhcpPkt *d = (const DhcpPkt *)udp_sock.data;
			if (len >= (int)sizeof(DhcpPkt) && d->op == 2 && d->xid == xid)
			{
				const uint8_t *o = udp_sock.data + sizeof(DhcpPkt);
				int olen = len - sizeof(DhcpPkt);
				uint8_t type = 0;
				for (int i = 0; i + 1 < olen && o[i] != 255;)
				{
					if (o[i] == 0)
					{
						i++;
						continue;
					}
					if (o[i] == 53)
						type = o[i + 2];
					i += 2 + o[i + 1];
				}
				if (type == want_type)
				{
					memcpy(out, d, sizeof(DhcpPkt));
					if (olen > 312)
						olen = 312;
					memcpy(opts, o, olen);
					*opts_len = olen;
					udp_sock.len = 0;
					return true;
				}
			}
			udp_sock.len = 0; // not ours; drop
		}
		thread_sleep_ms(5);
	}
	return false;
}

static void dhcp_parse_opts(const uint8_t *o, int olen)
{
	for (int i = 0; i + 1 < olen && o[i] != 255;)
	{
		if (o[i] == 0)
		{
			i++;
			continue;
		}
		uint8_t code = o[i], len = o[i + 1];
		const uint8_t *val = &o[i + 2];
		if (code == 1 && len >= 4)
			memcpy(&our_mask, val, 4);
		else if (code == 3 && len >= 4)
			memcpy(&our_gw, val, 4);
		else if (code == 6 && len >= 4)
			memcpy(&our_dns, val, 4);
		i += 2 + len;
	}
}

bool net_dhcp(uint32_t timeout_ms)
{
	if (!nic_up)
		return false;
	if (configured)
		return true;

	uint8_t buf[sizeof(DhcpPkt) + 64];
	DhcpPkt reply;
	uint8_t opts[312];
	int opts_len = 0;
	uint32_t xid = (uint32_t)timer_get_ticks() ^ 0xC0C0A0A0;

	udp_sock.len = 0;
	udp_sock.port = 68;

	int len = dhcp_build(buf, 1 /*DISCOVER*/, xid, 0, 0);
	udp_send(0xFFFFFFFF, 68, 67, buf, len);

	if (!dhcp_wait(xid, 2 /*OFFER*/, &reply, opts, &opts_len, timeout_ms))
	{
		udp_sock.port = 0;
		return false;
	}

	uint32_t offered = reply.yiaddr;
	uint32_t server = 0;
	for (int i = 0; i + 1 < opts_len && opts[i] != 255;)
	{
		if (opts[i] == 0)
		{
			i++;
			continue;
		}
		if (opts[i] == 54)
			memcpy(&server, &opts[i + 2], 4);
		i += 2 + opts[i + 1];
	}

	len = dhcp_build(buf, 3 /*REQUEST*/, xid, offered, server);
	udp_send(0xFFFFFFFF, 68, 67, buf, len);

	if (!dhcp_wait(xid, 5 /*ACK*/, &reply, opts, &opts_len, timeout_ms))
	{
		udp_sock.port = 0;
		return false;
	}

	our_ip = reply.yiaddr;
	dhcp_parse_opts(opts, opts_len);
	udp_sock.port = 0;
	configured = true;

	char a[16], b[16], c[16], d[16];
	ip_to_str(our_ip, a);
	ip_to_str(our_mask, b);
	ip_to_str(our_gw, c);
	ip_to_str(our_dns, d);
	kprintf("net: dhcp ip=%s mask=%s gw=%s dns=%s\n", a, b, c, d);
	return true;
}

// ---------------- ICMP ping ----------------

int net_ping(uint32_t ip, uint16_t seq, uint32_t timeout_ms)
{
	if (!configured)
		return -1;

	uint8_t pkt[sizeof(IcmpHdr) + 32];
	IcmpHdr *icmp = (IcmpHdr *)pkt;
	icmp->type = 8;
	icmp->code = 0;
	icmp->id = htons16(0xC0C0);
	icmp->seq = htons16(seq);
	for (int i = 0; i < 32; i++)
		pkt[sizeof(IcmpHdr) + i] = 'a' + (i % 23);
	icmp->csum = 0;
	icmp->csum = checksum16(pkt, sizeof(pkt), 0);

	icmp_state.id = icmp->id;
	icmp_state.seq = icmp->seq;
	icmp_state.got = false;
	icmp_state.waiting = true;

	uint64_t t0 = timer_get_ticks();
	ip_send(ip, IP_PROTO_ICMP, pkt, sizeof(pkt));

	uint64_t deadline = t0 + timeout_ms;
	while (timer_get_ticks() < deadline)
	{
		if (icmp_state.got)
		{
			icmp_state.waiting = false;
			return (int)(timer_get_ticks() - t0);
		}
		thread_sleep_ms(2);
	}
	icmp_state.waiting = false;
	return -1;
}

// ---------------- DNS resolver ----------------

bool net_resolve(const char *name, uint32_t *ip_out, uint32_t timeout_ms)
{
	if (str_to_ip(name, ip_out))
		return true;
	if (!configured || !our_dns)
		return false;

	uint8_t q[512];
	memset(q, 0, sizeof(q));
	uint16_t qid = (uint16_t)timer_get_ticks();
	q[0] = qid >> 8;
	q[1] = qid & 0xFF;
	q[2] = 0x01; // RD
	q[5] = 1;	 // QDCOUNT

	int pos = 12;
	const char *p = name;
	while (*p)
	{
		const char *dot = strchr(p, '.');
		int len = dot ? (int)(dot - p) : (int)strlen(p);
		if (len == 0 || len > 63 || pos + len + 1 > 500)
			return false;
		q[pos++] = len;
		memcpy(q + pos, p, len);
		pos += len;
		p += len;
		if (*p == '.')
			p++;
	}
	q[pos++] = 0;
	q[pos++] = 0;
	q[pos++] = 1; // QTYPE A
	q[pos++] = 0;
	q[pos++] = 1; // QCLASS IN

	static uint16_t eport = 49152;
	uint16_t sport = eport++;
	if (eport < 49152)
		eport = 49152;

	udp_sock.len = 0;
	udp_sock.port = sport;
	udp_send(our_dns, sport, 53, q, pos);

	uint64_t deadline = timer_get_ticks() + timeout_ms;
	while (timer_get_ticks() < deadline)
	{
		if (udp_sock.len > 0)
		{
			const uint8_t *r = (const uint8_t *)udp_sock.data;
			int rlen = udp_sock.len;
			if (rlen > 12 && r[0] == (qid >> 8) && r[1] == (qid & 0xFF))
			{
				int ancount = (r[6] << 8) | r[7];
				// skip question section
				int i = 12;
				while (i < rlen && r[i] != 0)
					i += r[i] + 1;
				i += 5;
				// walk answers
				for (int a = 0; a < ancount && i + 12 <= rlen; a++)
				{
					if ((r[i] & 0xC0) == 0xC0)
						i += 2; // compressed name
					else
					{
						while (i < rlen && r[i] != 0)
							i += r[i] + 1;
						i++;
					}
					if (i + 10 > rlen)
						break;
					uint16_t type = (r[i] << 8) | r[i + 1];
					uint16_t rdlen = (r[i + 8] << 8) | r[i + 9];
					i += 10;
					if (type == 1 && rdlen == 4 && i + 4 <= rlen)
					{
						memcpy(ip_out, r + i, 4);
						udp_sock.port = 0;
						udp_sock.len = 0;
						return true;
					}
					i += rdlen;
				}
			}
			udp_sock.len = 0;
		}
		thread_sleep_ms(5);
	}
	udp_sock.port = 0;
	return false;
}

// ---------------- misc ----------------

void ip_to_str(uint32_t ip, char *buf)
{
	const uint8_t *b = (const uint8_t *)&ip;
	snprintf(buf, 16, "%d.%d.%d.%d", b[0], b[1], b[2], b[3]);
}

bool str_to_ip(const char *s, uint32_t *ip_out)
{
	uint8_t parts[4];
	int idx = 0;
	const char *p = s;
	while (idx < 4)
	{
		if (*p < '0' || *p > '9')
			return false;
		int v = 0;
		while (*p >= '0' && *p <= '9')
		{
			v = v * 10 + (*p++ - '0');
			if (v > 255)
				return false;
		}
		parts[idx++] = v;
		if (idx < 4)
		{
			if (*p != '.')
				return false;
			p++;
		}
	}
	if (*p != '\0')
		return false;
	memcpy(ip_out, parts, 4);
	return true;
}

bool net_up(void) { return nic_up; }
bool net_configured(void) { return configured; }
uint32_t net_ip(void) { return our_ip; }
uint32_t net_mask(void) { return our_mask; }
uint32_t net_gateway(void) { return our_gw; }
uint32_t net_dns_server(void) { return our_dns; }
void net_mac(uint8_t out[6]) { memcpy(out, our_mac, 6); }

void net_init(void)
{
	if (!e1000_init())
		return;
	e1000_get_mac(our_mac);
	nic_up = true;
	thread_create("net-rx", net_rx_thread, NULL);
}
