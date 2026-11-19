/*
 * AstraSageOS ARM32 (ARMv7-A): the machine layer of the full kernel (QEMU "virt"). It gives the shared code (terminal, fs, gui, net, ASPI,
 * AstraScript ...) what the i386 kernel gets from arch/x86: serial, keyboard + mouse (virtio-input), screen (ramfb), disk
 * (virtio-blk), network (virtio-net), clock (PL031), power (PSCI), memory size (device tree) and the MMU.
 * Derived from arch/arm64/hw.c (the same virtio / ramfb / PL011 / PL031 devices). Written without an ARM machine at hand: only compile-checked.
 */
#include "../../kernel/astra.h"
#include "../../gui/gui.h"
typedef unsigned long long u64;

#define UART0   0x09000000UL
#define RTC0    0x09010000UL
#define FWCFG   0x09020000UL
#define VIRTIO  0x0a000000UL
#define FB_W 1280
#define FB_H 720

static inline void w32(u64 a, u32 v) { *(volatile u32 *)a = v; }
static inline u32  r32(u64 a)        { return *(volatile u32 *)a; }
static inline void dmb(void)         { __asm__ volatile("dmb" ::: "memory"); }
static inline u32  bswap32(u32 v)    { return __builtin_bswap32(v); }
static inline u64  bswap64(u64 v)    { return __builtin_bswap64(v); }
#define R32(x) ((u32)(x))
extern char _end[];

/* ---------------------------------------------------------------- MMU: identity map, 1 MiB sections (0-1 GiB device, 1-2 GiB normal write-back, rest unmapped) */
static u32 l1[4096] __attribute__((aligned(16384)));
void hw_mmu_enable(void)
{
    for (u32 i = 0; i < 4096; i++) {
        u32 base = i << 20;
        if (i < 1024)      l1[i] = base | 0xC00 | 0x2 | 0x4;                    /* device: B, full access */
        else if (i < 2048) l1[i] = base | 0xC00 | 0x1000 | 0xC | 0x2;          /* normal memory, write-back write-allocate */
        else               l1[i] = 0;
    }
    __asm__ volatile(
        "dsb\n"
        "mov r0, #0\n mcr p15, 0, r0, c2, c0, 2\n"                            /* TTBCR = 0 */
        "mcr p15, 0, %0, c2, c0, 0\n"                                         /* TTBR0 */
        "mov r0, #1\n mcr p15, 0, r0, c3, c0, 0\n"                           /* DACR: domain 0 = client */
        "mov r0, #0\n mcr p15, 0, r0, c8, c7, 0\n mcr p15, 0, r0, c7, c5, 0\n mcr p15, 0, r0, c7, c5, 6\n dsb\n isb\n"   /* TLB, I-cache, branch predictor */
        "mrc p15, 0, r0, c1, c0, 0\n"
        "orr r0, r0, #1\n orr r0, r0, #4\n orr r0, r0, #0x1000\n"           /* M, C, I */
        "bic r0, r0, #2\n"                                                    /* no alignment fault */
        "mcr p15, 0, r0, c1, c0, 0\n isb\n"
        :: "r"(l1) : "r0", "memory");
}

/* ---------------------------------------------------------------- serial (PL011) */
void serial_init(void) {}
void serial_putc(char c) { while (r32(UART0 + 0x18) & (1u << 5)) { } w32(UART0, (u32)(u8)c); }
int  serial_getb_nb(void) { if (r32(UART0 + 0x18) & (1u << 4)) return -1; return (int)(r32(UART0) & 0xFF); }
int  serial_pending(void) { return !(r32(UART0 + 0x18) & (1u << 4)); }
int  serial_terminal_present(void) { return 0; }

