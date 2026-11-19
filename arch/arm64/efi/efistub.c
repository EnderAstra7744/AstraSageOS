/* AstraSageOS ARM64 UEFI loader: EFI/BOOT/BOOTAA64.EFI on the CD's EFI system partition.
 * UEFI (edk2 on QEMU "virt", or real firmware) starts this program; it
 *   1. reads \AS_ARM64.BIN (the flat kernel image, linked at 0x40080000) from the same volume,
 *   2. reserves RAM for it at that address and copies it there,
 *   3. finds the device tree in the EFI configuration table,
 *   4. leaves boot services and jumps to the kernel with x0 = device tree (the Linux arm64 boot protocol).
 * Freestanding: no EFI headers, only the pieces of the UEFI structures that are used.
 * Never run on a real UEFI here (no emulator in the build sandbox): see arch/README.md. */
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
typedef __UINTPTR_TYPE__ UINTN;   /* 64 bit on ARM64, 32 bit on ARM32 */
typedef void *H;

#if defined(__aarch64__)
#define KERNEL_BASE   0x40080000ULL
#define KERNEL_SPACE  (80ULL << 20)           /* image + bss + stack of the kernel (its _end is about 75 MiB above the base) */
#define KERNEL_FILE   u"AS_ARM64.BIN"
#else
#define KERNEL_BASE   0x40010000ULL
#define KERNEL_SPACE  (80ULL << 20)
#define KERNEL_FILE   u"AS_ARM32.BIN"
#endif

typedef struct { u32 a; u16 b, c; u8 d[8]; } GUID;
typedef struct { u64 sig; u32 rev, hsize, crc, res; } HDR;
typedef struct { void *reset; UINTN (*out)(void *, const u16 *); } CONOUT;
typedef struct { GUID g; void *p; } CFG;
typedef struct {
    HDR h; void *vendor; u32 frev; H cih; void *cin; H coh; CONOUT *conout; H seh; void *stderr_; void *rt; void *bs;
    UINTN ncfg; CFG *cfg;
} SYSTAB;
typedef struct {
    HDR h; void *raise, *restore;
    UINTN (*alloc_pages)(u32 type, u32 mem, UINTN pages, u64 *addr);
    void *free_pages;
    UINTN (*get_map)(UINTN *size, void *map, UINTN *key, UINTN *dsize, u32 *dver);
    void *alloc_pool, *free_pool, *create_event, *set_timer, *wait_event, *signal_event, *close_event, *check_event;
    void *inst_proto, *reinst_proto, *uninst_proto;
    UINTN (*handle_protocol)(H h, const GUID *g, void **out);
    void *reserved, *reg_notify, *locate_handle, *locate_path, *inst_cfg, *load_image, *start_image, *exit, *unload_image;
    UINTN (*exit_bs)(H image, UINTN key);
} BOOTSV;
typedef struct { u32 rev; H parent; SYSTAB *st; H device; } LOADED;
typedef struct EFILE EFILE;
struct EFILE {
    u64 rev;
    UINTN (*open)(EFILE *f, EFILE **out, const u16 *name, u64 mode, u64 attr);
    UINTN (*close)(EFILE *f);
    void *del;
    UINTN (*read)(EFILE *f, UINTN *size, void *buf);
    void *write;
    UINTN (*getpos)(EFILE *f, u64 *pos);
    UINTN (*setpos)(EFILE *f, u64 pos);
};
typedef struct { u64 rev; UINTN (*openvol)(void *self, EFILE **root); } SFS;

