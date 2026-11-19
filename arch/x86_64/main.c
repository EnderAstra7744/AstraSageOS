/* AstraSageOS x86_64 port, first stage: long mode, serial + VGA text, nothing else. NOT RUN (no x86_64 emulator here). */
typedef unsigned char u8; typedef unsigned short u16;
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" :: "a"(v), "Nd"(p)); }
static inline u8 inb(u16 p) { u8 v; __asm__ volatile("inb %1,%0" : "=a"(v) : "Nd"(p)); return v; }
static int cx, cy;
static void putc_(char c)
{
    while (!(inb(0x3FD) & 0x20)) { }
    outb(0x3F8, (u8)c);
    volatile u16 *vga = (volatile u16 *)0xB8000;
    if (c == '\n') { cx = 0; cy++; }
    else { vga[cy * 80 + cx] = (u16)(0x0A00 | (u8)c); if (++cx == 80) { cx = 0; cy++; } }
    if (cy >= 25) { for (int i = 0; i < 80 * 24; i++) vga[i] = vga[i + 80]; for (int i = 80 * 24; i < 80 * 25; i++) vga[i] = 0x0A20; cy = 24; }
}
void x64_main(void)
{
    volatile u16 *vga = (volatile u16 *)0xB8000;
    for (int i = 0; i < 80 * 25; i++) vga[i] = 0x0A20;
    outb(0x3F9, 0); outb(0x3FB, 3);
    const char *m = "AstraSageOS x86_64 (long mode reached)\n";
    while (*m) putc_(*m++);
    for (;;) __asm__ volatile("hlt");
}