/* ---------------------------------------------------------------- device tree: memory size */
static u32 ram_mib = 256;
static u64 ram_base = 0x40000000ULL;
void hw_parse_dtb(u32 dtb)
{
    const u8 *b = (const u8 *)dtb;
    if (!dtb || bswap32(*(const u32 *)b) != 0xd00dfeedu) return;
    u32 off_struct = bswap32(*(const u32 *)(b + 8)), off_str = bswap32(*(const u32 *)(b + 12));
    const u8 *p = b + off_struct;
    int in_mem = 0, depth = 0, mem_depth = 0;
    for (;;) {
        u32 tok = bswap32(*(const u32 *)p); p += 4;
        if (tok == 1) {
            const char *name = (const char *)p; u32 n = 0; while (name[n]) n++;
            p += (n + 1 + 3) & ~3u; depth++;
            if (name[0] == 'm' && name[1] == 'e' && name[2] == 'm' && name[3] == 'o' && name[4] == 'r' && name[5] == 'y') { in_mem = 1; mem_depth = depth; }
        } else if (tok == 2) { if (in_mem && depth == mem_depth) in_mem = 0; depth--; }
        else if (tok == 3) {
            u32 len = bswap32(*(const u32 *)p), nameoff = bswap32(*(const u32 *)(p + 4)); p += 8;
            const char *pn = (const char *)(b + off_str + nameoff);
            if (in_mem && pn[0] == 'r' && pn[1] == 'e' && pn[2] == 'g' && !pn[3] && len >= 16) {     /* #address-cells = #size-cells = 2 on virt */
                ram_base = ((u64)bswap32(*(const u32 *)p) << 32) | bswap32(*(const u32 *)(p + 4));
                u64 sz = ((u64)bswap32(*(const u32 *)(p + 8)) << 32) | bswap32(*(const u32 *)(p + 12));
                if (sz >= (16ULL << 20)) ram_mib = (u32)(sz >> 20);
            }
            p += (len + 3) & ~3u;
        } else if (tok == 4) { /* nop */ }
        else break;
    }
}

/* ---------------------------------------------------------------- fw_cfg + ramfb */
static void fw_dma(u32 control, void *buf, u32 len)
{
    static volatile struct { u32 control, length; u64 address; } __attribute__((aligned(16))) d;
    d.control = bswap32(control); d.length = bswap32(len); d.address = bswap64((u64)(size_t)buf);
    dmb();
    { u64 da = bswap64((u64)(size_t)&d); w32(FWCFG + 16, (u32)da); w32(FWCFG + 20, (u32)(da >> 32)); }    /* high half first; the low half starts the transfer */
    while (bswap32(d.control) & ~1u) { }
}
#define FW_SELECT (1u << 3)
#define FW_READ   (1u << 1)
#define FW_WRITE  (1u << 4)
static int fw_find(const char *name, u32 *sel)
{
    u32 n; fw_dma((0x19u << 16) | FW_SELECT | FW_READ, &n, 4); n = bswap32(n);
    for (u32 i = 0; i < n && i < 64; i++) {
        struct { u32 size; u16 select, res; char name[56]; } e;
        fw_dma(FW_READ, &e, sizeof e);
        int same = 1;
        for (int k = 0; name[k] || e.name[k]; k++) if (name[k] != e.name[k]) { same = 0; break; }
        if (same) { *sel = (u32)__builtin_bswap16(e.select); return 1; }
    }
    return 0;
}

#include "../arm_pci.inc"
static video_info vi;
static const char *vreason = "no ramfb (add -device ramfb)";
static int video_ramfb(void)
{
    u32 sig = 0, sel;
    fw_dma((0u << 16) | FW_SELECT | FW_READ, &sig, 4);
    if (sig != 0x554D4551u) return 0;
    if (!fw_find("etc/ramfb", &sel)) return 0;
    u32 fb = (u32)(ram_base + ((u64)ram_mib << 20) - (16UL << 20));          /* the last 16 MiB of RAM */
    struct { u64 addr; u32 fourcc, flags, w, h, stride; } __attribute__((packed)) cfg;
    cfg.addr = bswap64(fb); cfg.fourcc = bswap32(0x34325258u); cfg.flags = 0;
    cfg.w = bswap32(FB_W); cfg.h = bswap32(FB_H); cfg.stride = bswap32(FB_W * 4);
    fw_dma((sel << 16) | FW_SELECT | FW_WRITE, &cfg, sizeof cfg);
    vi.addr = fb; vi.w = FB_W; vi.h = FB_H; vi.pitch = FB_W * 4; vi.bpp = 32; vi.rpos = 16; vi.gpos = 8; vi.bpos = 0; vi.how = "ramfb (fw_cfg)";
    return 1;
}
int video_init(u32 magic, const void *mbi)
{
    (void)magic; (void)mbi;
    if (video_ramfb()) return 1;
    u32 fb, regs;
    if (pci_vga_find(&fb, &regs) && pci_vga_set(regs, FB_W, FB_H)) {                 /* Limbo: "Video Card: std" */
        vi.addr = fb; vi.w = FB_W; vi.h = FB_H; vi.pitch = FB_W * 4; vi.bpp = 32; vi.rpos = 16; vi.gpos = 8; vi.bpos = 0; vi.how = "PCI VGA (std)";
        return 1;
    }
    vreason = pci_n > 0 ? "no ramfb and no std VGA among the PCI devices listed above (virtio-gpu-pci is not supported yet)" : "no ramfb (add -device ramfb) and no PCI VGA";
    return 0;
}
void hw_pci_report(void) { pci_report(); }
const char *video_reason(void) { return vreason; }
int  video_cmdline_has(const char *word) { (void)word; return 1; }
void video_text_mode(void) {}
void video_stage(int n) { (void)n; }
const video_info *video_get(void) { return vi.addr ? &vi : 0; }