static const GUID G_LOADED = { 0x5B1B31A1, 0x9562, 0x11d2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
static const GUID G_SFS    = { 0x964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };
static const GUID G_DTB    = { 0xb1b621d5, 0xf19c, 0x41a5, { 0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0 } };

static SYSTAB *ST;
static void say(const u16 *s) { if (ST && ST->conout) ST->conout->out(ST->conout, s); }
static int same(const GUID *a, const GUID *b)
{
    const u8 *x = (const u8 *)a, *y = (const u8 *)b;
    for (int i = 0; i < 16; i++) if (x[i] != y[i]) return 0;
    return 1;
}
static u8 mapbuf[65536] __attribute__((aligned(16)));

static void clean_range(u64 a, u64 n)
{
    u64 end = a + n;
#if defined(__aarch64__)
    for (a &= ~63ULL; a < end; a += 64) __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
    __asm__ volatile("dsb sy\n ic iallu\n dsb sy\n isb" ::: "memory");
#else
    for (a &= ~31ULL; a < end; a += 32) __asm__ volatile("mcr p15, 0, %0, c7, c14, 1" :: "r"((u32)a) : "memory");
    __asm__ volatile("dsb\n mcr p15, 0, %0, c7, c5, 0\n dsb\n isb" :: "r"(0) : "memory");
#endif
}

UINTN efi_main(H image, SYSTAB *st)
{
    ST = st;
    BOOTSV *bs = (BOOTSV *)st->bs;
    say(u"AstraSageOS ARM64 UEFI loader\r\n");

    LOADED *li = 0;
    if (bs->handle_protocol(image, &G_LOADED, (void **)&li) || !li) { say(u"no loaded-image protocol\r\n"); return 1; }
    SFS *fs = 0;
    if (bs->handle_protocol(li->device, &G_SFS, (void **)&fs) || !fs) { say(u"no file system on the boot device\r\n"); return 1; }
    EFILE *root = 0, *f = 0;
    if (fs->openvol(fs, &root) || !root) { say(u"cannot open the volume\r\n"); return 1; }
    if (root->open(root, &f, KERNEL_FILE, 1 /* read */, 0) || !f) { say(u"kernel file not found\r\n"); return 1; }
    u64 size = 0;
    f->setpos(f, ~0ULL); f->getpos(f, &size); f->setpos(f, 0);
    if (!size || size > KERNEL_SPACE) { say(u"bad kernel size\r\n"); return 1; }

    u64 addr = KERNEL_BASE;
    if (bs->alloc_pages(2 /* AllocateAddress */, 2 /* LoaderData */, KERNEL_SPACE >> 12, &addr)) {
        say(u"RAM at 0x40080000 is not free: cannot place the kernel there\r\n");
        return 1;
    }
    UINTN n = size;
    if (f->read(f, &n, (void *)KERNEL_BASE) || n != size) { say(u"read error\r\n"); return 1; }
    f->close(f);
    say(u"kernel loaded, starting...\r\n");

    u64 dtb = 0;  /* firmware pointer, widened */
    for (UINTN i = 0; i < st->ncfg; i++)
        if (same(&st->cfg[i].g, &G_DTB)) { dtb = (u64)(UINTN)st->cfg[i].p; break; }

    UINTN msize, key, dsize; u32 dver;
    UINTN rc;
    for (int tries = 0; tries < 4; tries++) {
        msize = sizeof mapbuf;
        rc = bs->get_map(&msize, mapbuf, &key, &dsize, &dver);
        if (rc) { say(u"memory map failed\r\n"); return 1; }
        rc = bs->exit_bs(image, key);
        if (!rc) break;
    }
    if (rc) return 1;

    /* the kernel starts with the MMU off at EL1 (or EL2 -> EL1): everything it reads must be in memory, not in the caches */
    clean_range(KERNEL_BASE, size);
    if (dtb) { u32 tot = __builtin_bswap32(*(volatile u32 *)(UINTN)(dtb + 4)); clean_range(dtb, tot); }
#if defined(__aarch64__)
    /* the kernel expects what QEMU -kernel gives it: MMU and caches off */
    u64 el, sc;
    __asm__ volatile("msr daifset, #15\n mrs %0, CurrentEL" : "=r"(el) :: "memory");
    if ((el >> 2) == 2) { __asm__ volatile("mrs %0, sctlr_el2" : "=r"(sc)); sc &= ~0x1005ULL; __asm__ volatile("msr sctlr_el2, %0\n isb" :: "r"(sc) : "memory"); }
    else                { __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sc)); sc &= ~0x1005ULL; __asm__ volatile("msr sctlr_el1, %0\n isb" :: "r"(sc) : "memory"); }
    register u64 x0 __asm__("x0") = dtb;
    __asm__ volatile("br %1" :: "r"(x0), "r"(KERNEL_BASE) : "memory");
#else
    u32 sc;
    __asm__ volatile("cpsid aif\n mrc p15, 0, %0, c1, c0, 0" : "=r"(sc) :: "memory");
    sc &= ~0x1005u;                                   /* M, C, I off */
    __asm__ volatile("mcr p15, 0, %0, c1, c0, 0\n isb" :: "r"(sc) : "memory");
    register u32 r0 __asm__("r0") = 0;
    register u32 r1 __asm__("r1") = 0xFFFFFFFFu;
    register u32 r2 __asm__("r2") = (u32)dtb;
    __asm__ volatile("bx %3" :: "r"(r0), "r"(r1), "r"(r2), "r"((u32)KERNEL_BASE) : "memory");
#endif
    for (;;) { }
}
