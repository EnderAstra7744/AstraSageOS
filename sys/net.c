/*
 * Network stack: Ethernet, ARP, IPv4, ICMP echo, UDP, DHCP client, DNS (A records), TCP client, HTTP/1.0 GET.
 * Static address by default (the QEMU/Limbo user-mode network: 10.0.2.15, gateway 10.0.2.2, DNS 10.0.2.3).
 * Polled; runs only while a command (as net, as ping, as get) is executing, so nothing happens in the background.
* https:// works through the TLS 1.2 client (sys/tls.c). One TCP connection at a time, no listening sockets.
 *
 *   as net                         card, link, MAC, address, DNS
 *   as net ip <ip> [mask] [gw]     change the static address
 *   as net dhcp                    ask a DHCP server for address, gateway and DNS
 *   as net dns <name>              resolve a name (A record)
 *   as ping <ip> [count]           ICMP echo request, round trip time in ms
 *   as get <http-url> [file]       HTTP GET (see net_http_get)
 */
#include "astra.h"
#include "io.h"
#include "tls.h"

typedef unsigned long long u64;
#define C_OK  "\033[38;5;112m"
#define C_BAD "\033[91m"
#define C_DIM "\033[90m"
#define C_RST "\033[0m"

int  nic_init(void);
const char *nic_name(void);
const u8 *nic_mac(void);
int  nic_ready(void);
int  nic_link(void);
int  nic_send(const void *data, u32 len);
int  nic_recv(void *buf, u32 cap);
int  nic_is_rtl(void);
int  pci_wireless_scan(int kind, char *first, u32 cap);
void ui_launch_net(void);
int  ui_active(void);

static u32 my_ip = 0x0A00020F, my_mask = 0xFFFFFF00, my_gw = 0x0A000202;   /* host byte order: 10.0.2.15 /24 gw 10.0.2.2 */
static u32 dns_ip = 0x0A000203;       /* 10.0.2.3 */
static int src_zero;                  /* DHCP: send with source 0.0.0.0 */
static u32 tsc_per_ms;
static u64 t0_tsc;

static u64 rdtsc64(void)
{
#if defined(__i386__) || defined(__x86_64__)
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((u64)hi << 32) | lo;
#else
    extern u32 ui_ms(void);                 /* ARM: milliseconds from the generic timer (hw.c of the ARM ports) */
    return ui_ms();
#endif
}

/* TSC speed measured against PIT channel 2 (10 ms one-shot); the PIT and the TSC run from the same machine clock */
static void calibrate(void)
{
    if (tsc_per_ms) return;
#if !defined(__i386__) && !defined(__x86_64__)
    tsc_per_ms = 1; t0_tsc = rdtsc64(); return;                         /* rdtsc64() already counts milliseconds here */
#endif
    u8 v = inb(0x61);
    outb(0x61, (u8)(v & ~3));
    outb(0x43, 0xB0);
    outb(0x42, 11932 & 0xFF); outb(0x42, 11932 >> 8);                 /* 10.0 ms */
    u64 a = rdtsc64();
    outb(0x61, (u8)((v & ~2) | 1));
    for (u32 i = 0; i < 50000000 && !(inb(0x61) & 0x20); i++) { }
    u64 b = rdtsc64();
    outb(0x61, v);
    u32 d = (u32)((b - a) >> 4);
    tsc_per_ms = d ? d * 16 / 10 : 1;                                   /* cycles per ms */
    if (!tsc_per_ms) tsc_per_ms = 1;
    t0_tsc = rdtsc64();
}

static u32 now_ms(void)
{
    u64 d = rdtsc64() - t0_tsc;
#if !defined(__i386__) && !defined(__x86_64__)
    return (u32)d;
#endif
    return (u32)(d >> 8) / (tsc_per_ms >> 8 ? tsc_per_ms >> 8 : 1);
}

