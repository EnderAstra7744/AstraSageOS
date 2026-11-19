/*
 * AstraSageOS ARM64 port (QEMU "virt" machine). What it does today:
 *   - PL011 serial console (input and output)
 *   - ramfb framebuffer 1280x720 (fw_cfg) with an 8x16 text console (the OS font)
 *   - virtio-mmio keyboard (virtio-input) and virtio-blk disk (raw sector read/write; no file system yet)
 *   - generic timer (uptime), PSCI power off / reset
 *   - a small AST shell: help ver uptime mem echo clear color reboot halt
 * Not ported yet (the i386 kernel has them): MMU / caches, GIC interrupts, virtio-blk (disk + file system), virtio-net,
 * mouse, the desktop (gui/), AstraScript, ASPI. Those need the 32-bit-only parts of the code base to be made 64-bit clean.
 * NOTE: written without an ARM64 machine at hand - it compiles (clang) but has not been run. See arch/README.md.
 */
#include "../../kernel/astra.h"
typedef unsigned long u64;
#include "../../font/font8x16.h"

#define UART0   0x09000000UL
#define FWCFG   0x09020000UL
#define VIRTIO  0x0a000000UL
#define FB_ADDR 0x44000000UL
#define FB_W 1280
#define FB_H 720

static inline void w32(u64 a, u32 v) { *(volatile u32 *)a = v; }
static inline u32  r32(u64 a)        { return *(volatile u32 *)a; }
static inline u32  bswap32(u32 v)    { return __builtin_bswap32(v); }
static inline u64  bswap64(u64 v)    { return __builtin_bswap64(v); }
static inline void dmb(void)         { __asm__ volatile("dmb sy" ::: "memory"); }

/* ---------- serial ---------- */
static void uart_putc(char c) { while (r32(UART0 + 0x18) & (1u << 5)) { } w32(UART0, (u32)(u8)c); }
static int  uart_getc(void)   { if (r32(UART0 + 0x18) & (1u << 4)) return -1; return (int)(r32(UART0) & 0xFF); }

/* ---------- framebuffer + text console ---------- */
static u32 *fb; static int fb_ok;
static int cx, cy, cols = FB_W / 8, rows = FB_H / 16;
static u32 fg = 0xE0E0E0, bg = 0x101018;

