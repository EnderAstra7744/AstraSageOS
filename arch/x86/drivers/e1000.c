/*
 * Network card driver: Intel PRO/1000 (e1000 family, 82540/82545/... - what QEMU, Limbo and Bochs emulate).
 * Polled (no interrupts), one receive ring and one transmit ring, flat memory (virtual = physical).
 * The Realtek RTL8139 is driven by drives/rtl8139.c (nic_* below pick the card that was found).
 * virtio-net and e1000e are detected and reported but NOT driven.
 */
#include "astra.h"
#include "io.h"

#define R_CTRL   0x0000
#define R_STATUS 0x0008
#define R_EERD   0x0014
#define R_ICR    0x00C0
#define R_IMC    0x00D8
#define R_RCTL   0x0100
#define R_TCTL   0x0400
#define R_TIPG   0x0410
#define R_RDBAL  0x2800
#define R_RDBAH  0x2804
#define R_RDLEN  0x2808
#define R_RDH    0x2810
#define R_RDT    0x2818
#define R_TDBAL  0x3800
#define R_TDBAH  0x3804
#define R_TDLEN  0x3808
#define R_TDH    0x3810
#define R_TDT    0x3818
#define R_MTA    0x5200
#define R_RAL0   0x5400
#define R_RAH0   0x5404

#define RX_N 32
#define TX_N 8
#define BUF  2048

typedef struct { u32 lo, hi; u16 len, csum; u8 status, errors; u16 special; } __attribute__((packed)) rxd;
typedef struct { u32 lo, hi; u16 len; u8 cso, cmd, status, css; u16 special; } __attribute__((packed)) txd;

static rxd rx[RX_N] __attribute__((aligned(128)));
static txd tx[TX_N] __attribute__((aligned(128)));
static u8 rxb[RX_N][BUF] __attribute__((aligned(16)));
static u8 txb[TX_N][BUF] __attribute__((aligned(16)));
static u32 base;
static int rx_cur, tx_cur, ready;
static u8 mac_addr[6];
static const char *nic_label = "no network card found";
static char nic_found[48];

static u32 rd(u32 o) { return *(volatile u32 *)(base + o); }
static void wr(u32 o, u32 v) { *(volatile u32 *)(base + o) = v; }
static void io_delay(void) { inb(0x80); }

u32 pci_cfg_r(u32 bus, u32 dev, u32 off);
void pci_cfg_w(u32 bus, u32 dev, u32 off, u32 v);
int rtl_init(u32 dev, u8 *mac);
int rtl_link(void);
int rtl_send(const void *data, u32 len);
int rtl_recv(void *buf, u32 cap);
static int use_rtl;

static u32 pci_r(u32 bus, u32 dev, u32 off)
{
    outl(0xCF8, 0x80000000u | bus << 16 | dev << 11 | (off & 0xFC));
    return inl(0xCFC);
}
static void pci_w(u32 bus, u32 dev, u32 off, u32 v)
{
    outl(0xCF8, 0x80000000u | bus << 16 | dev << 11 | (off & 0xFC));
    outl(0xCFC, v);
}
u32 pci_cfg_r(u32 bus, u32 dev, u32 off) { return pci_r(bus, dev, off); }
void pci_cfg_w(u32 bus, u32 dev, u32 off, u32 v) { pci_w(bus, dev, off, v); }

static int is_e1000(u16 id)
{
    static const u16 ids[] = { 0x1004, 0x100E, 0x100F, 0x1010, 0x1011, 0x1015, 0x1016, 0x1017, 0x1018, 0x1019, 0x101D,
                               0x1026, 0x1027, 0x1028, 0x1075, 0x1076, 0x1077, 0x1078, 0x1079, 0x107C };
    for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; i++) if (ids[i] == id) return 1;
    return 0;
}

