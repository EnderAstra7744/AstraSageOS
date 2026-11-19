/*
 * Video setup. Two ways to get a linear framebuffer, in this order:
 *
 *  1. The boot loader hands one over in the Multiboot info (flag bit 12): GRUB does when the
 *     header asks for it (build with -DMB_VIDEO, see boot/boot.S). Address, size, pitch, depth and
 *     colour layout are taken from there, nothing is guessed.
 *  2. The loader gave none (QEMU's -kernel loader and our CD boot sector never do): the
 *     standard VGA of QEMU/Bochs (PCI 1234:1111) has the "Bochs VBE" (DISPI) registers, which
 *     switch to a linear 32-bit mode without a BIOS call. Its framebuffer address is read from
 *     PCI BAR0. This only exists on that emulated card - on other hardware there is no
 *     framebuffer and the text console stays.
 */
#include "gui.h"
#include "io.h"

#define VBE_INDEX 0x01CE
#define VBE_DATA  0x01CF
enum { VI_ID, VI_XRES, VI_YRES, VI_BPP, VI_ENABLE, VI_BANK, VI_VW, VI_VH, VI_XOFF, VI_YOFF };

struct mb_fb {
    u32 flags, mem_lower, mem_upper, boot_device, cmdline, mods_count, mods_addr, syms[4],
        mmap_length, mmap_addr, drives_length, drives_addr, config_table, boot_loader_name, apm_table;
    u32 vbe_control_info, vbe_mode_info; u16 vbe_mode, vbe_seg, vbe_off, vbe_len;
    u32 fb_addr_lo, fb_addr_hi, fb_pitch, fb_width, fb_height;
    u8  fb_bpp, fb_type, red_pos, red_size, green_pos, green_size, blue_pos, blue_size;
} __attribute__((packed));

static video_info vi;
static int have_gfx, used_dispi;
static const char *why = "not probed";
static const char *cmdline;

static void vbe_w(u16 idx, u16 v) { outw(VBE_INDEX, idx); outw(VBE_DATA, v); }
static u16  vbe_r(u16 idx)        { outw(VBE_INDEX, idx); return inw(VBE_DATA); }

static u32 pci_r(u32 dev, u32 off)
{
    outl(0xCF8, 0x80000000u | dev << 11 | (off & 0xFC));
    return inl(0xCFC);
}

static int from_multiboot(const struct mb_fb *m)
{
    if (!(m->flags & (1u << 12))) { why = "boot loader gave no framebuffer"; return 0; }
    if (m->fb_type != 1)          { why = "boot loader framebuffer is not direct RGB"; return 0; }
    if (m->fb_addr_hi || !m->fb_addr_lo) { why = "framebuffer address unusable"; return 0; }
    if (m->fb_bpp != 32 && m->fb_bpp != 24) { why = "framebuffer depth is not 24/32 bit"; return 0; }
    if (m->fb_width < 320 || m->fb_height < 200 || m->fb_width > GFX_MAXW || m->fb_height > GFX_MAXH) {
        why = "framebuffer size outside 320x200 .. 1920x1080"; return 0;
    }
    if (m->fb_pitch < m->fb_width * (m->fb_bpp / 8u)) { why = "framebuffer pitch smaller than a row"; return 0; }
    vi.addr = m->fb_addr_lo; vi.w = m->fb_width; vi.h = m->fb_height; vi.pitch = m->fb_pitch; vi.bpp = m->fb_bpp;
    vi.rpos = m->red_pos; vi.gpos = m->green_pos; vi.bpos = m->blue_pos;
    if (m->red_size != 8 || m->green_size != 8 || m->blue_size != 8) { why = "framebuffer colour layout is not 8-8-8"; return 0; }
    vi.how = "Multiboot framebuffer";
    return 1;
}