static void glyph(int x, int y, unsigned ch)
{
    if (ch < AS_FONT_FIRST || ch >= AS_FONT_FIRST + AS_FONT_COUNT) ch = '?';
    const unsigned char *g = &as_font8x16[(ch - AS_FONT_FIRST) * 16];
    for (int r = 0; r < 16; r++) for (int c = 0; c < 8; c++) fb[(y + r) * FB_W + x + c] = (g[r] & (0x80 >> c)) ? fg : bg;
}
static void fb_clear(void) { for (int i = 0; i < FB_W * FB_H; i++) fb[i] = bg; cx = cy = 0; }
static void scroll(void)
{
    for (int i = 0; i < FB_W * (FB_H - 16); i++) fb[i] = fb[i + FB_W * 16];
    for (int i = FB_W * (FB_H - 16); i < FB_W * FB_H; i++) fb[i] = bg;
    cy = rows - 1;
}
static void putc_(char c)
{
    uart_putc(c);
    if (c == '\n') uart_putc('\r');
    if (!fb_ok) return;
    if (c == '\n') { cx = 0; cy++; }
    else if (c == '\b') { if (cx) { cx--; glyph(cx * 8, cy * 16, ' '); } }
    else { glyph(cx * 8, cy * 16, (u8)c); if (++cx >= cols) { cx = 0; cy++; } }
    if (cy >= rows) scroll();
}
static void puts_(const char *s) { while (*s) putc_(*s++); }
static void putu(u64 v) { char b[24]; int n = 0; do { b[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) putc_(b[--n]); }
static void puthex(u64 v) { puts_("0x"); for (int i = 60; i >= 0; i -= 4) putc_("0123456789abcdef"[(v >> i) & 15]); }

/* ---------- fw_cfg (DMA) -> ramfb ---------- */
static void fw_dma(u32 control, void *buf, u32 len)
{
    static volatile struct { u32 control, length; u64 address; } __attribute__((aligned(16))) d;
    d.control = bswap32(control); d.length = bswap32(len); d.address = bswap64((u64)buf);
    dmb();
    *(volatile u64 *)(FWCFG + 16) = bswap64((u64)&d);
    while (bswap32(d.control) & ~1u) { }                 /* the device clears everything but the error bit */
}
#define FW_SELECT (1u << 3)
#define FW_READ   (1u << 1)
#define FW_WRITE  (1u << 4)
static int fw_find(const char *name, u32 *sel)
{
    u32 n; u32 ctl = (0x19u << 16) | FW_SELECT | FW_READ;
    fw_dma(ctl, &n, 4); n = bswap32(n);
    for (u32 i = 0; i < n && i < 64; i++) {
        struct { u32 size; u16 select, res; char name[56]; } e;
        fw_dma(FW_READ, &e, sizeof e);
        int same = 1;
        for (int k = 0; name[k] || e.name[k]; k++) if (name[k] != e.name[k]) { same = 0; break; }
        if (same) { *sel = (u32)(u16)__builtin_bswap16(e.select); return 1; }
    }
    return 0;
}
static int ramfb_init(void)
{
    u32 sel;
    { u32 sig; fw_dma((0u << 16) | FW_SELECT | FW_READ, &sig, 4); if (sig != 0x554D4551u) return 0; }   /* "QEMU" */
    if (!fw_find("etc/ramfb", &sel)) return 0;
    struct { u64 addr; u32 fourcc, flags, w, h, stride; } __attribute__((packed)) cfg;
    cfg.addr = bswap64(FB_ADDR); cfg.fourcc = bswap32(0x34325258u);   /* 'XR24' */
    cfg.flags = 0; cfg.w = bswap32(FB_W); cfg.h = bswap32(FB_H); cfg.stride = bswap32(FB_W * 4);
    fw_dma((sel << 16) | FW_SELECT | FW_WRITE, &cfg, sizeof cfg);
    fb = (u32 *)FB_ADDR; fb_ok = 1; fb_clear();
    return 1;
}

/* ---------- virtio-mmio keyboard ---------- */
#define QN 16
static u8 vq_mem[2][8192] __attribute__((aligned(4096)));
static struct { u16 type, code; u32 value; } evbuf[QN];
static u64 kbd_base; static u16 kbd_last; static int vkbd_ok;
static int shift_state;

static const char kmap[58]  = { 0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ' };
static const char kmapS[58] = { 0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ' };

static void vk_init(void)
{
    for (int s = 0; s < 32; s++) {
        u64 b = VIRTIO + (u64)s * 0x200;
        if (r32(b) != 0x74726976u || r32(b + 8) != 18) continue;           /* "virt", device id 18 = input */
        u32 ver = r32(b + 4);
        w32(b + 0x70, 0); w32(b + 0x70, 1); w32(b + 0x70, 1 | 2);          /* reset, ACK, DRIVER */
        if (ver >= 2) { w32(b + 0x24, 1); w32(b + 0x20, 1); w32(b + 0x24, 0); w32(b + 0x20, 0); w32(b + 0x70, 1 | 2 | 8); if (!(r32(b + 0x70) & 8)) continue; }
        else w32(b + 0x28, 4096);                                           /* legacy: guest page size */
        w32(b + 0x30, 0);                                                   /* queue 0 = events */
        if (r32(b + 0x34) < QN) continue;
        w32(b + 0x38, QN);
        u8 *q = vq_mem[0];
        u64 desc = (u64)q, avail = (u64)q + 256, used = (u64)q + 4096;
        for (int i = 0; i < QN; i++) {
            u64 *d = (u64 *)(desc + i * 16);
            d[0] = (u64)&evbuf[i]; ((u32 *)d)[2] = 8; ((u16 *)d)[6] = 2; ((u16 *)d)[7] = 0;   /* device-writable, 8 bytes */
            ((u16 *)(avail + 4))[i] = (u16)i;
        }
        ((u16 *)avail)[0] = 0; ((u16 *)avail)[1] = QN;
        dmb();
        if (ver >= 2) {
            w32(b + 0x80, (u32)desc); w32(b + 0x84, (u32)(desc >> 32));
            w32(b + 0x90, (u32)avail); w32(b + 0x94, (u32)(avail >> 32));
            w32(b + 0xa0, (u32)used); w32(b + 0xa4, (u32)(used >> 32));
            w32(b + 0x44, 1);
            w32(b + 0x70, 1 | 2 | 8 | 4);
        } else {
            w32(b + 0x3c, 4096); w32(b + 0x40, (u32)((u64)q >> 12));
            w32(b + 0x70, 1 | 2 | 4);
        }
        w32(b + 0x50, 0);                                                    /* tell the device the buffers are there */
        kbd_base = b; kbd_last = 0; vkbd_ok = 1;
        return;
    }
}
static int vk_getc(void)
{
    if (!vkbd_ok) return -1;
    u16 *used = (u16 *)((u64)vq_mem[0] + 4096);
    dmb();
    while (kbd_last != used[1]) {
        u32 id = ((u32 *)(used + 2))[(kbd_last % QN) * 2];
        kbd_last++;
        u16 type = evbuf[id % QN].type, code = evbuf[id % QN].code; u32 val = evbuf[id % QN].value;
        u16 *avail = (u16 *)((u64)vq_mem[0] + 256);                          /* give the buffer back */
        avail[2 + (avail[1] % QN)] = (u16)id; dmb(); avail[1]++; dmb();
        w32(kbd_base + 0x50, 0);
        if (type != 1) continue;
        if (code == 42 || code == 54) { shift_state = val != 0; continue; }
        if (val == 0 || code >= 58) continue;
        char c = shift_state ? kmapS[code] : kmap[code];
        if (c) return (u8)c;
    }
    return -1;
}


/* ---------- virtio-blk (polled, one request at a time) ---------- */
#define BQN 8
static u64 blk_base; static u64 blk_sectors; static int blk_ok; static u16 blk_avail_idx;
static struct { u32 type, reserved; u64 sector; } __attribute__((aligned(16))) blk_hdr;
static volatile u8 blk_status;

static void blk_init(void)
{
    for (int s = 0; s < 32; s++) {
        u64 b = VIRTIO + (u64)s * 0x200;
        if (r32(b) != 0x74726976u || r32(b + 8) != 2) continue;             /* device id 2 = block */
        u32 ver = r32(b + 4);
        w32(b + 0x70, 0); w32(b + 0x70, 1); w32(b + 0x70, 1 | 2);
        if (ver >= 2) { w32(b + 0x24, 1); w32(b + 0x20, 1); w32(b + 0x24, 0); w32(b + 0x20, 0); w32(b + 0x70, 1 | 2 | 8); if (!(r32(b + 0x70) & 8)) continue; }
        else w32(b + 0x28, 4096);
        w32(b + 0x30, 0);
        if (r32(b + 0x34) < BQN) continue;
        w32(b + 0x38, BQN);
        u8 *q = vq_mem[1];
        u64 desc = (u64)q, avail = (u64)q + 256, used = (u64)q + 4096;
        if (ver >= 2) {
            w32(b + 0x80, (u32)desc); w32(b + 0x84, (u32)(desc >> 32));
            w32(b + 0x90, (u32)avail); w32(b + 0x94, (u32)(avail >> 32));
            w32(b + 0xa0, (u32)used); w32(b + 0xa4, (u32)(used >> 32));
            w32(b + 0x44, 1); w32(b + 0x70, 1 | 2 | 8 | 4);
        } else { w32(b + 0x3c, 4096); w32(b + 0x40, (u32)((u64)q >> 12)); w32(b + 0x70, 1 | 2 | 4); }
        blk_sectors = r32(b + 0x100) | ((u64)r32(b + 0x104) << 32);
        blk_base = b; blk_ok = 1; blk_avail_idx = 0;
        return;
    }
}
/* reads (write=0) or writes (write=1) one 512-byte sector; returns 0 on success */
static int blk_rw(u64 sector, void *buf, int write)
{
    if (!blk_ok || sector >= blk_sectors) return -1;
    u8 *q = vq_mem[1];
    u64 *d0 = (u64 *)q, *d1 = (u64 *)(q + 16), *d2 = (u64 *)(q + 32);
    blk_hdr.type = write ? 1 : 0; blk_hdr.reserved = 0; blk_hdr.sector = sector;
    blk_status = 0xFF;
    d0[0] = (u64)&blk_hdr; ((u32 *)d0)[2] = 16;  ((u16 *)d0)[6] = 1; ((u16 *)d0)[7] = 1;                 /* NEXT -> 1 */
    d1[0] = (u64)buf;      ((u32 *)d1)[2] = 512; ((u16 *)d1)[6] = (u16)(1 | (write ? 0 : 2)); ((u16 *)d1)[7] = 2;
    d2[0] = (u64)&blk_status; ((u32 *)d2)[2] = 1; ((u16 *)d2)[6] = 2; ((u16 *)d2)[7] = 0;                /* device writes the status */
    u16 *avail = (u16 *)(q + 256), *used = (u16 *)(q + 4096);
    u16 want = (u16)(used[1] + 1);
    avail[2 + (blk_avail_idx % BQN)] = 0; blk_avail_idx++;
    dmb(); avail[1] = blk_avail_idx; dmb();
    w32(blk_base + 0x50, 0);
    for (u64 t = 0; used[1] != want; t++) { dmb(); if (t > 200000000UL) return -2; }
    return blk_status == 0 ? 0 : -3;
}


/* ---------- what the shared core (kernel/kprintf.c, fs/fs.c, fs/persist.c) expects from the platform ---------- */
void con_putc(char c) { putc_(c); }
int  ata_init(void) { return blk_ok; }
u32  ata_sectors(void) { return blk_sectors > 0xFFFFFFFFUL ? 0xFFFFFFFFu : (u32)blk_sectors; }
int  ata_read(u32 lba, void *b) { static u8 t[512] __attribute__((aligned(16))); if (blk_rw(lba, t, 0)) return -1; kmemcpy(b, t, 512); return 0; }
int  ata_write(u32 lba, const void *b) { static u8 t[512] __attribute__((aligned(16))); kmemcpy(t, b, 512); return blk_rw(lba, t, 1) ? -1 : 0; }
void bl_add(int level, const char *fmt, ...) { (void)level; (void)fmt; }
const char *config_tr(const char *en, const char *tr) { (void)tr; return en; }
void hal_datetime(hal_time *t) { t->year = 2026; t->month = 1; t->day = 1; t->hour = t->min = t->sec = 0; }
const char *user_name(void) { return "root"; }
int  user_home(void) { return 0; }


/* ---------- MMU + caches (identity map, 1 GiB blocks): 0-1 GiB devices, 1-2 GiB RAM cacheable, the rest device ---------- */
static u64 l1[512] __attribute__((aligned(4096)));
static int mmu_on;
static void mmu_enable(void)
{
    if (mmu_on) return;
    for (int i = 0; i < 512; i++) l1[i] = 0;
    for (int i = 0; i < 4; i++) {
        u64 attr = (i == 1) ? 1 : 0;                              /* AttrIdx 1 = Normal write-back, 0 = Device-nGnRnE */
        l1[i] = ((u64)i << 30) | (1UL << 10) | ((i == 1 ? 3UL : 0UL) << 8) | (attr << 2) | 1UL;   /* block, AF, SH inner (RAM) */
    }
    u64 mair = 0x00UL | (0xFFUL << 8);
    u64 mmfr; __asm__ volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(mmfr));
    u64 tcr = 32UL | (1UL << 8) | (1UL << 10) | (3UL << 12) | (0UL << 14) | (1UL << 23) | ((mmfr & 7UL) << 32);   /* T0SZ=32, WB WA, inner shareable, 4K, EPD1 */
    __asm__ volatile(
        "dsb sy\n"
        "msr mair_el1, %0\n"
        "msr tcr_el1, %1\n"
        "msr ttbr0_el1, %2\n"
        "isb\n"
        "tlbi vmalle1\n"
        "dsb nsh\n"
        "isb\n"
        "ic iallu\n"
        "dsb nsh\n"
        "isb\n"
        "mrs x9, sctlr_el1\n"
        "orr x9, x9, #1\n"                                        /* M */
        "orr x9, x9, #(1 << 2)\n"                                 /* C */
        "orr x9, x9, #(1 << 12)\n"                                /* I */
        "bic x9, x9, #(1 << 1)\n"                                 /* A off: unaligned access to RAM is fine */
        "msr sctlr_el1, x9\n"
        "isb\n"
        :: "r"(mair), "r"(tcr), "r"((u64)l1) : "x9", "memory");
    mmu_on = 1;
}

/* ---------- time, power ---------- */
static u64 cnt(void) { u64 v; __asm__ volatile("isb; mrs %0, cntpct_el0" : "=r"(v)); return v; }
static u64 frq(void) { u64 v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
static void psci(u32 fn) { register u64 x0 __asm__("x0") = fn; __asm__ volatile("hvc #0" : "+r"(x0) :: "memory"); }

void arm64_exception(u64 type, u64 esr, u64 elr, u64 far)
{
    puts_("\n*** AstraSageOS ARM64: exception type "); putu(type); puts_(" ESR="); puthex(esr); puts_(" ELR="); puthex(elr); puts_(" FAR="); puthex(far); puts_("\n");
}

/* ---------- shell ---------- */
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int starts(const char *s, const char *p) { while (*p) if (*s++ != *p++) return 0; return 1; }

static void run(char *l)
{
    while (*l == ' ') l++;
    if (!*l) return;
    if (streq(l, "help")) puts_("help ver uptime mem mmu bench disk sector N ls cat mkdir write rm save echo clear color reboot halt\n");
    else if (streq(l, "ver")) puts_("AstraSageOS ARM64 port 0.3 (QEMU virt, aarch64)\n");
    else if (streq(l, "uptime")) { u64 s = (cnt() - 0) / frq(); puts_("up "); putu(s / 60); puts_(" min "); putu(s % 60); puts_(" s\n"); }
    else if (streq(l, "mem")) { puts_("kernel loaded at 0x40080000, framebuffer at "); puthex(FB_ADDR); puts_(fb_ok ? " (ramfb)\n" : " (not available)\n"); }
    else if (starts(l, "echo ")) { puts_(l + 5); putc_('\n'); }
    else if (streq(l, "clear")) { if (fb_ok) fb_clear(); }
    else if (streq(l, "color")) { fg = fg == 0xE0E0E0 ? 0x60FF90 : 0xE0E0E0; puts_("ok\n"); }
    else if (streq(l, "ls") || starts(l, "ls ")) {
        int d = l[2] ? fs_resolve(l + 3, 0) : 0;
        if (d < 0 || !fs_is_dir(d)) { puts_("ls: not a directory\n"); return; }
        for (int c = fs_child(d, 0); c > 0; c = fs_child(d, c)) { puts_(fs_name(c)); if (fs_is_dir(c)) putc_('/'); else { puts_("  "); putu(fs_size_real(c)); } putc_('\n'); }
    }
    else if (starts(l, "cat ")) {
        int n = fs_resolve(l + 4, 0);
        if (n < 0 || fs_is_dir(n)) { puts_("cat: no such file\n"); return; }
        if (fs_is_big(n)) { puts_("cat: big file\n"); return; }
        const char *d = fs_data(n); for (u32 i = 0; i < fs_size(n); i++) putc_(d[i]); putc_('\n');
    }
    else if (starts(l, "mkdir ")) { int r = fs_create(l + 6, 0, 1); if (r < 0) puts_(fs_strerror(r)), putc_('\n'); }
    else if (starts(l, "write ")) {            /* write FILE TEXT */
        char *name = l + 6, *txt = name; while (*txt && *txt != ' ') txt++;
        if (*txt) *txt++ = 0;
        int n = fs_resolve(name, 0); if (n < 0) n = fs_create(name, 0, 0);
        if (n < 0) { puts_(fs_strerror(n)); putc_('\n'); return; }
        int r = fs_write(n, txt, (u32)kstrlen(txt), 0); if (r < 0) { puts_(fs_strerror(r)); putc_('\n'); }
    }
    else if (starts(l, "rm ")) { int n = fs_resolve(l + 3, 0); if (n <= 0) puts_("rm: no such file\n"); else fs_delete(n); }
    else if (streq(l, "save")) { puts_(persist_save() == 0 ? "saved\n" : "save failed (no disk?)\n"); }
    else if (streq(l, "disk")) { if (!blk_ok) puts_("no virtio disk (add -drive file=disk.img,if=none,format=raw,id=d -device virtio-blk-device,drive=d)\n"); else { puts_("virtio-blk: "); putu(blk_sectors); puts_(" sectors ("); putu(blk_sectors / 2048); puts_(" MiB)\n"); } }
    else if (starts(l, "sector ")) {
        u64 n = 0; for (const char *p = l + 7; *p >= '0' && *p <= '9'; p++) n = n * 10 + (u64)(*p - '0');
        static u8 sec[512] __attribute__((aligned(16)));
        int rc = blk_rw(n, sec, 0);
        if (rc) { puts_("read failed ("); putu((u64)(-rc)); puts_(")\n"); }
        else for (int i = 0; i < 128; i++) { if (i % 16 == 0) { putc_('\n'); } putc_("0123456789abcdef"[sec[i] >> 4]); putc_("0123456789abcdef"[sec[i] & 15]); putc_(' '); }
        putc_('\n');
    }
    else if (streq(l, "mmu")) { if (mmu_on) puts_("mmu: already on\n"); else { puts_("mmu: switching on ...\n"); mmu_enable(); puts_("mmu: on (caches enabled)\n"); } }
    else if (streq(l, "bench")) {                  /* how fast is memory (the MMU/cache difference) */
        static u8 buf[1 << 20]; u64 t0 = cnt();
        for (int r = 0; r < 8; r++) for (u32 i = 0; i < sizeof buf; i += 8) *(volatile u64 *)(buf + i) = i;
        u64 dt = cnt() - t0; puts_("8 MiB written in "); putu(dt * 1000 / frq()); puts_(" ms (mmu "); puts_(mmu_on ? "on" : "off"); puts_(")\n");
    }
    else if (streq(l, "reboot")) psci(0x84000009);
    else if (streq(l, "halt")) psci(0x84000008);
    else { puts_("ast: command not found: "); puts_(l); puts_("\n"); }
}

void arm64_main(u64 dtb)
{
    (void)dtb;
    ramfb_init();
    vk_init();
    blk_init();
    fs_init();
    { int n = persist_init(); puts_(n >= 0 ? "file system: restored from disk\n" : "file system: RAM only\n"); }
    puts_("AstraSageOS ARM64 (QEMU virt)\n");
    puts_(fb_ok ? "screen: ramfb 1280x720\n" : "screen: none (serial only; add -device ramfb)\n");
    puts_(vkbd_ok ? "keyboard: virtio-input\n" : "keyboard: serial only (add -device virtio-keyboard-device)\n");
    puts_(blk_ok ? "disk: virtio-blk found (command: disk)\n" : "disk: none (add a virtio-blk drive)\n");
    puts_("type help\n\n");
    char line[128]; int n = 0;
    puts_("ast> ");
    for (;;) {
        int c = uart_getc();
        if (c < 0) c = vk_getc();
        if (c < 0) continue;
        if (c == '\r') c = '\n';
        if (c == 127) c = '\b';
        if (c == '\n') { putc_('\n'); line[n] = 0; run(line); n = 0; puts_("ast> "); }
        else if (c == '\b') { if (n) { n--; putc_('\b'); if (!fb_ok) { puts_(" \b"); } } }
        else if (c >= 32 && n < 126) { line[n++] = (char)c; putc_((char)c); }
    }
}