static u16 eeprom(u32 a)
{
    wr(R_EERD, (a << 8) | 1);
    for (int i = 0; i < 100000; i++) {
        u32 v = rd(R_EERD);
        if (v & 0x10) return (u16)(v >> 16);
        io_delay();
    }
    return 0;
}

int nic_init(void)
{
    if (ready) return 1;
    int dev_found = -1;
    for (u32 dev = 0; dev < 32; dev++) {
        u32 id = pci_r(0, dev, 0);
        if (id == 0xFFFFFFFFu || id == 0) continue;
        u16 ven = (u16)id, did = (u16)(id >> 16);
        if (ven == 0x8086 && is_e1000(did)) { dev_found = (int)dev; ksnprintf(nic_found, sizeof nic_found, "Intel PRO/1000 (e1000) %04x:%04x", ven, did); break; }
        if (ven == 0x10EC && did == 0x8139 && !use_rtl) {
            if (rtl_init(dev, mac_addr)) { use_rtl = 1; ready = 1; nic_label = "Realtek RTL8139 (untested driver)"; return 1; }
            nic_label = "RTL8139 network card found, but it did not start";
        }
        else if (ven == 0x1AF4 && (did == 0x1000 || did == 0x1041)) nic_label = "virtio-net card found, but only Intel e1000 is supported";
        else if (ven == 0x8086 && (did == 0x10D3 || did == 0x153A || did == 0x10EA)) nic_label = "Intel e1000e card found, but only the older e1000 is supported";
        else if (ven == 0x1022 && did == 0x2000) nic_label = "AMD PCnet card found, but only Intel e1000 is supported";
    }
    if (dev_found < 0) return 0;
    u32 dev = (u32)dev_found;
    u32 bar = pci_r(0, dev, 0x10);
    if (bar & 1) { nic_label = "e1000 BAR0 is an I/O port range (unsupported)"; return 0; }
    base = bar & ~0xFu;
    if (!base) { nic_label = "e1000 memory BAR is empty"; return 0; }
    pci_w(0, dev, 4, (pci_r(0, dev, 4) & 0xFFFF) | 6);            /* memory space + bus master */

    wr(R_IMC, 0xFFFFFFFFu);
    wr(R_RCTL, 0);
    wr(R_TCTL, 8);
    wr(R_CTRL, rd(R_CTRL) | (1u << 26));                          /* reset */
    for (int i = 0; i < 200000 && (rd(R_CTRL) & (1u << 26)); i++) io_delay();
    wr(R_IMC, 0xFFFFFFFFu);
    (void)rd(R_ICR);
    wr(R_CTRL, (rd(R_CTRL) | (1u << 6) | (1u << 5)) & ~((1u << 3) | (1u << 31) | (1u << 30)));   /* link up, auto speed */

    u32 ral = rd(R_RAL0), rah = rd(R_RAH0);
    if (rah & 0x80000000u) {
        mac_addr[0] = (u8)ral; mac_addr[1] = (u8)(ral >> 8); mac_addr[2] = (u8)(ral >> 16); mac_addr[3] = (u8)(ral >> 24);
        mac_addr[4] = (u8)rah; mac_addr[5] = (u8)(rah >> 8);
    } else {
        for (int i = 0; i < 3; i++) { u16 w = eeprom((u32)i); mac_addr[i * 2] = (u8)w; mac_addr[i * 2 + 1] = (u8)(w >> 8); }
    }
    wr(R_RAL0, mac_addr[0] | mac_addr[1] << 8 | mac_addr[2] << 16 | (u32)mac_addr[3] << 24);
    wr(R_RAH0, mac_addr[4] | mac_addr[5] << 8 | 0x80000000u);
    for (u32 i = 0; i < 128; i++) wr(R_MTA + i * 4, 0);

    for (int i = 0; i < RX_N; i++) { rx[i].lo = (u32)(unsigned long)rxb[i]; rx[i].hi = 0; rx[i].status = 0; }
    wr(R_RDBAL, (u32)(unsigned long)rx); wr(R_RDBAH, 0); wr(R_RDLEN, RX_N * 16);
    wr(R_RDH, 0); wr(R_RDT, RX_N - 1); rx_cur = 0;
    wr(R_RCTL, (1u << 1) | (1u << 15) | (1u << 26));              /* enable, accept broadcast, strip CRC, 2048-byte buffers */

    for (int i = 0; i < TX_N; i++) { tx[i].lo = (u32)(unsigned long)txb[i]; tx[i].hi = 0; tx[i].status = 1; tx[i].cmd = 0; }
    wr(R_TDBAL, (u32)(unsigned long)tx); wr(R_TDBAH, 0); wr(R_TDLEN, TX_N * 16);
    wr(R_TDH, 0); wr(R_TDT, 0); tx_cur = 0;
    wr(R_TIPG, 0x0060200A);
    wr(R_TCTL, (1u << 1) | (1u << 3) | (0x10u << 4) | (0x40u << 12));
    ready = 1;
    nic_label = nic_found;
    return 1;
}

