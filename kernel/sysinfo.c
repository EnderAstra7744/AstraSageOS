/* Hardware facts: CPUID, Multiboot memory map summary, CMOS RTC uptime. */
#include "astra.h"
#include "io.h"

extern char _end[];
static u32 mb_mem_upper_kib;      /* from Multiboot info */
static u32 boot_secs;

static void cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
    __asm__ volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(leaf), "c"(0));
}

static u8 cmos(u8 reg) { outb(0x70, reg); return inb(0x71); }

static u32 rtc_once(void)
{
    while (cmos(0x0A) & 0x80) { }                     /* update in progress */
    u32 s = cmos(0), m = cmos(2), h = cmos(4);
    if (!(cmos(0x0B) & 4)) {                          /* BCD -> binary */
        s = (s & 15) + (s >> 4) * 10;
        m = (m & 15) + (m >> 4) * 10;
        h = (h & 15) + ((h & 0x70) >> 4) * 10 + (h & 0x80);
    }
    return h * 3600 + m * 60 + s;
}

static u32 rtc_secs(void)
{
    u32 a, b;
    do { a = rtc_once(); b = rtc_once(); } while (a != b);
    return a;
}

/* Wall-clock date and time from the CMOS RTC (QEMU: -rtc base=localtime shows local time). */
void hal_datetime(hal_time *t)
{
    u32 v[6], w[6];
    do {
        while (cmos(0x0A) & 0x80) { }
        for (int i = 0; i < 6; i++) v[i] = cmos((u8[]){0, 2, 4, 7, 8, 9}[i]);
        while (cmos(0x0A) & 0x80) { }
        for (int i = 0; i < 6; i++) w[i] = cmos((u8[]){0, 2, 4, 7, 8, 9}[i]);
    } while (kmemcmp(v, w, sizeof v) != 0);
    u8 rb = cmos(0x0B);
    u32 pm = v[2] & 0x80;
    if (!(rb & 4)) {
        for (int i = 0; i < 6; i++) {
            u32 x = v[i];
            if (i == 2) x &= 0x7F;
            v[i] = (x & 15) + (x >> 4) * 10;
        }
    } else v[2] &= 0x7F;
    if (!(rb & 2)) {                                  /* 12-hour clock */
        if (pm) { if (v[2] != 12) v[2] += 12; }
        else if (v[2] == 12) v[2] = 0;
    }
    t->sec = v[0]; t->min = v[1]; t->hour = v[2];
    t->day = v[3]; t->month = v[4]; t->year = 2000 + v[5];
}

/* RAM in KiB, best source first: the Multiboot memory map (highest usable end below 4 GiB), then mem_upper, then the CMOS
 * extended-memory registers every PC BIOS / QEMU fills (0x30/0x31: KiB above 1 MiB up to 16 MiB, 0x34/0x35: 64 KiB blocks above 16 MiB). */
static u32 mb_mem_total_kib;
static u32 cmos_total_kib(void)
{
    u32 low = (u32)cmos(0x30) | ((u32)cmos(0x31) << 8);
    u32 high = (u32)cmos(0x34) | ((u32)cmos(0x35) << 8);
    if (!low && !high) return 0;
    return 1024 + low + high * 64;
}
void sysinfo_init(u32 mbi_flags, u32 mem_upper, u32 mmap_len, u32 mmap_addr)
{
    mb_mem_upper_kib = (mbi_flags & 1) ? mem_upper : 0;
    mb_mem_total_kib = 0;
    if ((mbi_flags & 64) && mmap_len && mmap_addr) {
        unsigned long long top = 0;
        for (u32 o = 0; o + 24 <= mmap_len; ) {
            const u8 *e = (const u8 *)(size_t)(mmap_addr + o);
            u32 sz = *(const u32 *)e;
            unsigned long long base = *(const unsigned long long *)(e + 4), len = *(const unsigned long long *)(e + 12);
            u32 type = *(const u32 *)(e + 20);
            if (type == 1 && base < (1ull << 32)) { unsigned long long end = base + len; if (end > (1ull << 32)) end = 1ull << 32; if (end > top) top = end; }
            if (sz < 20 || sz > 64) break;
            o += sz + 4;
        }
        mb_mem_total_kib = (u32)(top >> 10);
    }
    if (!mb_mem_total_kib && mb_mem_upper_kib) mb_mem_total_kib = mb_mem_upper_kib + 1024;
    if (!mb_mem_total_kib) mb_mem_total_kib = cmos_total_kib();
    boot_secs = rtc_secs();
}

u32 hal_uptime_sec(void)
{
    return (rtc_secs() + 86400 - boot_secs) % 86400;
}

u32 hal_mem_total_mib(void)
{
    return mb_mem_total_kib ? (mb_mem_total_kib + 512) / 1024 : 0;
}