static int from_bochs(void)
{
    u32 dev, found = 0;
    for (dev = 0; dev < 32; dev++) {
        u32 id = pci_r(dev, 0);
        if (id == 0x11111234u) { found = 1; break; }
    }
    if (!found) { why = "no Bochs/QEMU standard VGA on PCI bus 0"; return 0; }
    u16 id = vbe_r(VI_ID);
    if ((id & 0xFFF0) != 0xB0C0) { why = "VBE (DISPI) interface did not answer"; return 0; }
    u32 bar = pci_r(dev, 0x10) & ~0xFu;
    if (!bar) { why = "VGA framebuffer BAR is empty"; return 0; }
    outl(0xCF8, 0x80000000u | dev << 11 | 4);                    /* make sure memory decoding is on */
    outl(0xCFC, (inl(0xCFC) & 0xFFFF) | 2);
    /* 1280x720 on purpose: the emulators on phones redraw every pixel in software, 1920x1080 is 2.25x slower */
    u16 modes[6][2] = { { 1280, 720 }, { 1024, 768 }, { 1280, 720 }, { 1024, 768 }, { 800, 600 }, { 640, 480 } };
    for (unsigned i = 0; i < sizeof modes / sizeof modes[0]; i++) {
        if (!modes[i][0]) continue;
        vbe_w(VI_ENABLE, 0);
        vbe_w(VI_XRES, modes[i][0]); vbe_w(VI_YRES, modes[i][1]); vbe_w(VI_BPP, 32);
        vbe_w(VI_ENABLE, 0x01 | 0x40);                           /* enabled + linear framebuffer */
        if (vbe_r(VI_XRES) == modes[i][0] && vbe_r(VI_YRES) == modes[i][1] && vbe_r(VI_BPP) == 32) {
            vi.addr = bar; vi.w = modes[i][0]; vi.h = modes[i][1]; vi.pitch = modes[i][0] * 4u; vi.bpp = 32;
            vi.rpos = 16; vi.gpos = 8; vi.bpos = 0;              /* B,G,R,X in memory */
            vi.how = "Bochs VBE (DISPI) on the standard VGA";
            used_dispi = 1;
            return 1;
        }
    }
    vbe_w(VI_ENABLE, 0);
    why = "VBE refused every mode (wallpaper size, 1920x1080 ... 640x480)";
    return 0;
}

int video_init(u32 magic, const void *mbi)
{
    have_gfx = 0;
    if (magic == 0x2BADB002u && mbi) {
        const struct mb_fb *m = mbi;
        if (m->flags & (1u << 2)) cmdline = (const char *)(size_t)m->cmdline;
        if (from_multiboot(m)) { have_gfx = 1; why = "ok"; return 1; }
    } else {
        why = "no Multiboot information";
    }
    if (from_bochs()) { have_gfx = 1; why = "ok"; return 1; }
    return 0;
}

const char *video_reason(void) { return why; }
const video_info *video_get(void) { return have_gfx ? &vi : 0; }

int video_cmdline_has(const char *word)
{
    if (!cmdline) return 0;
    size_t n = kstrlen(word);
    for (const char *p = cmdline; *p;) {
        while (*p == ' ') p++;
        const char *e = p;
        while (*e && *e != ' ') e++;
        if ((size_t)(e - p) == n && !kstrncmp(p, word, n)) return 1;
        p = e;
    }
    return 0;
}

void video_text_mode(void)
{
    if (used_dispi) vbe_w(VI_ENABLE, 0);
    have_gfx = 0;
}

/*
 * Boot progress marker for diagnosing a black screen: writes a letter to QEMU's debug port 0xE9
 * (run with -debugcon file:debug.log) and paints a small white square on the framebuffer.
 */
void video_stage(int n)
{
    outb(0xE9, (u8)('0' + n));
    if (!have_gfx || !vi.addr || vi.bpp != 32) return;
    for (int y = 4; y < 20; y++) {
        volatile u32 *row = (volatile u32 *)(size_t)(vi.addr + (u32)y * vi.pitch);
        for (int x = 4 + n * 20; x < 20 + n * 20; x++) row[x] = 0xFFFFFFFFu;
    }
}