static void put16(u8 *p, u16 v) { p[0] = (u8)(v >> 8); p[1] = (u8)v; }
static void put32(u8 *p, u32 v) { p[0] = (u8)(v >> 24); p[1] = (u8)(v >> 16); p[2] = (u8)(v >> 8); p[3] = (u8)v; }
static u16 get16(const u8 *p) { return (u16)(p[0] << 8 | p[1]); }
static u32 get32(const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

static u16 csum(const u8 *d, u32 n)
{
    u32 s = 0;
    for (u32 i = 0; i + 1 < n; i += 2) s += get16(d + i);
    if (n & 1) s += (u32)d[n - 1] << 8;
    while (s >> 16) s = (s & 0xFFFF) + (s >> 16);
    return (u16)~s;
}

typedef struct { u32 ip; u8 mac[6]; int ok; } arp_ent;
static arp_ent arp[8];
static int arp_next;
static int echo_seq = -1;            /* sequence of the reply we wait for; set to -2 when it arrived */
static u32 echo_from, echo_ttl;

static void arp_learn(u32 ip, const u8 *mac)
{
    for (int i = 0; i < 8; i++) if (arp[i].ok && arp[i].ip == ip) { for (int k = 0; k < 6; k++) arp[i].mac[k] = mac[k]; return; }
    arp_ent *e = &arp[arp_next++ & 7];
    e->ip = ip; e->ok = 1;
    for (int k = 0; k < 6; k++) e->mac[k] = mac[k];
}
static const u8 *arp_find(u32 ip)
{
    for (int i = 0; i < 8; i++) if (arp[i].ok && arp[i].ip == ip) return arp[i].mac;
    return 0;
}

static void send_arp(u16 op, const u8 *dst_mac, u32 target_ip)
{
    u8 f[42];
    const u8 *m = nic_mac();
    static const u8 bc[6] = { 255, 255, 255, 255, 255, 255 };
    const u8 *edst = op == 1 ? bc : dst_mac;
    for (int i = 0; i < 6; i++) { f[i] = edst[i]; f[6 + i] = m[i]; }
    put16(f + 12, 0x0806);
    put16(f + 14, 1); put16(f + 16, 0x0800); f[18] = 6; f[19] = 4; put16(f + 20, op);
    for (int i = 0; i < 6; i++) f[22 + i] = m[i];
    put32(f + 28, my_ip);
    for (int i = 0; i < 6; i++) f[32 + i] = op == 1 ? 0 : dst_mac[i];
    put32(f + 38, target_ip);
    nic_send(f, 42);
}

static void send_ip(const u8 *dst_mac, u32 dst, u8 proto, const u8 *payload, u32 plen)
{
    static u8 f[14 + 20 + 1500];
    static u16 ident;
    const u8 *m = nic_mac();
    for (int i = 0; i < 6; i++) { f[i] = dst_mac[i]; f[6 + i] = m[i]; }
    put16(f + 12, 0x0800);
    u8 *ip = f + 14;
    ip[0] = 0x45; ip[1] = 0; put16(ip + 2, (u16)(20 + plen)); put16(ip + 4, ++ident); put16(ip + 6, 0);
    ip[8] = 64; ip[9] = proto; put16(ip + 10, 0); put32(ip + 12, src_zero ? 0 : my_ip); put32(ip + 16, dst);
    put16(ip + 10, csum(ip, 20));
    for (u32 i = 0; i < plen; i++) ip[20 + i] = payload[i];
    nic_send(f, 14 + 20 + plen);
}

/* ---- UDP receive slot (one datagram at a time: DHCP and DNS use it one after the other) ---- */
static u16 udp_port;                 /* destination port we wait for, 0 = none */
static u8  udp_buf[600];
static u32 udp_len, udp_src;
static int udp_have;

/* ---- TCP client state (one connection) ---- */
enum { T_CLOSED, T_SYN, T_EST, T_SYNRCVD };
static u16 t_listen;                 /* server: port we accept a connection on, 0 = not listening */
static u8  t_peer_mac[6];            /* source MAC of the last TCP segment (learned for the peer of an accepted connection) */
static int  t_state;
static u16  t_lport, t_rport;
static u32  t_rip, t_snd_nxt, t_snd_una, t_rcv_nxt;
static u8  *t_rx;                    /* receive buffer given by the caller */
static u32  t_rx_cap, t_rx_len;
static int  t_fin, t_rst, t_trunc;

static void tcp_seg(u16 flags, const u8 *data, u32 len, u32 seq, int mss_opt);

static void tcp_in(const u8 *ip, u32 ihl, u32 tot)
{
    const u8 *t = ip + ihl;
    u32 tl = tot - ihl;
    if (tl >= 20 && t_state == T_CLOSED && t_listen && get16(t + 2) == t_listen && (t[13] & 0x12) == 0x02) {   /* SYN: accept */
        static u8 pb0[12 + 60];
        if (tl > 60) return;
        put32(pb0, get32(ip + 12)); put32(pb0 + 4, get32(ip + 16)); pb0[8] = 0; pb0[9] = 6; put16(pb0 + 10, (u16)tl);
        for (u32 i = 0; i < tl; i++) pb0[12 + i] = t[i];
        if (csum(pb0, 12 + tl) != 0) return;
        t_rip = get32(ip + 12); t_rport = get16(t); t_lport = t_listen;
        arp_learn(t_rip, t_peer_mac);
        u32 iss = hal_ticks() * 2654435761u;
        t_rcv_nxt = get32(t + 4) + 1; t_snd_una = iss; t_snd_nxt = iss + 1;
        t_rx_len = 0; t_fin = t_rst = t_trunc = 0;
        t_state = T_SYNRCVD;
        tcp_seg(0x12, 0, 0, iss, 1);                                    /* SYN | ACK */
        return;
    }
    if (tl < 20 || t_state == T_CLOSED) return;
    if (get32(ip + 12) != t_rip || get16(t) != t_rport || get16(t + 2) != t_lport) return;
    /* checksum over pseudo header + segment */
    {
        static u8 pb[12 + 1500];
        if (tl > 1500) return;
        put32(pb, get32(ip + 12)); put32(pb + 4, get32(ip + 16)); pb[8] = 0; pb[9] = 6; put16(pb + 10, (u16)tl);
        for (u32 i = 0; i < tl; i++) pb[12 + i] = t[i];
        if (csum(pb, 12 + tl) != 0) return;
    }
    u32 seq = get32(t + 4), ack = get32(t + 8);
    u32 doff = (u32)(t[12] >> 4) * 4;
    u8 fl = t[13];
    if (doff < 20 || doff > tl) return;
    u32 dlen = tl - doff;
    if (fl & 4) { t_rst = 1; t_state = T_CLOSED; return; }           /* RST */
    if (t_state == T_SYNRCVD) {
        if ((fl & 0x12) == 0x02) { tcp_seg(0x12, 0, 0, t_snd_una, 1); return; }          /* the SYN again: repeat our answer */
        if ((fl & 0x10) && ack == t_snd_nxt) t_state = T_EST;                          /* handshake done; data may come with it */
        else return;
    }
    if (t_state == T_SYN) {
        if ((fl & 0x12) == 0x12 && ack == t_snd_nxt) {                /* SYN+ACK */
            t_rcv_nxt = seq + 1; t_snd_una = ack; t_state = T_EST;
            tcp_seg(0x10, 0, 0, t_snd_nxt, 0);
        }
        return;
    }
    if (fl & 0x10) { if ((int)(ack - t_snd_una) > 0 && (int)(ack - t_snd_nxt) <= 0) t_snd_una = ack; }
    if (dlen > 0) {
        if (seq == t_rcv_nxt) {
            u32 take = dlen;
            if (t_rx_len + take > t_rx_cap) { take = t_rx_cap - t_rx_len; t_trunc = 1; }
            for (u32 i = 0; i < take; i++) t_rx[t_rx_len + i] = t[doff + i];
            t_rx_len += take;
            t_rcv_nxt += dlen;
        }
        tcp_seg(0x10, 0, 0, t_snd_nxt, 0);                            /* ACK (a duplicate ACK when out of order) */
    }
    if ((fl & 1) && seq + dlen == t_rcv_nxt) {                        /* FIN, in order */
        t_rcv_nxt++; t_fin = 1;
        tcp_seg(0x10, 0, 0, t_snd_nxt, 0);
    }
}

static void handle(const u8 *f, u32 n)
{
    if (n < 14) return;
    u16 type = get16(f + 12);
    if (type == 0x0806 && n >= 42) {                                  /* ARP */
        u16 op = get16(f + 20);
        u32 sip = get32(f + 28), tip = get32(f + 38);
        if (op == 2 && tip == my_ip) arp_learn(sip, f + 22);
        if (op == 1 && tip == my_ip) { arp_learn(sip, f + 22); send_arp(2, f + 22, sip); }
    } else if (type == 0x0800 && n >= 34) {                           /* IPv4 */
        const u8 *ip = f + 14;
        u32 ihl = (u32)(ip[0] & 15) * 4, tot = get16(ip + 2);
        if ((ip[0] >> 4) != 4 || ihl < 20 || tot < ihl || 14 + tot > n) return;
        u32 dst = get32(ip + 16);
        if (dst != my_ip && dst != 0xFFFFFFFFu && my_ip != 0) return;
        if (csum(ip, ihl) != 0) return;
        if (get16(ip + 6) & 0x3FFF) return;                           /* fragments are not reassembled */
        if (ip[9] == 1 && tot >= ihl + 8) {                           /* ICMP */
            const u8 *ic = ip + ihl; u32 il = tot - ihl;
            if (csum(ic, il) != 0) return;
            if (ic[0] == 8 && dst == my_ip) {                         /* echo request: answer (we are pingable) */
                static u8 r[1500];
                if (il > sizeof r) return;
                for (u32 i = 0; i < il; i++) r[i] = ic[i];
                r[0] = 0; put16(r + 2, 0); put16(r + 2, csum(r, il));
                arp_learn(get32(ip + 12), f + 6);
                send_ip(f + 6, get32(ip + 12), 1, r, il);
            } else if (ic[0] == 0 && (int)get16(ic + 6) == echo_seq) { /* our echo reply */
                echo_from = get32(ip + 12); echo_ttl = ip[8]; echo_seq = -2;
            }
        } else if (ip[9] == 17 && tot >= ihl + 8) {                   /* UDP */
            const u8 *u = ip + ihl; u32 ul = get16(u + 4);
            if (ul < 8 || ul > tot - ihl) return;
            if (udp_port && get16(u + 2) == udp_port && !udp_have) {
                u32 pl = ul - 8; if (pl > sizeof udp_buf) pl = sizeof udp_buf;
                for (u32 i = 0; i < pl; i++) udp_buf[i] = u[8 + i];
                udp_len = pl; udp_src = get32(ip + 12); udp_have = 1;
            }
        } else if (ip[9] == 6 && dst == my_ip) {                      /* TCP */
            for (int i = 0; i < 6; i++) t_peer_mac[i] = f[6 + i];
            tcp_in(ip, ihl, tot);
        }
    }
}

extern int aspi_wake;                    /* while set, the graphical shell is resumed every frame (see gui/app_ast.c) */
static int net_yielding;                 /* a download is running: let the desktop draw while we wait for packets */

static void poll(void)
{
    static u8 f[1600];
    int n, guard = 0;
    while (guard++ < 64 && (n = nic_recv(f, sizeof f)) > 0) handle(f, (u32)n);
    if (net_yielding) {                  /* without this the whole desktop froze during DNS, TCP and the TLS handshake */
        static u32 last;
        u32 t = now_ms();
        if (t - last >= 12) { last = t; kbd_wait_hint(); }
    }
}

static int resolve(u32 ip, u8 *mac_out, u32 timeout_ms)
{
    const u8 *m = arp_find(ip);
    if (!m) {
        u32 start = now_ms(), last = 0;
        for (;;) {
            u32 t = now_ms() - start;
            if (t == 0 || t - last >= 500 || (last == 0 && t < 2)) { if (t >= last + 500 || last == 0) { send_arp(1, 0, ip); last = t ? t : 1; } }
            poll();
            m = arp_find(ip);
            if (m) break;
            if (t > timeout_ms) return 0;
        }
    }
    for (int i = 0; i < 6; i++) mac_out[i] = m[i];
    return 1;
}

static int parse_ip(const char *s, u32 *out)
{
    u32 v = 0; int parts = 0;
    for (;;) {
        if (*s < '0' || *s > '9') return 0;
        u32 o = 0; int digits = 0;
        while (*s >= '0' && *s <= '9') { o = o * 10 + (u32)(*s - '0'); s++; if (++digits > 3) return 0; }
        if (o > 255) return 0;
        v = v << 8 | o; parts++;
        if (parts == 4) break;
        if (*s != '.') return 0;
        s++;
    }
    if (*s && *s != ' ') return 0;
    *out = v;
    return 1;
}

static void fmt_ip(char *o, size_t cap, u32 v) { ksnprintf(o, cap, "%u.%u.%u.%u", v >> 24, (v >> 16) & 255, (v >> 8) & 255, v & 255); }

/* next hop (the gateway when the address is off our subnet) and its MAC address */
static int route_mac(u32 ip, u8 *mac)
{
    u32 next = ((ip & my_mask) == (my_ip & my_mask)) ? ip : my_gw;
    return resolve(next, mac, 2000);
}

static void send_udp(const u8 *mac, u32 dst, u16 sport, u16 dport, const u8 *data, u32 len)
{
    static u8 d[8 + 600];
    if (len > 600) return;
    put16(d, sport); put16(d + 2, dport); put16(d + 4, (u16)(8 + len)); put16(d + 6, 0);   /* checksum 0 = none (legal in IPv4) */
    for (u32 i = 0; i < len; i++) d[8 + i] = data[i];
    send_ip(mac, dst, 17, d, 8 + len);
}

/* waits up to `ms` for a datagram on `port`; 1 = udp_buf/udp_len hold it */
static int udp_wait(u16 port, u32 ms)
{
    udp_port = port; udp_have = 0;
    u32 st = now_ms();
    while (now_ms() - st < ms) { poll(); if (udp_have) return 1; }
    return 0;
}

/* ---------------------------------------------------------------- DHCP */

static int dhcp_options(const u8 *o, u32 n, u8 want, u8 *out, u32 cap)    /* option value copied to out; returns length or -1 */
{
    for (u32 i = 0; i < n;) {
        u8 c = o[i];
        if (c == 255) break;
        if (c == 0) { i++; continue; }
        if (i + 1 >= n) break;
        u8 l = o[i + 1];
        if (i + 2 + l > n) break;
        if (c == want) { for (u32 k = 0; k < l && k < cap; k++) out[k] = o[i + 2 + k]; return l; }
        i += 2u + l;
    }
    return -1;
}

static void dhcp_build(u8 *b, u32 *len, u32 xid, u8 type, u32 req_ip, u32 server)
{
    for (u32 i = 0; i < 300; i++) b[i] = 0;
    b[0] = 1; b[1] = 1; b[2] = 6; put32(b + 4, xid); put16(b + 10, 0x8000);   /* broadcast flag */
    const u8 *m = nic_mac();
    for (int i = 0; i < 6; i++) b[28 + i] = m[i];
    b[236] = 99; b[237] = 130; b[238] = 83; b[239] = 99;
    u32 o = 240;
    b[o++] = 53; b[o++] = 1; b[o++] = type;
    if (type == 3) { b[o++] = 50; b[o++] = 4; put32(b + o, req_ip); o += 4; b[o++] = 54; b[o++] = 4; put32(b + o, server); o += 4; }
    b[o++] = 55; b[o++] = 4; b[o++] = 1; b[o++] = 3; b[o++] = 6; b[o++] = 15;
    b[o++] = 255;
    *len = o;
}

static int dhcp_run(void)
{
    static const u8 bc[6] = { 255, 255, 255, 255, 255, 255 };
    static u8 b[320];
    u32 len, xid = hal_ticks() | 1;
    u32 old_ip = my_ip;
    int ok = 0;
    my_ip = 0;
    for (int attempt = 0; attempt < 3 && !ok; attempt++) {
        dhcp_build(b, &len, xid, 1, 0, 0);
        src_zero = 1; send_udp(bc, 0xFFFFFFFFu, 68, 67, b, len); src_zero = 0;
        for (;;) {
            if (!udp_wait(68, 2000)) break;
            if (udp_len < 241 || udp_buf[0] != 2 || get32(udp_buf + 4) != xid) { udp_have = 0; continue; }
            u8 t[4], sid[4];
            if (dhcp_options(udp_buf + 240, udp_len - 240, 53, t, 4) < 1 || t[0] != 2) { udp_have = 0; continue; }   /* OFFER */
            u32 offer = get32(udp_buf + 16), server = 0;
            if (dhcp_options(udp_buf + 240, udp_len - 240, 54, sid, 4) == 4) server = get32(sid);
            udp_have = 0;
            dhcp_build(b, &len, xid, 3, offer, server);
            src_zero = 1; send_udp(bc, 0xFFFFFFFFu, 68, 67, b, len); src_zero = 0;
            for (;;) {
                if (!udp_wait(68, 2000)) break;
                if (udp_len < 241 || udp_buf[0] != 2 || get32(udp_buf + 4) != xid) { udp_have = 0; continue; }
                u8 v[4];
                int tl = dhcp_options(udp_buf + 240, udp_len - 240, 53, t, 4);
                if (tl < 1) { udp_have = 0; continue; }
                if (t[0] == 6) { udp_have = 0; break; }              /* NAK */
                if (t[0] != 5) { udp_have = 0; continue; }           /* ACK */
                my_ip = get32(udp_buf + 16);
                if (dhcp_options(udp_buf + 240, udp_len - 240, 1, v, 4) == 4) my_mask = get32(v);
                if (dhcp_options(udp_buf + 240, udp_len - 240, 3, v, 4) >= 4) my_gw = get32(v);
                if (dhcp_options(udp_buf + 240, udp_len - 240, 6, v, 4) >= 4) dns_ip = get32(v);
                udp_have = 0; ok = 1;
                break;
            }
            break;
        }
    }
    udp_port = 0;
    if (!ok) my_ip = old_ip;
    for (int i = 0; i < 8; i++) arp[i].ok = 0;
    return ok;
}

/* ---------------------------------------------------------------- DNS (A records) */

static int dns_resolve(const char *name, u32 *out)
{
    static u8 q[300];
    u16 id = (u16)(hal_ticks() | 1), sport = (u16)(40000 + (hal_ticks() & 0x3FF));
    u32 o = 12;
    for (u32 i = 0; i < 12; i++) q[i] = 0;
    put16(q, id); put16(q + 2, 0x0100); put16(q + 4, 1);
    for (const char *p = name; *p;) {
        const char *e = p; while (*e && *e != '.') e++;
        u32 l = (u32)(e - p);
        if (l == 0 || l > 63 || o + l + 6 > sizeof q) return 0;
        q[o++] = (u8)l;
        for (u32 i = 0; i < l; i++) q[o++] = (u8)p[i];
        p = *e ? e + 1 : e;
    }
    q[o++] = 0; put16(q + o, 1); put16(q + o + 2, 1); o += 4;
    u8 mac[6];
    if (!route_mac(dns_ip, mac)) return 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        udp_port = sport; udp_have = 0;
        send_udp(mac, dns_ip, sport, 53, q, o);
        if (!udp_wait(sport, 2000)) continue;
        if (udp_len < 12 || get16(udp_buf) != id || !(udp_buf[2] & 0x80)) { udp_have = 0; continue; }
        if ((udp_buf[3] & 15) != 0) { udp_port = 0; return 0; }          /* error (e.g. name not found) */
        u32 an = get16(udp_buf + 6), pos = 12;
        for (u32 k = 0; k < get16(udp_buf + 4) && pos < udp_len; k++) {   /* skip the question */
            while (pos < udp_len && udp_buf[pos]) pos += (udp_buf[pos] & 0xC0) == 0xC0 ? 2 : 1u + udp_buf[pos];
            pos += 5;
        }
        for (u32 k = 0; k < an && pos + 12 <= udp_len; k++) {
            while (pos < udp_len && udp_buf[pos] && (udp_buf[pos] & 0xC0) != 0xC0) pos += 1u + udp_buf[pos];
            pos += (pos < udp_len && (udp_buf[pos] & 0xC0) == 0xC0) ? 2 : 1;
            if (pos + 10 > udp_len) break;
            u16 type = get16(udp_buf + pos), rl = get16(udp_buf + pos + 8);
            if (type == 1 && rl == 4 && pos + 10 + 4 <= udp_len) { *out = get32(udp_buf + pos + 10); udp_port = 0; return 1; }
            pos += 10u + rl;
        }
        udp_port = 0;
        return 0;
    }
    udp_port = 0;
    return 0;
}