u32 hal_mem_used_mib(void)
{
    return ((u32)(size_t)_end + (1u << 20) - 1) >> 20;     /* kernel image end, rounded up */
}

const char *hal_cpu_name(void)
{
    static char name[49];
    if (name[0]) return name;
    u32 a, b, c, d;
    cpuid(0x80000000, &a, &b, &c, &d);
    if (a >= 0x80000004) {
        u32 *w = (u32 *)name;
        for (u32 i = 0; i < 3; i++)
            cpuid(0x80000002 + i, &w[i * 4], &w[i * 4 + 1], &w[i * 4 + 2], &w[i * 4 + 3]);
        name[48] = 0;
        char *s = name;
        while (*s == ' ') s++;
        if (s != name) kmemcpy(name, s, kstrlen(s) + 1);
    } else {
        cpuid(0, &a, &b, &c, &d);
        u32 *w = (u32 *)name;
        w[0] = b; w[1] = d; w[2] = c; name[12] = 0;
    }
    if (!name[0]) kmemcpy(name, "Unknown", 8);
    return name;
}

const char *hal_model(void)
{
    static char sig[13];
    u32 a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(c & (1u << 31))) return "Generic x86 PC";
    cpuid(0x40000000, &a, &b, &c, &d);
    u32 *w = (u32 *)sig;
    w[0] = b; w[1] = c; w[2] = d; sig[12] = 0;
    if (sig[0] == 'T' && sig[1] == 'C' && sig[2] == 'G') return "QEMU (TCG)";
    if (sig[0] == 'K' && sig[1] == 'V' && sig[2] == 'M') return "QEMU/KVM";
    return "Virtual Machine";
}

u32 hal_ticks(void)
{
    u32 lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return lo ^ hi;
}

int hal_cpu_long_mode(void)
{
#if defined(__i386__) || defined(__x86_64__)
    u32 a, b, c, d;
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a < 0x80000001u) return 0;
    cpuid(0x80000001u, &a, &b, &c, &d);
    return (d >> 29) & 1;
#else
    return 1;
#endif
}

u32 hal_cpu_cores(void)
{
    u32 a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    u32 n = (d & (1u << 28)) ? (b >> 16) & 0xFF : 1;
    return n ? n : 1;
}

static u32 pci_read(u32 bus, u32 dev, u32 fn, u32 off)
{
    outl(0xCF8, 0x80000000u | bus << 16 | dev << 11 | fn << 8 | (off & 0xFC));
    return inl(0xCFC);
}

/* First display controller (PCI class 03h) on bus 0. */
const char *hal_gpu_name(void)
{
    static char name[40];
    if (name[0]) return name;
    for (u32 dev = 0; dev < 32; dev++) {
        u32 id = pci_read(0, dev, 0, 0);
        if ((id & 0xFFFF) == 0xFFFF) continue;
        if ((pci_read(0, dev, 0, 8) >> 24) != 3) continue;
        u32 ven = id & 0xFFFF, did = id >> 16;
        const char *n = 0;
        if (ven == 0x1234 && did == 0x1111) n = "QEMU Standard VGA";
        else if (ven == 0x1013)             n = "Cirrus Logic GD 5446";
        else if (ven == 0x15AD)             n = "VMware SVGA II";
        else if (ven == 0x1AF4)             n = "Virtio GPU";
        if (n) ksnprintf(name, sizeof name, "%s", n);
        else   ksnprintf(name, sizeof name, "VGA %x:%x", ven, did);
        return name;
    }
    return "VGA text mode";
}

/* Power off: the ACPI/emulator poweroff ports of QEMU (0x604, and 0xB004 for older ones), VirtualBox (0x4004)
 * and Bochs; real hardware without those ports ends in a halt with the usual message. */
void hal_shutdown(void)
{
    __asm__ volatile("cli");
    __asm__ volatile("outw %0, %1" : : "a"((u16)0x2000), "Nd"((u16)0x604));
    __asm__ volatile("outw %0, %1" : : "a"((u16)0x2000), "Nd"((u16)0xB004));
    __asm__ volatile("outw %0, %1" : : "a"((u16)0x3400), "Nd"((u16)0x4004));
    for (;;) __asm__ volatile("hlt");
}

/* Restart: reset through the keyboard controller, then the chipset reset port, then a deliberate triple fault. */
void hal_reboot(void)
{
    __asm__ volatile("cli");
    for (int i = 0; i < 100000 && (inb(0x64) & 2); i++) { }
    outb(0x64, 0xFE);
    for (volatile int i = 0; i < 2000000; i++) { }
    outb(0xCF9, 0x06);
    for (volatile int i = 0; i < 2000000; i++) { }
    struct { u16 limit; u32 base; } __attribute__((packed)) idt = { 0, 0 };
    __asm__ volatile("lidt %0\n\tint3" : : "m"(idt));
    for (;;) __asm__ volatile("hlt");
}