/* ---------------------------------------------------------------- virtio-mmio: queues */
#define QN 16
typedef struct { u64 base; u32 ver; u8 *q; } vdev;

/* common start: reset, ACK, DRIVER, features (legacy: low word `lf`; modern: VERSION_1 + `lf`); 0 on failure */
static int vdev_open(vdev *d, u64 base, u32 lf)
{
    d->base = base; d->ver = r32(base + 4);
    w32(base + 0x70, 0); w32(base + 0x70, 1); w32(base + 0x70, 1 | 2);
    u32 offered = (w32(base + 0x14, 0), r32(base + 0x10));
    if (d->ver >= 2) {
        w32(base + 0x24, 0); w32(base + 0x20, lf & offered);
        w32(base + 0x24, 1); w32(base + 0x20, 1);                         /* VERSION_1 */
        w32(base + 0x70, 1 | 2 | 8);
        if (!(r32(base + 0x70) & 8)) return 0;
    } else { w32(base + 0x20, lf & offered); w32(base + 0x28, 4096); }
    return 1;
}
/* queue `idx` with n entries in memory q (desc @0, avail @256, used @4096) */
static int vq_open(vdev *d, int idx, u8 *q, u32 n)
{
    u64 b = d->base;
    w32(b + 0x30, (u32)idx);
    if (r32(b + 0x34) < n) return 0;
    w32(b + 0x38, n);
    u64 desc = (u64)q, avail = (u64)q + 256, used = (u64)q + 4096;
    if (d->ver >= 2) {
        w32(b + 0x80, (u32)desc); w32(b + 0x84, (u32)(desc >> 32));
        w32(b + 0x90, (u32)avail); w32(b + 0x94, (u32)(avail >> 32));
        w32(b + 0xa0, (u32)used); w32(b + 0xa4, (u32)(used >> 32));
        w32(b + 0x44, 1);
    } else { w32(b + 0x3c, 4096); w32(b + 0x40, (u32)((u64)q >> 12)); }
    return 1;
}
static void vdev_go(vdev *d) { w32(d->base + 0x70, d->ver >= 2 ? (1 | 2 | 8 | 4) : (1 | 2 | 4)); }

static void put_desc(u8 *q, int i, u64 addr, u32 len, u16 flags, u16 next)
{
    u64 *d = (u64 *)(q + i * 16);
    d[0] = addr; ((u32 *)d)[2] = len; ((u16 *)d)[6] = flags; ((u16 *)d)[7] = next;
}

/* ---------------------------------------------------------------- virtio-input: keyboard + pointer */
#define MAXIN 4
static struct { vdev d; u8 q[8192] __attribute__((aligned(4096))); struct { u16 type, code; u32 value; } ev[QN]; u16 last; } in_dev[MAXIN];
static int in_n;

/* key queue */
#define KQ 64
static int kq[KQ], kq_h, kq_t;
static void kpush(int k) { int n = (kq_t + 1) % KQ; if (n != kq_h) { kq[kq_t] = k; kq_t = n; } }

/* pointer state */
static int mx, my, bw = FB_W, bh = FB_H, wheel_acc, have_ptr, moved; static u8 btn;
#define QMAX 64
static mouse_ev mq[QMAX]; static int mq_h, mq_t;
static void mpush(void)
{
    int n = (mq_t + 1) % QMAX;
    if (n == mq_h) { int last = (mq_t + QMAX - 1) % QMAX; if (mq[last].buttons == btn) { mq[last].x = mx; mq[last].y = my; } return; }
    mq[mq_t].x = mx; mq[mq_t].y = my; mq[mq_t].buttons = btn; mq_t = n;
}