/* ---------------------------------------------------------------- TCP client */

static void tcp_seg(u16 flags, const u8 *data, u32 len, u32 seq, int mss_opt)
{
    static u8 pb[12 + 24 + 1460];
    u8 *t = pb + 12;
    u32 hl = mss_opt ? 24 : 20;
    if (len > 1460) return;
    put16(t, t_lport); put16(t + 2, t_rport); put32(t + 4, seq); put32(t + 8, (flags & 0x10) ? t_rcv_nxt : 0);
    t[12] = (u8)(hl / 4 << 4); t[13] = (u8)flags;
    u32 room = t_rx_cap > t_rx_len ? t_rx_cap - t_rx_len : 0;
    put16(t + 14, (u16)(room > 8192 ? 8192 : (room < 536 ? 536 : room)));
    put16(t + 16, 0); put16(t + 18, 0);
    if (mss_opt) { t[20] = 2; t[21] = 4; put16(t + 22, 1460); }
    for (u32 i = 0; i < len; i++) t[hl + i] = data[i];
    put32(pb, my_ip); put32(pb + 4, t_rip); pb[8] = 0; pb[9] = 6; put16(pb + 10, (u16)(hl + len));
    put16(t + 16, csum(pb, 12 + hl + len));
    u8 mac[6];
    if (!route_mac(t_rip, mac)) return;
    send_ip(mac, t_rip, 6, t, hl + len);
}

