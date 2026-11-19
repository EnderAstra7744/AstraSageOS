/* AstraSageOS ARM32 port, first stage: PL011 serial hello and echo. NOT RUN (no ARM emulator here). */
#define UART0 0x09000000u
static inline void w32(unsigned a, unsigned v) { *(volatile unsigned *)a = v; }
static inline unsigned r32(unsigned a) { return *(volatile unsigned *)a; }
static void putc_(char c) { while (r32(UART0 + 0x18) & (1u << 5)) { } w32(UART0, (unsigned char)c); }
static void puts_(const char *s) { while (*s) { if (*s == '\n') putc_('\r'); putc_(*s++); } }
void arm32_main(void)
{
    puts_("AstraSageOS ARM32 (QEMU virt, armv7)\nserial echo:\n");
    for (;;) {
        if (r32(UART0 + 0x18) & (1u << 4)) continue;
        char c = (char)(r32(UART0) & 0xFF);
        if (c == '\r') c = '\n';
        putc_(c);
    }
}
