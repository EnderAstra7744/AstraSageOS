/*
 * CPU exceptions -> Kernel Panic screen.
 * The kernel runs with interrupts off, so only the 32 exception vectors are installed.
 * Each stub pushes a dummy error code (when the CPU does not) and its vector, then calls exc_dispatch.
 */
#include "astra.h"

#define STUB_NOERR(n)  "isr" #n ": pushl $0\n pushl $" #n "\n jmp exc_common\n"
#define STUB_ERR(n)    "isr" #n ": pushl $" #n "\n jmp exc_common\n"

__asm__(
    ".text\n"
    "exc_common:\n"
    "  pusha\n"
    "  movl 32(%esp), %eax\n"      /* vector */
    "  movl 36(%esp), %ebx\n"      /* error code */
    "  movl 40(%esp), %ecx\n"      /* eip */
    "  pushl %ecx\n pushl %ebx\n pushl %eax\n"
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

struct idt_gate { u16 lo; u16 sel; u8 zero; u8 flags; u16 hi; } __attribute__((packed));
static struct idt_gate idt[32];

void __attribute__((used, noreturn, regparm(0))) exc_dispatch(u32 vec, u32 err, u32 eip)
{
    char r[120];
    ksnprintf(r, sizeof r, "CPU exception %u (%s) at EIP=0x%x, code=0x%x", vec, vec < 32 ? names[vec] : "?", eip, err);
    err_kernel_panic(r);
}

void err_idt_init(void)
{
    u16 cs;
    __asm__ volatile("movw %%cs, %0" : "=r"(cs));
    for (int i = 0; i < 32; i++) {
        u32 a = (u32)stubs[i];
        idt[i].lo = (u16)(a & 0xFFFF); idt[i].hi = (u16)(a >> 16);
        idt[i].sel = cs; idt[i].zero = 0; idt[i].flags = 0x8E;      /* present, ring 0, 32-bit interrupt gate */
    }
    struct { u16 limit; u32 base; } __attribute__((packed)) p = { sizeof idt - 1, (u32)idt };
    __asm__ volatile("lidt %0" : : "m"(p));
}