const char *nic_name(void) { return nic_label; }
const u8 *nic_mac(void) { return mac_addr; }
int nic_is_rtl(void) { return use_rtl; }
int nic_ready(void) { return ready; }
int nic_link(void) { return use_rtl ? rtl_link() : (ready && (rd(R_STATUS) & 2)); }

int nic_send(const void *data, u32 len)
{
    if (use_rtl) return rtl_send(data, len);
    if (!ready || len == 0 || len > 1514) return -1;
    txd *d = &tx[tx_cur];
    for (int i = 0; i < 1000000 && !(d->status & 1); i++) io_delay();
    if (!(d->status & 1)) return -1;
    u8 *b = txb[tx_cur];
    for (u32 i = 0; i < len; i++) b[i] = ((const u8 *)data)[i];
    while (len < 60) b[len++] = 0;
    d->len = (u16)len; d->cmd = 0x0B; d->status = 0;              /* EOP | IFCS | RS */
    tx_cur = (tx_cur + 1) % TX_N;
    wr(R_TDT, (u32)tx_cur);
    return 0;
}

int nic_recv(void *buf, u32 cap)
{
    if (use_rtl) return rtl_recv(buf, cap);
    if (!ready) return 0;
    rxd *d = &rx[rx_cur];
    if (!(d->status & 1)) return 0;
    u32 n = d->len;
    if (!(d->status & 2) || (d->errors) || n > BUF) n = 0;        /* not a whole good frame: drop */
    if (n > cap) n = cap;
    for (u32 i = 0; i < n; i++) ((u8 *)buf)[i] = rxb[rx_cur][i];
    d->status = 0;
    wr(R_RDT, (u32)rx_cur);
    rx_cur = (rx_cur + 1) % RX_N;
    return (int)n;
}

/* ---- wireless adapters on the PCI bus: only reported, there are no Wi-Fi / Bluetooth drivers ---- */
/* kind 0: Wi-Fi (network controller "other" 02:80, wireless 0D:20/21), kind 1: Bluetooth (0D:11). Returns how many were found. */
int pci_wireless_scan(int kind, char *first, u32 cap)
{
    int n = 0;
    if (cap) first[0] = 0;
    for (u32 bus = 0; bus < 8; bus++)
        for (u32 dev = 0; dev < 32; dev++) {
            u32 id = pci_r(bus, dev, 0);
            if (id == 0xFFFFFFFFu || id == 0) continue;
            u32 cl = pci_r(bus, dev, 8) >> 16;                    /* class << 8 | subclass */
            int wifi = cl == 0x0280 || cl == 0x0D20 || cl == 0x0D21, bt = cl == 0x0D11;
            if ((kind == 0 && wifi) || (kind == 1 && bt)) {
                if (!n && cap) ksnprintf(first, cap, "%04x:%04x (bus %u, slot %u)", id & 0xFFFF, id >> 16, bus, dev);
                n++;
            }
        }
    return n;
}
