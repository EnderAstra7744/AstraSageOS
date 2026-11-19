/*
 * ATA PIO driver (primary channel, master drive, LBA28). Polled, no interrupts.
 * QEMU: -drive file=astrasage.disk,format=raw,if=ide,index=0   (or -hda)
 */
#include "astra.h"
#include "io.h"

#define ATA_DATA   0x1F0
#define ATA_ERR    0x1F1
#define ATA_COUNT  0x1F2
#define ATA_LBA0   0x1F3
#define ATA_LBA1   0x1F4
#define ATA_LBA2   0x1F5
#define ATA_DRIVE  0x1F6
#define ATA_CMD    0x1F7
#define ATA_STATUS 0x1F7
#define ATA_CTRL   0x3F6

#define ST_ERR 0x01
#define ST_DRQ 0x08
#define ST_DF  0x20
#define ST_RDY 0x40
#define ST_BSY 0x80

static u32 total_sectors_d[2];

static void delay400(void)
{
    for (int i = 0; i < 4; i++) (void)inb(ATA_CTRL);
}

/* Waits for BSY to clear; returns the status byte or -1 on timeout. */
static int wait_not_busy(void)
{
    for (long i = 0; i < 4000000L; i++) {
        u8 s = inb(ATA_STATUS);
        if (!(s & ST_BSY)) return s;
    }
    return -1;
}

static int wait_drq(void)
{
    for (long i = 0; i < 4000000L; i++) {
        u8 s = inb(ATA_STATUS);
        if (s & (ST_ERR | ST_DF)) return -1;
        if (!(s & ST_BSY) && (s & ST_DRQ)) return 0;
    }
    return -1;
}

/* drive 0 = primary master, drive 1 = primary slave */
int ata_init_drive(int d)
{
    total_sectors_d[d] = 0;
    if (inb(ATA_STATUS) == 0xFF) return 0;            /* floating bus: no controller */

    outb(ATA_CTRL, 0x02);                              /* nIEN: polled operation */
    outb(ATA_DRIVE, (u8)(0xA0 | (d << 4)));            /* select drive, CHS/LBA bit cleared for IDENTIFY */
    delay400();
    outb(ATA_COUNT, 0); outb(ATA_LBA0, 0); outb(ATA_LBA1, 0); outb(ATA_LBA2, 0);
    outb(ATA_CMD, 0xEC);                               /* IDENTIFY */
    if (inb(ATA_STATUS) == 0) return 0;                /* no drive */
    if (wait_not_busy() < 0) return 0;
    if (inb(ATA_LBA1) || inb(ATA_LBA2)) return 0;      /* ATAPI / SATA signature: not supported */
    if (wait_drq() < 0) return 0;

    u16 id[256];
    for (int i = 0; i < 256; i++) id[i] = inw(ATA_DATA);
    total_sectors_d[d] = (u32)id[60] | ((u32)id[61] << 16);
    return total_sectors_d[d] != 0;
}

u32 ata_sectors_drive(int d) { return total_sectors_d[d]; }

static int setup(int d, u32 lba, u8 cmd)
{
    if (!total_sectors_d[d] || lba >= total_sectors_d[d]) return -1;
    if (wait_not_busy() < 0) return -1;
    outb(ATA_DRIVE, (u8)(0xE0 | (d << 4) | ((lba >> 24) & 0x0F)));
    delay400();
    outb(ATA_COUNT, 1);
    outb(ATA_LBA0, (u8)lba);
    outb(ATA_LBA1, (u8)(lba >> 8));
    outb(ATA_LBA2, (u8)(lba >> 16));
    outb(ATA_CMD, cmd);
    return 0;
}

int ata_read_drive(int d, u32 lba, void *buf512)
{
    if (setup(d, lba, 0x20) < 0) return -1;            /* READ SECTORS */
    delay400();
    if (wait_drq() < 0) return -1;
    u16 *w = (u16 *)buf512;
    for (int i = 0; i < 256; i++) w[i] = inw(ATA_DATA);
    return 0;
}

int ata_write_drive(int d, u32 lba, const void *buf512)
{
    if (setup(d, lba, 0x30) < 0) return -1;            /* WRITE SECTORS */
    delay400();
    if (wait_drq() < 0) return -1;
    const u16 *w = (const u16 *)buf512;
    for (int i = 0; i < 256; i++) outw(ATA_DATA, w[i]);
    outb(ATA_CMD, 0xE7);                               /* CACHE FLUSH */
    int s = wait_not_busy();
    if (s < 0 || (s & (ST_ERR | ST_DF))) return -1;
    return 0;
}

/* the OS data disk is always drive 0 */
int ata_init(void)                          { return ata_init_drive(0); }
u32 ata_sectors(void)                       { return total_sectors_d[0]; }
int ata_read(u32 lba, void *b)              { return ata_read_drive(0, lba, b); }
int ata_write(u32 lba, const void *b)       { return ata_write_drive(0, lba, b); }