static int tcp_connect(u32 ip, u16 port)
{
    t_lport = (u16)(49152 + (hal_ticks() & 0x3FFF)); t_rport = port; t_rip = ip;
    u32 iss = hal_ticks();
    t_snd_nxt = iss + 1; t_snd_una = iss; t_rcv_nxt = 0;                /* the SYN uses sequence number iss */
    t_rx_len = 0; t_fin = t_rst = t_trunc = 0;
    t_state = T_SYN;
    for (int attempt = 0; attempt < 4; attempt++) {
        tcp_seg(0x02, 0, 0, iss, 1);                                    /* SYN */
        u32 st = now_ms();
        while (now_ms() - st < 1000) {
            poll();
            if (t_state == T_EST) { return 1; }
            if (t_rst) return 0;
        }
    }
    t_state = T_CLOSED;
    return 0;
}
static int tcp_send_wait(const u8 *data, u32 len)                       /* one segment, retransmitted until acknowledged */
{
    u32 seq = t_snd_nxt;
    t_snd_nxt = seq + len;                                              /* so that the ACK of this segment is accepted */
    for (int attempt = 0; attempt < 4; attempt++) {
        tcp_seg(0x18, data, len, seq, 0);                               /* PSH | ACK */
        u32 st = now_ms();
        while (now_ms() - st < 1000) {
            poll();
            if ((int)(t_snd_una - (seq + len)) >= 0) return 1;
            if (t_rst || t_state == T_CLOSED) return 0;
        }
    }
    return 0;
}

