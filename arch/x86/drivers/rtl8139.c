/*
 * Realtek RTL8139 network card (QEMU "-nic model=rtl8139", the default card of some Limbo setups). Polled, no interrupts.
 * One 8 KiB receive ring (with the WRAP slack the chip needs) and the four transmit descriptors, flat memory.
 * NOTE: written from the RTL8139 programming guide; it has not been run on a real chip or in QEMU yet.
 */
#include "astra.h"
#include "io.h"

#define R_IDR0  0x00
#define R_TSD0  0x10
#define R_TSAD0 0x20
#define R_RBSTART 0x30
#define R_CR    0x37
#define R_CAPR  0x38
#define R_IMR   0x3C
#define R_ISR   0x3E
#define R_TCR   0x40
#define R_RCR   0x44
#define R_CONFIG1 0x52
#define R_MSR   0x58

#define RX_LEN 8192
#define RX_BUF (RX_LEN + 16 + 1536)
#define TX_BUF 1792

extern u32  pci_cfg_r(u32 bus, u32 dev, u32 off);
extern void pci_cfg_w(u32 bus, u32 dev, u32 off, u32 v);

static u16 io;
static u32 rx_off;
static int tx_cur, up;
static u8 rxbuf[RX_BUF] __attribute__((aligned(16)));
static u8 txbuf[4][TX_BUF] __attribute__((aligned(16)));

static void delay(void) { inb(0x80); }

int rtl_init(u32 dev, u8 *mac)
{
    u32 bar = pci_cfg_r(0, dev, 0x10);
    if (!(bar & 1)) return 0;                                      /* BAR0 must be an I/O range */
    io = (u16)(bar & ~3u);
    pci_cfg_w(0, dev, 4, (pci_cfg_r(0, dev, 4) & 0xFFFF) | 5);     /* I/O space + bus master */
    outb(io + R_CONFIG1, 0);                                       /* power on */
    outb(io + R_CR, 0x10);                                         /* reset */
    for (int i = 0; i < 100000 && (inb(io + R_CR) & 0x10); i++) delay();
    if (inb(io + R_CR) & 0x10) return 0;
    for (int i = 0; i < 6; i++) mac[i] = inb(io + R_IDR0 + i);
    outl(io + R_RBSTART, (u32)(unsigned long)rxbuf);
    outw(io + R_IMR, 0);                                           /* polled */
    outw(io + R_ISR, 0xFFFF);
    outl(io + R_RCR, 0x0F | (1u << 7));                            /* accept all / multicast / mine / broadcast, WRAP */
    outl(io + R_TCR, (6u << 8));                                   /* DMA burst 1024 */
    outb(io + R_CR, 0x0C);                                         /* receiver + transmitter on */
    rx_off = 0; tx_cur = 0; up = 1;
    return 1;
}

int rtl_link(void) { return up && !(inb(io + R_MSR) & 4); }

int rtl_send(const void *data, u32 len)
{
    if (!up || len == 0 || len > 1514) return -1;
    u16 tsd = (u16)(io + R_TSD0 + tx_cur * 4);
    for (int i = 0; i < 1000000; i++) {                            /* OWN (bit 13) is set again when the chip is done */
        u32 s = inl(tsd);
        if (s & (1u << 13) || !(s & 0x1FFF)) break;
        delay();
    }
    u8 *b = txbuf[tx_cur];
    for (u32 i = 0; i < len; i++) b[i] = ((const u8 *)data)[i];
    while (len < 60) b[len++] = 0;
    outl(io + R_TSAD0 + tx_cur * 4, (u32)(unsigned long)b);
    outl(tsd, len);                                                /* writing the size starts the transfer */
    tx_cur = (tx_cur + 1) & 3;
    return 0;
}

int rtl_recv(void *buf, u32 cap)
{
    if (!up || (inb(io + R_CR) & 1)) return 0;                     /* buffer empty */
    u8 *p = rxbuf + rx_off;
    u16 status = (u16)(p[0] | p[1] << 8), len = (u16)(p[2] | p[3] << 8);
    u32 n = 0;
    if ((status & 1) && len >= 64 && len <= 1518 + 4) {            /* ROK and a sane frame; len includes the 4-byte CRC */
        n = (u32)len - 4;
        if (n > cap) n = cap;
        for (u32 i = 0; i < n; i++) ((u8 *)buf)[i] = p[4 + i];
    } else if (!(status & 1) || len < 8 || len > 1518 + 4) {       /* the ring is out of step: start over */
        outb(io + R_CR, 0x04); outb(io + R_CR, 0x0C);
        rx_off = 0;
        outl(io + R_RBSTART, (u32)(unsigned long)rxbuf);
        outw(io + R_CAPR, (u16)(0 - 16));
        return 0;
    }
    rx_off = (rx_off + len + 4 + 3) & ~3u;
    if (rx_off >= RX_LEN) rx_off -= RX_LEN;
    outw(io + R_CAPR, (u16)(rx_off - 16));
    outw(io + R_ISR, 0xFFFF);
    return (int)n;
}
