/* x86_64: CPU exceptions -> Kernel Panic screen (64-bit IDT; interrupts stay off, only the 32 exception vectors exist). */
#include "astra.h"

#define STUB_NOERR(n)  "isr" #n ": pushq $0\n pushq $" #n "\n jmp exc_common\n"
#define STUB_ERR(n)    "isr" #n ": pushq $" #n "\n jmp exc_common\n"

__asm__(
    ".text\n"
    "exc_common:\n"
    "  movq (%rsp), %rdi\n"        /* vector */
    "  movq 8(%rsp), %rsi\n"       /* error code */
    "  movq 16(%rsp), %rdx\n"      /* rip */
    "  andq $-16, %rsp\n"
    "  call exc_dispatch\n"
    "1: hlt\n jmp 1b\n"
    STUB_NOERR(0) STUB_NOERR(1) STUB_NOERR(2) STUB_NOERR(3) STUB_NOERR(4) STUB_NOERR(5) STUB_NOERR(6) STUB_NOERR(7)
    STUB_ERR(8)   STUB_NOERR(9) STUB_ERR(10)  STUB_ERR(11)  STUB_ERR(12)  STUB_ERR(13)  STUB_ERR(14)  STUB_NOERR(15)
    STUB_NOERR(16) STUB_ERR(17) STUB_NOERR(18) STUB_NOERR(19) STUB_NOERR(20) STUB_NOERR(21) STUB_NOERR(22) STUB_NOERR(23)
    STUB_NOERR(24) STUB_NOERR(25) STUB_NOERR(26) STUB_NOERR(27) STUB_NOERR(28) STUB_NOERR(29) STUB_NOERR(30) STUB_NOERR(31)
);

#define ISR(n) extern void isr##n(void);
ISR(0) ISR(1) ISR(2) ISR(3) ISR(4) ISR(5) ISR(6) ISR(7) ISR(8) ISR(9) ISR(10) ISR(11) ISR(12) ISR(13) ISR(14) ISR(15)
ISR(16) ISR(17) ISR(18) ISR(19) ISR(20) ISR(21) ISR(22) ISR(23) ISR(24) ISR(25) ISR(26) ISR(27) ISR(28) ISR(29) ISR(30) ISR(31)

static void (*const stubs[32])(void) = {
    isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7, isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15,
    isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23, isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31,
};

static const char *const names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range", "Invalid opcode", "Device not available",
    "Double fault", "Coprocessor overrun", "Invalid TSS", "Segment not present", "Stack fault", "General protection fault",
    "Page fault", "Reserved", "x87 FPU error", "Alignment check", "Machine check", "SIMD FPU error",
    "Virtualization", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Security", "Reserved",
};

struct idt_gate { u16 lo, sel; u8 ist, flags; u16 mid; u32 hi, zero; } __attribute__((packed));
static struct idt_gate idt[32];

extern unsigned long long x64_mbi;
static void raw_row(unsigned char *fb, unsigned pitch, int row, unsigned long long v, int bits)
{
    for (int b = 0; b < bits; b++) {
        unsigned col = ((v >> (bits - 1 - b)) & 1) ? 0xFFFFFFFFu : 0x000040FFu;
        for (int y = 0; y < 12; y++)
            for (int x = 0; x < 12; x++)
                *(volatile unsigned *)(fb + (unsigned long)(40 + row * 16 + y) * pitch + (unsigned long)((b * 14 + x) * 4)) = col;
    }
}
/* last resort when the Kernel Panic screen itself fails: bits of RIP / vector+error / CR2 as white (1) and blue (0) squares */
static void raw_dump(unsigned long long vec, unsigned long long err, unsigned long long rip)
{
    const unsigned *m = (const unsigned *)(size_t)x64_mbi;
    if (!x64_mbi || !(m[0] & 0x1000)) return;
    unsigned char *fb = (unsigned char *)(size_t)m[22];
    unsigned pitch = m[24];
    unsigned long long cr2; __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    for (int y = 0; y < 24; y++) for (int x = 0; x < 900; x++) *(volatile unsigned *)(fb + (unsigned long)y * pitch + (unsigned long)x * 4) = 0x00FF0000u;
    raw_row(fb, pitch, 0, rip, 64);
    raw_row(fb, pitch, 1, (vec << 32) | (err & 0xFFFFFFFFull), 64);
    raw_row(fb, pitch, 2, cr2, 64);
}

static int depth;
void __attribute__((used, noreturn)) exc_dispatch(unsigned long long vec, unsigned long long err, unsigned long long rip)
{
    if (depth++) { raw_dump(vec, err, rip); for (;;) __asm__ volatile("hlt"); }
    char r[120];
    ksnprintf(r, sizeof r, "CPU exception %u (%s) at RIP=0x%x%08x, code=0x%x", (u32)vec, vec < 32 ? names[vec] : "?", (u32)(rip >> 32), (u32)rip, (u32)err);
    err_kernel_panic(r);
}

void err_idt_init(void)
{
    for (int i = 0; i < 32; i++) {
        unsigned long long a = (unsigned long long)(size_t)stubs[i];
        idt[i].lo = (u16)a; idt[i].mid = (u16)(a >> 16); idt[i].hi = (u32)(a >> 32);
        idt[i].sel = 8; idt[i].ist = 0; idt[i].flags = 0x8E; idt[i].zero = 0;     /* present, ring 0, 64-bit interrupt gate */
    }
    struct { u16 limit; unsigned long long base; } __attribute__((packed)) p = { sizeof idt - 1, (unsigned long long)(size_t)idt };
    __asm__ volatile("lidt %0" : : "m"(p));
}