static void tcp_close(void)
{
    if (t_state == T_EST) {
        tcp_seg(0x11, 0, 0, t_snd_nxt, 0);                              /* FIN | ACK */
        t_snd_nxt++;
        u32 st = now_ms();
        while (now_ms() - st < 300) poll();
    }
    t_state = T_CLOSED;
}


/* ---------------------------------------------------------------- TLS glue: random numbers, transport, trust anchors */

extern const u8 asset_roots_start[] __attribute__((weak));
extern const u8 asset_roots_end[] __attribute__((weak));
int tls_verify_on = 1;                                   /* as tls insecure  ->  0 */

/*
 * Random numbers: a SHA-256 hash chain over a pool that is stirred with the CPU timestamp counter, the PIT/tick counter,
 * the clock and the arrival times of network packets (and RDRAND when the CPU has it). This is a small, honest entropy
 * source for an OS that has no hardware RNG driver: the secrets are ephemeral per connection, but an attacker who knew
 * the exact boot timing of the machine could narrow them down.
 */
static u8 rng_pool[32];
static u32 rng_ctr;
static void rng_mix(u32 a, u32 b)
{
    u8 in[48];
    kmemcpy(in, rng_pool, 32);
    put32(in + 32, a); put32(in + 36, b); put32(in + 40, rng_ctr++); put32(in + 44, hal_ticks());
    sha256(in, sizeof in, rng_pool);
}
static int has_rdrand(void)
{
#if !defined(__i386__) && !defined(__x86_64__)
    return 0;
#else
    u32 a = 1, b, c = 0, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
    return (c >> 30) & 1;
#endif
}
void tls_random(cu8 *out, size_t n)
{
    u64 t = rdtsc64();
    hal_time tm; hal_datetime(&tm);
    rng_mix((u32)t, (u32)(t >> 32));
    rng_mix(ktime_pack(&tm), my_ip);
#if defined(__i386__) || defined(__x86_64__)
    if (has_rdrand()) {
        for (int i = 0; i < 4; i++) { u32 v = 0, ok = 0; for (int k = 0; k < 10 && !ok; k++) __asm__ volatile("rdrand %0; setc %%cl; movzbl %%cl, %1" : "=r"(v), "=r"(ok) :: "ecx", "cc"); if (ok) rng_mix(v, (u32)i); }
    }
#endif
    size_t off = 0;
    while (off < n) {
        u8 in[40], h[32];
        kmemcpy(in, rng_pool, 32); put32(in + 32, rng_ctr++); put32(in + 36, (u32)rdtsc64());
        sha256(in, sizeof in, h);
        size_t take = n - off < 32 ? n - off : 32;
        kmemcpy(out + off, h, take);
        off += take;
        rng_mix((u32)rdtsc64(), (u32)off);
    }
}

static tls_t tls_conn;
static u8 tls_in[98304];                 /* encrypted bytes from the server wait here until the TLS layer takes them */
static u32 tls_rd;

static int tio_send(void *c, const cu8 *d, size_t n)
{
    (void)c;
    while (n) {
        u32 take = n > 1400 ? 1400 : (u32)n;
        if (!tcp_send_wait(d, take)) return -1;
        d += take; n -= take;
    }
    return 0;
}

static int tio_recv(void *c, cu8 *buf, size_t max, cu32 timeout_ms)
{
    (void)c;
    u32 st = now_ms();
    for (;;) {
        if (t_rx_len > tls_rd) {
            u32 take = t_rx_len - tls_rd; if (take > max) take = (u32)max;
            kmemcpy(buf, tls_in + tls_rd, take);
            tls_rd += take;
            if (tls_rd == t_rx_len) { tls_rd = 0; t_rx_len = 0; }
            return (int)take;
        }
        if (t_fin || t_rst || t_state != T_EST) return -1;
        if (now_ms() - st >= timeout_ms) return 0;
        poll();
        rng_mix((u32)rdtsc64(), t_rx_len);
    }
}

static u32 clock_epoch(void)
{
    hal_time t; hal_datetime(&t);
    return ktime_pack(&t) + 946684800u;
}

void tls_command(const char *a)
{
    while (*a == ' ') a++;
    if (!kstrcmp(a, "insecure")) { tls_verify_on = 0; kprintf(C_BAD "TLS: certificates are NOT checked any more (encrypted, but the server is not authenticated)." C_RST "\n"); }
    else if (!kstrcmp(a, "strict")) { tls_verify_on = 1; kprintf(C_OK "TLS: certificates are checked (chain, host name, dates)." C_RST "\n"); }
    else {
        size_t rl = asset_roots_start ? (size_t)(asset_roots_end - asset_roots_start) : 0, n = 0;
        for (size_t off = 0; off + 4 <= rl;) { size_t sl = (size_t)asset_roots_start[off] << 8 | asset_roots_start[off + 1], pl = (size_t)asset_roots_start[off + 2] << 8 | asset_roots_start[off + 3]; off += 4 + sl + pl; n++; }
        kprintf("TLS 1.2 client: ECDHE (X25519, P-256), AES-128/256-GCM, RSA/ECDSA certificates.\n");
        kprintf("Trusted root certificates: %u (Mozilla list built into the OS image).\n", (u32)n);
        kprintf("Certificate checking: %s\n", tls_verify_on ? "strict" : "OFF (insecure)");
        kprintf("Usage: tls [strict|insecure]\n");
    }
}

/* ---------------------------------------------------------------- HTTP/1.0 GET */

static u8  http_buf[32768];
static const char *http_err = "";

/*
 * http://host[:port]/path  (host = a.b.c.d or a name). Returns 1 on success: *body and *blen point into an internal 32 KiB
 * buffer (valid until the next call), *status is the HTTP status code, *truncated is set when the answer was longer than
 * the buffer. On failure returns 0 and net_http_error() says why. Redirects are not followed; https:// is refused.
 */
const char *net_http_error(void) { return http_err; }

static const char *req_method = "GET", *req_xhdr = 0;     /* set by net_http_request for the call in progress */
static const u8 *req_body = 0;
static u32 req_body_len = 0;