static const char kmap[58]  = { 0,27,'1','2','3','4','5','6','7','8','9','0','-','=','\b','\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' ' };
static const char kmapS[58] = { 0,27,'!','@','#','$','%','^','&','*','(',')','_','+','\b','\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,'A','S','D','F','G','H','J','K','L',':','"','~',0,'|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' ' };
static int k_shift, k_ctrl, k_alt, k_caps;

static void key_event(u16 code, u32 val)
{
    if (code == 42 || code == 54) { k_shift = val != 0; return; }
    if (code == 29 || code == 97) { k_ctrl = val != 0; return; }
    if (code == 56 || code == 100) { k_alt = val != 0; return; }
    if (code == 58) { if (val == 1) k_caps = !k_caps; return; }
    if (val == 0) return;                                            /* release; 1 = press, 2 = repeat */
    int k = -1;
    switch (code) {
    case 103: k = k_alt ? KEY_ALT_UP : KEY_UP; break;
    case 108: k = k_alt ? KEY_ALT_DOWN : KEY_DOWN; break;
    case 105: k = k_alt ? KEY_ALT_LEFT : KEY_LEFT; break;
    case 106: k = k_alt ? KEY_ALT_RIGHT : KEY_RIGHT; break;
    case 102: k = KEY_HOME; break;
    case 107: k = KEY_END; break;
    case 104: k = KEY_PGUP; break;
    case 109: k = KEY_PGDN; break;
    case 111: k = KEY_DEL; break;
    default: break;
    }
    if (k < 0) {
        if (code >= 58) return;
        if (k_alt && code == 15) { kpush(KEY_ALT_TAB); return; }
        char c = kmap[code];
        if (!c) return;
        int alpha = c >= 'a' && c <= 'z';
        if (k_ctrl && k_alt && alpha) k = KEY_HOT + (c - 'a');
        else if (k_ctrl && alpha) k = c - 'a' + 1;
        else { int up = alpha ? (k_shift ^ k_caps) : k_shift; k = (u8)(up ? kmapS[code] : c); }
    }
    kpush(k);
}

static void pointer_event(u16 type, u16 code, u32 value)
{
    int sv = (int)value;
    if (type == 3 && code == 0) { mx = (int)((u64)value * (u64)bw >> 15); if (mx >= bw) mx = bw - 1; have_ptr = 1; }       /* EV_ABS X */
    else if (type == 3 && code == 1) { my = (int)((u64)value * (u64)bh >> 15); if (my >= bh) my = bh - 1; have_ptr = 1; }  /* EV_ABS Y */
    else if (type == 2 && code == 0) { mx += sv; have_ptr = 1; }                                                              /* EV_REL X */
    else if (type == 2 && code == 1) { my += sv; have_ptr = 1; }
    else if (type == 2 && code == 8) { wheel_acc -= sv; return; }                                                              /* wheel */
    else if (type == 1 && (code == 0x110 || code == 0x111 || code == 0x112)) {
        u8 bit = code == 0x110 ? 1 : code == 0x111 ? 2 : 4;
        btn = value ? (u8)(btn | bit) : (u8)(btn & ~bit); have_ptr = 1;
        mpush(); return;
    }
    else return;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > bw - 1) mx = bw - 1;
    if (my > bh - 1) my = bh - 1;
    moved = 1;
}

static void in_pump(void)
{
    for (int i = 0; i < in_n; i++) {
        u16 *used = (u16 *)(in_dev[i].q + 4096);
        dmb();
        while (in_dev[i].last != used[1]) {
            u32 id = ((u32 *)(used + 2))[(in_dev[i].last % QN) * 2] % QN;
            in_dev[i].last++;
            u16 type = in_dev[i].ev[id].type, code = in_dev[i].ev[id].code; u32 val = in_dev[i].ev[id].value;
            u16 *avail = (u16 *)(in_dev[i].q + 256);
            avail[2 + (avail[1] % QN)] = (u16)id; dmb(); avail[1]++; dmb();
            w32(in_dev[i].d.base + 0x50, 0);
            if (type == 1 && code < 0x100) key_event(code, val);
            else if (type == 0) { if (moved) { mpush(); moved = 0; } }          /* EV_SYN: the pointer moved in this report */
            else pointer_event(type, code, val);
        }
    }
}
int hw_input_pending(void) { in_pump(); return kq_h != kq_t; }
int hw_input_poll(void)
{
    in_pump();
    if (kq_h == kq_t) return -1;
    int k = kq[kq_h]; kq_h = (kq_h + 1) % KQ;
    return k;
}

static void input_init(void)
{
    for (int s = 0; s < 32 && in_n < MAXIN; s++) {
        u64 b = VIRTIO + (u64)s * 0x200;
        if (r32(b) != 0x74726976u || r32(b + 8) != 18) continue;
        int i = in_n;
        if (!vdev_open(&in_dev[i].d, b, 0)) continue;
        w32(b + 0x30, 0);
        if (!vq_open(&in_dev[i].d, 0, in_dev[i].q, QN)) continue;
        u8 *q = in_dev[i].q;
        for (int k = 0; k < QN; k++) { put_desc(q, k, (u64)&in_dev[i].ev[k], 8, 2, 0); ((u16 *)(q + 256 + 4))[k] = (u16)k; }
        ((u16 *)(q + 256))[0] = 0; ((u16 *)(q + 256))[1] = QN;
        dmb();
        vdev_go(&in_dev[i].d);
        w32(b + 0x50, 0);
        in_dev[i].last = 0; in_n++;
    }
}

/* mouse API (arch/x86/drivers/mouse.c for PS/2 has the same interface) */
int  mouse_init(void) { input_init(); return in_n > 0; }
void mouse_set_bounds(int w, int h) { bw = w; bh = h; if (mx >= w || mx == 0) mx = w / 2; if (my >= h || my == 0) my = h / 2; }
void mouse_feed(u8 b) { (void)b; }
int  mouse_pop(mouse_ev *ev) { in_pump(); if (mq_h == mq_t) return 0; *ev = mq[mq_h]; mq_h = (mq_h + 1) % QMAX; return 1; }
void mouse_position(int *x, int *y) { *x = mx; *y = my; }
int  mouse_wheel_take(void) { in_pump(); int z = wheel_acc; wheel_acc = 0; return z; }
int  mouse_present(void) { return have_ptr || in_n > 0; }

/* ---------------------------------------------------------------- virtio-blk (the disk interface kernel/astra.h calls ata_*) */
#define BQN 16                      /* legacy virtio puts the avail ring right behind the descriptors: 16 x 16 bytes = offset 256, as the code assumes */
#define MAXBLK 2                    /* disk 0 = the system disk, disk 1 = the ASPI repository disk (a second -device virtio-blk-device) */
static struct { vdev d; u8 q[8192] __attribute__((aligned(4096))); u64 sectors; int ok; u16 idx; } blk[MAXBLK];
static struct { u32 type, reserved; u64 sector; } __attribute__((aligned(16))) blk_hdr;
static volatile u8 blk_status;
static u8 blk_tmp[512] __attribute__((aligned(16)));

static void blk_init(void)
{
    int n = 0;
    for (int s = 0; s < 32 && n < MAXBLK; s++) {
        u64 b = VIRTIO + (u64)s * 0x200;
        if (r32(b) != 0x74726976u || r32(b + 8) != 2) continue;
        if (!vdev_open(&blk[n].d, b, 0)) continue;
        if (!vq_open(&blk[n].d, 0, blk[n].q, BQN)) continue;
        vdev_go(&blk[n].d);
        blk[n].sectors = r32(b + 0x100) | ((u64)r32(b + 0x104) << 32);
        blk[n].ok = 1; n++;
    }
}
static int blk_rw(int n, u64 sector, void *buf, int write)
{
    if (n < 0 || n >= MAXBLK || !blk[n].ok || sector >= blk[n].sectors) return -1;
    u8 *q = blk[n].q;
    blk_hdr.type = write ? 1 : 0; blk_hdr.reserved = 0; blk_hdr.sector = sector; blk_status = 0xFF;
    put_desc(q, 0, (u64)(size_t)&blk_hdr, 16, 1, 1);
    put_desc(q, 1, (u64)(size_t)buf, 512, (u16)(1 | (write ? 0 : 2)), 2);
    put_desc(q, 2, (u64)(size_t)&blk_status, 1, 2, 0);
    u16 *avail = (u16 *)(q + 256), *used = (u16 *)(q + 4096);
    u16 want = (u16)(used[1] + 1);
    avail[2 + (blk[n].idx % BQN)] = 0; blk[n].idx++;
    dmb(); avail[1] = blk[n].idx; dmb();
    w32(blk[n].d.base + 0x50, 0);
    for (u64 t = 0; used[1] != want; t++) { dmb(); if (t > 400000000UL) return -2; }
    return blk_status == 0 ? 0 : -3;
}
static u32 blk_secs(int n) { return n < MAXBLK && blk[n].sectors > 0xFFFFFFFFUL ? 0xFFFFFFFFu : (n < MAXBLK ? (u32)blk[n].sectors : 0); }
int  ata_init(void) { return blk[0].ok; }
u32  ata_sectors(void) { return blk_secs(0); }
int  ata_read(u32 lba, void *b) { if (blk_rw(0, lba, blk_tmp, 0)) return -1; kmemcpy(b, blk_tmp, 512); return 0; }
int  ata_write(u32 lba, const void *b) { kmemcpy(blk_tmp, b, 512); return blk_rw(0, lba, blk_tmp, 1) ? -1 : 0; }
int  ata_init_drive(int d) { return d >= 0 && d < MAXBLK && blk[d].ok; }
u32  ata_sectors_drive(int d) { return blk_secs(d); }
int  ata_read_drive(int d, u32 lba, void *buf512) { if (blk_rw(d, lba, blk_tmp, 0)) return -1; kmemcpy(buf512, blk_tmp, 512); return 0; }

/* ---------------------------------------------------------------- virtio-net */
#define NQN 16
#define NBUF 2048
static vdev net; static int net_ok, net_hdr_len;
static u8 net_rxq[8192] __attribute__((aligned(4096))), net_txq[8192] __attribute__((aligned(4096)));
static u8 net_rxbuf[NQN][NBUF] __attribute__((aligned(16))), net_txbuf[NBUF] __attribute__((aligned(16)));
static u16 net_rx_last, net_tx_idx; static u8 net_mac_[6];
static const char *net_why = "no virtio-net card (add -device virtio-net-device,netdev=n -netdev user,id=n)";

int nic_init(void)
{
    if (net_ok) return 1;
    for (int s = 0; s < 32; s++) {
        u64 b = VIRTIO + (u64)s * 0x200;
        if (r32(b) != 0x74726976u || r32(b + 8) != 1) continue;
        if (!vdev_open(&net, b, 1u << 5)) { net_why = "virtio-net: features refused"; continue; }       /* VIRTIO_NET_F_MAC */
        net_hdr_len = net.ver >= 2 ? 12 : 10;
        if (!vq_open(&net, 0, net_rxq, NQN) || !vq_open(&net, 1, net_txq, NQN)) { net_why = "virtio-net: queue setup failed"; continue; }
        for (int k = 0; k < 6; k++) net_mac_[k] = *(volatile u8 *)(b + 0x100 + k);
        for (int k = 0; k < NQN; k++) { put_desc(net_rxq, k, (u64)net_rxbuf[k], NBUF, 2, 0); ((u16 *)(net_rxq + 256 + 4))[k] = (u16)k; }
        ((u16 *)(net_rxq + 256))[0] = 0; ((u16 *)(net_rxq + 256))[1] = NQN;
        dmb();
        vdev_go(&net);
        w32(b + 0x50, 0);
        net_ok = 1; return 1;
    }
    return 0;
}
const char *nic_name(void) { return net_ok ? "virtio-net (QEMU)" : net_why; }
const u8 *nic_mac(void) { return net_mac_; }
int nic_ready(void) { return net_ok; }
int nic_link(void) { return net_ok; }
int nic_is_rtl(void) { return 0; }
int nic_send(const void *data, u32 len)
{
    if (!net_ok || len > NBUF - 16) return -1;
    kmemset(net_txbuf, 0, (u32)net_hdr_len);
    kmemcpy(net_txbuf + net_hdr_len, data, len);
    put_desc(net_txq, 0, (u64)net_txbuf, len + (u32)net_hdr_len, 0, 0);
    u16 *avail = (u16 *)(net_txq + 256), *used = (u16 *)(net_txq + 4096);
    u16 want = (u16)(used[1] + 1);
    avail[2 + (net_tx_idx % NQN)] = 0; net_tx_idx++;
    dmb(); avail[1] = net_tx_idx; dmb();
    w32(net.base + 0x50, 1);
    for (u64 t = 0; used[1] != want; t++) { dmb(); if (t > 100000000UL) return -1; }
    return (int)len;
}
int nic_recv(void *buf, u32 cap)
{
    if (!net_ok) return 0;
    u16 *used = (u16 *)(net_rxq + 4096);
    dmb();
    if (net_rx_last == used[1]) return 0;
    u32 id = ((u32 *)(used + 2))[(net_rx_last % NQN) * 2] % NQN, tot = ((u32 *)(used + 2))[(net_rx_last % NQN) * 2 + 1];
    net_rx_last++;
    int n = tot > (u32)net_hdr_len ? (int)(tot - (u32)net_hdr_len) : 0;
    if ((u32)n > cap) n = (int)cap;
    if (n) kmemcpy(buf, net_rxbuf[id] + net_hdr_len, (u32)n);
    u16 *avail = (u16 *)(net_rxq + 256);
    avail[2 + (avail[1] % NQN)] = (u16)id; dmb(); avail[1]++; dmb();
    w32(net.base + 0x50, 0);
    return n;
}
int rtl_probe_dummy;

/* ---------------------------------------------------------------- machine info */
static u64 t_boot;
static u64 cnt(void) { u32 lo, hi; __asm__ volatile("isb\n mrrc p15, 0, %0, %1, c14" : "=r"(lo), "=r"(hi)); return ((u64)hi << 32) | lo; }
static u32 frq(void) { u32 v; __asm__ volatile("mrc p15, 0, %0, c14, c0, 0" : "=r"(v)); return v ? v : 62500000u; }
static void psci(u32 fn) { register u32 r0 __asm__("r0") = fn; __asm__ volatile(".arch_extension virt\n hvc #0" : "+r"(r0) :: "memory"); }

void hal_datetime(hal_time *t) { ktime_unpack(r32(RTC0) - 946684800u, t); }
u32  hal_uptime_sec(void) { return (u32)((cnt() - t_boot) / frq()); }
u32  hal_mem_total_mib(void) { return ram_mib; }
u32  hal_mem_used_mib(void) { return (u32)(((u64)(size_t)_end - ram_base + (1UL << 20) - 1) >> 20); }
const char *hal_cpu_name(void) { return "ARMv7-A (ARM32, QEMU virt)"; }
u32  hal_cpu_cores(void) { return 1; }
int  hal_cpu_long_mode(void) { return 0; }
const char *hal_gpu_name(void) { return vi.addr && vi.how[0] == 'P' ? "PCI VGA (QEMU std)" : "ramfb (QEMU)"; }
const char *hal_model(void) { return "QEMU virt (arm)"; }
u32  hal_ticks(void) { u64 v = cnt(); return (u32)v ^ (u32)(v >> 32); }
int  hal_term_size(u32 *c, u32 *r) { (void)c; (void)r; return 0; }
void hal_shutdown(void) { psci(0x84000008); for (;;) __asm__ volatile("wfe"); }
void hal_reboot(void)   { psci(0x84000009); for (;;) __asm__ volatile("wfe"); }

/* ---------------------------------------------------------------- what the x86-only parts would have provided */
void err_idt_init(void) {}
/* native programs: kernel/native.c; they may use RAM from NATIVE_BASE up to the frame buffer */
u32 native_ram_top(void) { return (u32)(ram_base + ((u64)ram_mib << 20) - (18UL << 20)); }

void arm32_exception(u32 type, u32 lr, u32 fsr, u32 far)
{
    static const char *const names[] = { "reset", "undefined instruction", "supervisor call", "prefetch abort", "data abort", "reserved", "IRQ", "FIQ" };
    static char msg[160];
    ksnprintf(msg, sizeof msg, "CPU exception %u (%s) at %x, status %x, address %x", type, names[type & 7], lr, fsr, far);
    err_kernel_panic(msg);
}

/* the UI clock: the x86 build reads the PIT, here the generic timer */
static u32 ms_clock(void) { u32 per_ms = frq() / 1000u; if (!per_ms) per_ms = 1; return (u32)((cnt() - t_boot) / per_ms); }

/* ---------------------------------------------------------------- entry */
void hw_early(u32 dtb)
{
    hw_parse_dtb(dtb);
    t_boot = cnt();
    ui_ms_hook = ms_clock;
    blk_init();
}

/* ---------------------------------------------------------------- not available on this machine yet */
double sqrt(double x) { double r; __asm__("vsqrt.f64 %P0, %P1" : "=w"(r) : "w"(x)); return r; }
#include "../arm_snd.inc"
int  pci_wireless_scan(int kind, char *first, u32 cap) { (void)kind; if (cap) first[0] = 0; return 0; }