static int http_get_into2(const char *url, u8 *buf, u32 cap, const char **body, u32 *blen, int *status, int *truncated, int verbose)
{
    char host[64], path[200];
    u32 port = 80, ip;
    int https = 0;
    http_err = "";
    if (!kstrncmp(url, "https://", 8)) https = 1;
    else if (kstrncmp(url, "http://", 7)) { http_err = "the address must start with http:// or https://"; return 0; }
    if (https) port = 443;
    const char *p = url + (https ? 8 : 7);
    u32 hn = 0;
    while (*p && *p != '/' && *p != ':' && hn < sizeof host - 1) host[hn++] = *p++;
    host[hn] = 0;
    if (*p == ':') {
        p++; port = 0;
        while (*p >= '0' && *p <= '9' && port < 70000) port = port * 10 + (u32)(*p++ - '0');
        if (port == 0 || port > 65535) { http_err = "bad port"; return 0; }
    }
    if (*p && *p != '/') { http_err = "bad address"; return 0; }
    u32 pn = 0;
    if (!*p) { path[pn++] = '/'; }
    while (*p && pn < sizeof path - 1) path[pn++] = *p++;
    path[pn] = 0;
    if (!host[0]) { http_err = "no host in the address"; return 0; }
    if (!nic_ready() && !nic_init()) { http_err = nic_name(); return 0; }
    calibrate();
    if (!parse_ip(host, &ip)) {
        if (verbose) kprintf("Resolving %s via DNS %u.%u.%u.%u ...\n", host, dns_ip >> 24, (dns_ip >> 16) & 255, (dns_ip >> 8) & 255, dns_ip & 255);
        if (!dns_resolve(host, &ip)) { http_err = "could not resolve the name (DNS gave no answer)"; return 0; }
    }
    if (verbose) { char s[20]; fmt_ip(s, sizeof s, ip); kprintf("Connecting to %s:%u ...\n", s, port); }
    if (https) {
        t_rx = tls_in; t_rx_cap = sizeof tls_in; tls_rd = 0;
    } else { t_rx = buf; t_rx_cap = cap; }
    if (!tcp_connect(ip, (u16)port)) { http_err = t_rst ? "connection refused" : "no answer from the server (timeout)"; return 0; }
    static char req[66560];                                              /* request line + headers + a form body of up to 64 KiB */
    int rl;
    {
        const char *xh = req_xhdr;
        rl = ksnprintf(req, 2048, "%s %s HTTP/1.0\r\nHost: %s", req_method, path, host);
        if (rl > 0 && rl < 2000 && port != (https ? 443u : 80u)) rl += ksnprintf(req + rl, 2048 - (u32)rl, ":%u", port);
        if (rl > 0 && rl < 2040) rl += ksnprintf(req + rl, 2048 - (u32)rl, "\r\n");
        if (xh && *xh) {
            u32 xl = kstrlen(xh);
            if (rl + (int)xl + 64 >= 4096) { tcp_close(); http_err = "request headers too long"; return 0; }
            kmemcpy(req + rl, xh, xl); rl += (int)xl;
        } else rl += ksnprintf(req + rl, 1024, "User-Agent: AstraSageOS\r\nAccept: */*\r\n");
        if (req_body) rl += ksnprintf(req + rl, 64, "Content-Length: %u\r\n", req_body_len);
        rl += ksnprintf(req + rl, 64, "Connection: close\r\n\r\n");
        if (req_body) {
            if (req_body_len > sizeof req - (u32)rl - 1) { tcp_close(); http_err = "form data too large"; return 0; }
            kmemcpy(req + rl, req_body, req_body_len); rl += (int)req_body_len;
        }
    }
    if (rl <= 0 || rl >= (int)sizeof req) { tcp_close(); http_err = "request too long"; return 0; }
    int reset;
    u32 total = 0;
    if (https) {
        const u8 *roots = asset_roots_start; size_t rln = asset_roots_start ? (size_t)(asset_roots_end - asset_roots_start) : 0;
        tls_io io = { tio_send, tio_recv, 0 };
        if (verbose) kprintf("TLS handshake with %s ...\n", host);
        if (tls_connect(&tls_conn, &io, host, tls_verify_on, clock_epoch(), roots, rln)) { http_err = tls_error(); tcp_close(); return 0; }
        if (verbose) kprintf("%s, server certificate %s\n", tls_suite_name(&tls_conn), tls_conn.verified ? "verified" : C_BAD "NOT verified" C_RST);
        if (tls_write(&tls_conn, (const cu8 *)req, (size_t)rl)) { http_err = tls_error(); tcp_close(); return 0; }
        int r;
        for (;;) {
            if (total >= cap) { t_trunc = 1; break; }
            r = tls_read(&tls_conn, buf + total, cap - total, 5000);
            if (r > 0) { total += (u32)r; continue; }
            break;
        }
        reset = r == -2;
        if (r == -2 && total == 0) http_err = tls_error();
        tls_close(&tls_conn);
        tcp_close();
        t_rx_len = total;
        if (total == 0) { if (!http_err[0]) http_err = "the server sent nothing"; return 0; }
    } else {
        for (u32 so = 0; so < (u32)rl; ) {
            u32 take = (u32)rl - so > 1400 ? 1400 : (u32)rl - so;
            if (!tcp_send_wait((const u8 *)req + so, take)) { tcp_close(); http_err = "the server did not accept the request"; return 0; }
            so += take;
        }
        u32 last_progress = now_ms(), last_len = 0;
        while (!t_fin && !t_rst && t_state == T_EST) {
            poll();
            if (t_rx_len != last_len) { last_len = t_rx_len; last_progress = now_ms(); }
            if (now_ms() - last_progress > 5000) break;
        }
        reset = t_rst;
        tcp_close();
        if (t_rx_len == 0) { http_err = reset ? "connection reset by the server" : "the server sent nothing"; return 0; }
    }
    buf[t_rx_len < cap ? t_rx_len : cap - 1] = 0;
    if (kstrncmp((const char *)buf, "HTTP/", 5)) { http_err = "the answer is not HTTP"; return 0; }
    int st = 0;
    const char *sp = kstrchr((const char *)buf, ' ');
    if (sp) { sp++; while (*sp >= '0' && *sp <= '9') st = st * 10 + (*sp++ - '0'); }
    u32 off = 0;
    for (u32 i = 0; i + 3 < t_rx_len; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') { off = i + 4; break; }
    if (!off) { http_err = "the answer has no complete header"; return 0; }
    *status = st; *body = (const char *)buf + off; *blen = t_rx_len - off; *truncated = t_trunc;
    return 1;
}


static int http_get_into(const char *url, u8 *buf, u32 cap, const char **body, u32 *blen, int *status, int *truncated, int verbose)
{
    net_yielding = 1; aspi_wake = 1;
    int r = http_get_into2(url, buf, cap, body, blen, status, truncated, verbose);
    net_yielding = 0; aspi_wake = 0;
    return r;
}

int net_http_get(const char *url, const char **body, u32 *blen, int *status, int *truncated, int verbose)
{
    return http_get_into(url, http_buf, sizeof http_buf, body, blen, status, truncated, verbose);
}

/* Downloads the body of url into buf (cap bytes); the body is moved to buf[0]. 1 = ok (*len bytes, *status), 0 = failed. */
int net_http_download(const char *url, u8 *buf, u32 cap, u32 *len, int *status)
{
    const char *body; u32 bl; int tr;
    if (!http_get_into(url, buf, cap, &body, &bl, status, &tr, 0)) return 0;
    if (tr) { http_err = "the file is larger than the download buffer"; return 0; }
    kmemmove(buf, body, bl);
    *len = bl;
    return 1;
}

/* Raw fetch for the browser: buf receives header + body; *body_off / *body_len say where the body is. 1 = ok. */
int net_http_fetch(const char *url, u8 *buf, u32 cap, u32 *hdr_len, u32 *body_off, u32 *body_len, int *status)
{
    const char *body; u32 bl; int tr;
    if (!http_get_into(url, buf, cap, &body, &bl, status, &tr, 0)) return 0;
    *body_off = (u32)((const u8 *)body - buf);
    *hdr_len = *body_off;
    *body_len = bl;
    return 1;
}

/* General request for native programs: method, extra request headers ("Name: value\r\n" lines, or NULL for the defaults) and an
 * optional body (forms). Same answer layout as net_http_fetch. *trunc is set when the answer did not fit into cap. */
int net_http_request(const char *method, const char *url, const char *xhdr, const void *body, u32 blen,
                     u8 *buf, u32 cap, u32 *hdr_len, u32 *body_off, u32 *body_len, int *status)
{
    const char *b; u32 bl; int tr, ok;
    req_method = method && *method ? method : "GET";
    req_xhdr = xhdr;
    req_body = blen ? (const u8 *)body : 0;
    req_body_len = blen;
    ok = http_get_into(url, buf, cap, &b, &bl, status, &tr, 0);
    req_method = "GET"; req_xhdr = 0; req_body = 0; req_body_len = 0;
    if (!ok) return 0;
    if (tr) { http_err = "the page is larger than the receive buffer"; return 0; }
    *body_off = (u32)((const u8 *)b - buf);
    *hdr_len = *body_off;
    *body_len = bl;
    return 1;
}

/* ---------------------------------------------------------------- small helpers for the graphical shell */

void net_poll(void) { poll(); }

/* ip address as text */
void net_my_ip(char *out, u32 cap) { fmt_ip(out, cap, my_ip); }

static int bring_up(void)
{
    if (!nic_ready() && !nic_init()) { kprintf(C_BAD "net: %s" C_RST "\n", nic_name()); return 0; }
    calibrate();
    return 1;
}

void net_command(const char *args)
{
    while (*args == ' ') args++;
    if (!kstrncmp(args, "ip", 2) && (args[2] == ' ' || !args[2])) {
        char a[24] = "", b[24] = "", c[24] = "";
        const char *p = args + 2;
        char *dst[3] = { a, b, c };
        for (int i = 0; i < 3; i++) {
            while (*p == ' ') p++;
            int n = 0;
            while (*p && *p != ' ' && n < 23) dst[i][n++] = *p++;
            dst[i][n] = 0;
        }
        u32 ip, mask = my_mask, gw = my_gw;
        if (!parse_ip(a, &ip) || (b[0] && !parse_ip(b, &mask)) || (c[0] && !parse_ip(c, &gw))) {
            kprintf("Usage: as net ip <a.b.c.d> [mask] [gateway]      e.g. as net ip 10.0.2.15 255.255.255.0 10.0.2.2\n");
            return;
        }
        my_ip = ip; my_mask = mask; my_gw = gw;
        for (int i = 0; i < 8; i++) arp[i].ok = 0;
        kprintf(TT("Address set (not saved across restarts).\n"));
        return;
    }
    if (!kstrncmp(args, "dhcp", 4) && !args[4]) {
        if (!bring_up()) return;
        kprintf(TT("Asking for an address (DHCP) ...\n"));
        if (dhcp_run()) {
            char s1[20], s2[20], s3[20], s4[20];
            fmt_ip(s1, sizeof s1, my_ip); fmt_ip(s2, sizeof s2, my_mask); fmt_ip(s3, sizeof s3, my_gw); fmt_ip(s4, sizeof s4, dns_ip);
            kprintf(C_OK "%s" C_RST, s1); kprintf(TT("  mask %s  gateway %s  DNS %s\n"), s2, s3, s4);
        } else kprintf(C_BAD "%s" C_RST, TT("No DHCP answer")); kprintf(TT(" - the old address is kept (set one by hand: as net ip ...)\n"));
        return;
    }
    if (!kstrncmp(args, "dns", 3) && (args[3] == ' ' || !args[3])) {
        const char *n = args + 3; while (*n == ' ') n++;
        if (!*n) { kprintf(TT("Usage: as net dns <name>\n")); return; }
        if (!bring_up()) return;
        u32 ip;
        if (dns_resolve(n, &ip)) { char s[20]; fmt_ip(s, sizeof s, ip); kprintf("%s = %s\n", n, s); }
        else { kprintf(C_BAD); kprintf(TT("%s: no answer / name not found"), n); kprintf(C_RST "\n"); }
        return;
    }
    if (!kstrcmp(args, "manager") || !kstrcmp(args, "gui")) {
        if (ui_active()) ui_launch_net(); else kprintf(TT("The network manager window needs the graphical desktop.\n"));
        return;
    }
    if (*args) { kprintf("Usage: as net [ip <a.b.c.d> [mask] [gateway] | dhcp | dns <name> | manager]\n"); return; }
    int up = bring_up();
    kprintf(TT("Network card : %s\n"), nic_name());
    if (!up) return;
    const u8 *m = nic_mac();
    char s1[20], s2[20], s3[20];
    fmt_ip(s1, sizeof s1, my_ip); fmt_ip(s2, sizeof s2, my_mask); fmt_ip(s3, sizeof s3, my_gw);
    kprintf(TT("MAC address  : %02x:%02x:%02x:%02x:%02x:%02x\n"), m[0], m[1], m[2], m[3], m[4], m[5]);
    kprintf(TT("Link         : %s\n"), nic_link() ? TT("up") : TT("down (no cable / the emulator has no network)"));
    kprintf(TT("IPv4         : %s  mask %s  gateway %s\n"), s1, s2, s3);
    { char s4[20]; fmt_ip(s4, sizeof s4, dns_ip); kprintf(TT("DNS          : %s\n"), s4); }
    kprintf(C_DIM "ARP, IPv4, ICMP (as ping), UDP, DHCP (as net dhcp), DNS, TCP, HTTP and HTTPS/TLS (as get). Wi-Fi / Bluetooth: as wifi, as bluetooth." C_RST "\n");
}

/* what the network manager (window and `as net`) shows */
void net_status(net_info *o)
{
    kmemset(o, 0, sizeof *o);
    o->card = nic_ready() || nic_init();
    kstrncpy(o->name, nic_name(), sizeof o->name - 1);
    if (!o->card) return;
    kmemcpy(o->mac, nic_mac(), 6);
    o->link = nic_link();
    o->ip = my_ip; o->mask = my_mask; o->gw = my_gw; o->dns = dns_ip;
}

void net_fmt_ip(char *o, u32 cap, u32 v) { fmt_ip(o, cap, v); }

/* `as weather`: one line "WX|temperature|condition|place" from wttr.in (the notch panel reads it), or "WX|ERR|why" */
void weather_command(const char *args)
{
    (void)args;
    const char *body = 0; u32 blen = 0;
    char url[96];
    ksnprintf(url, sizeof url, "https://wttr.in/?format=%%t|%%C|%%l&m&lang=%s", config_tr("en", "tr"));
    int st = 0, tr = 0;
    if (!net_http_get(url, &body, &blen, &st, &tr, 0)) { kprintf("WX|ERR|%s\n", net_http_error()); return; }
    if (st != 200 || !body) { kprintf("WX|ERR|The weather service answered with status %d\n", st); return; }
    char out[100]; u32 n = 0;
    for (u32 i = 0; i < blen && n < sizeof out - 1 && body[i] != '\n' && body[i] != '\r'; i++) {
        unsigned char c = (unsigned char)body[i];
        if (c == 0xC2 && i + 1 < blen && (unsigned char)body[i + 1] == 0xB0) { out[n++] = ' '; i++; continue; }   /* the degree sign */
        if (c >= 32 && c < 127) out[n++] = (char)c;
    }
    out[n] = 0;
    if (!kstrncmp(out, "<", 1) || kstrchr(out, '<') || n == 0) { kprintf("WX|ERR|Unreadable answer from the weather service\n"); return; }
    kprintf("WX|%s\n", out);
}

/* `as wifi`: there is no Wi-Fi driver - say so, and what is there instead */
void wifi_command(const char *args)
{
    while (*args == ' ') args++;
    char who[48];
    int n = pci_wireless_scan(0, who, sizeof who);
    int a = !kstrncmp(args, "scan", 4) || !kstrncmp(args, "connect", 7) || !kstrncmp(args, "disconnect", 10);
    if (*args && !a && kstrcmp(args, "status")) { kprintf("Usage: as wifi [status | scan | connect <ssid> <password> | disconnect]\n"); return; }
    kprintf(TT("Wi-Fi adapter: %s\n"), n ? who : TT("none found"));
    if (n) { kprintf(C_BAD "%s" C_RST, TT("AstraSageOS has no driver for it")); kprintf(TT(" (a Wi-Fi chip needs firmware and an 802.11 stack).\n")); }
    else   kprintf(TT("An emulator (QEMU, Limbo, Bochs) has no radio: the phone's or PC's Wi-Fi reaches AstraSageOS as the wired card.\n"));
    if (a) kprintf(C_BAD "%s is not possible without a Wi-Fi adapter and driver." C_RST "\n", !kstrncmp(args, "scan", 4) ? "Scanning" : "Connecting");
    kprintf("Internet access:  as net   (card and address),  as net dhcp,  as ping <ip>,  as get <url>\n");
}

/* `as bluetooth`: same honesty - Bluetooth controllers are USB devices and there is no USB stack yet */
void bluetooth_command(const char *args)
{
    while (*args == ' ') args++;
    char who[48];
    int n = pci_wireless_scan(1, who, sizeof who);
    int a = !kstrncmp(args, "scan", 4) || !kstrncmp(args, "pair", 4) || !kstrncmp(args, "on", 2) || !kstrncmp(args, "off", 3);
    if (*args && !a && kstrcmp(args, "status")) { kprintf("Usage: as bluetooth [status | scan | pair <name> | on | off]\n"); return; }
    kprintf(TT("Bluetooth controller: %s\n"), n ? who : TT("none found"));
    kprintf("Bluetooth controllers are USB devices and AstraSageOS has no USB stack yet%s.\n", n ? " (and no Bluetooth driver)" : "");
    if (a) kprintf(C_BAD "Nothing to %s." C_RST "\n", !kstrncmp(args, "scan", 4) ? "scan" : "switch or pair");
}

void ping_command(const char *args)
{
    while (*args == ' ') args++;
    u32 ip;
    if (!parse_ip(args, &ip)) { kprintf("Usage: as ping <a.b.c.d> [count]        (names are not supported: no DNS yet)\n"); return; }
    int count = 4;
    const char *p = args; while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    if (*p >= '0' && *p <= '9') { count = 0; while (*p >= '0' && *p <= '9' && count < 100) count = count * 10 + (*p++ - '0'); if (count < 1) count = 1; }
    if (!bring_up()) return;
    if (!nic_link()) kprintf(C_DIM "(link is down - trying anyway)" C_RST "\n");
    char s[20]; fmt_ip(s, sizeof s, ip);
    u32 next = ((ip & my_mask) == (my_ip & my_mask)) ? ip : my_gw;
    u8 mac[6];
    int got = 0;
    kprintf("PING %s%s\n", s, next == ip ? "" : "  (via gateway)");
    if (!resolve(next, mac, 2000)) { kprintf(C_BAD "Request timed out: %s did not answer ARP" C_RST " (nothing reachable there)\n", next == ip ? s : "the gateway"); return; }
    for (int seq = 1; seq <= count; seq++) {
        static u8 pk[64];
        pk[0] = 8; pk[1] = 0; put16(pk + 2, 0); put16(pk + 4, 0x4153); put16(pk + 6, (u16)seq);
        for (int i = 8; i < 40; i++) pk[i] = (u8)(0x61 + (i % 23));
        put16(pk + 2, csum(pk, 40));
        echo_seq = seq;
        u32 start = now_ms();
        send_ip(mac, ip, 1, pk, 40);
        int done = 0;
        while (now_ms() - start < 2000) { poll(); if (echo_seq == -2) { done = 1; break; } }
        if (done) {
            u32 rtt = now_ms() - start;
            char f[20]; fmt_ip(f, sizeof f, echo_from);
            kprintf("40 bytes from %s: seq=%d ttl=%u time=%u ms\n", f, seq, echo_ttl, rtt);
            got++;
        } else kprintf(C_BAD "seq=%d: request timed out" C_RST "\n", seq);
        if (seq < count) { u32 w = now_ms(); while (now_ms() - w < 700) poll(); }
    }
    echo_seq = -1;
    kprintf("%d sent, %d received, %d%% loss\n", count, got, (count - got) * 100 / count);
}
