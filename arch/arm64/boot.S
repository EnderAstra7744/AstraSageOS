/* AstraSageOS ARM64: entry for QEMU "virt" (-kernel Image) and any firmware that follows the Linux arm64 boot protocol.
 * x0 = device tree pointer. Runs at EL2 or EL1; always continues at EL1. MMU and caches stay off (identity, Device memory),
 * so the C code is built with -mstrict-align. */
    .section .text.boot, "ax"
    .global _start
_start:
    b       1f                       /* code0: jump over the header */
    .long   0
    .quad   0x80000                  /* text_offset: loaded 512 KiB above the start of RAM (linker.ld: 0x40080000) */
    .quad   _end - _start            /* image size */
    .quad   0x0a                     /* flags: little endian, 4K pages, anywhere in RAM */
    .quad   0, 0, 0
    .ascii  "ARM\x64"
    .long   0
1:
    mov     x19, x0                  /* keep the dtb pointer */
    mrs     x0, CurrentEL
    lsr     x0, x0, #2
    cmp     x0, #2
    b.ne    2f
    /* EL2 -> EL1 (aarch64, no MMU), interrupts masked */
    mov     x0, #(1 << 31)           /* HCR_EL2.RW = 1: EL1 is aarch64 */
    msr     hcr_el2, x0
    mov     x0, #0x33ff
    msr     cptr_el2, x0             /* no trapping of FP/SIMD */
    msr     hstr_el2, xzr
    mrs     x0, cnthctl_el2
    orr     x0, x0, #3
    msr     cnthctl_el2, x0          /* EL1 may use the physical timer */
    msr     cntvoff_el2, xzr
    mov     x0, #0x0800
    movk    x0, #0x30d0, lsl #16     /* SCTLR_EL1 reset value (MMU, caches off) */
    msr     sctlr_el1, x0
    mov     x0, #0x3c5               /* SPSR_EL2: EL1h, DAIF masked */
    msr     spsr_el2, x0
    adr     x0, 2f
    msr     elr_el2, x0
    eret
2:
    mov     x0, #(3 << 20)
    msr     cpacr_el1, x0            /* FP/SIMD allowed at EL1 */
    isb
    ldr     x0, =__stack_top
    mov     sp, x0
    ldr     x0, =vectors
    msr     vbar_el1, x0
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
3:  cmp     x0, x1
    b.hs    4f
    str     xzr, [x0], #8
    b       3b
4:
    mov     x0, x19
    bl      arm64_main
5:  wfe
    b       5b

/* exception vectors: every entry reports through the C handler (type 0..15) and stops */
    .macro  vec n
    .balign 128
    mov     x0, #\n
    b       exc_common
    .endm
    .balign 2048
    .global vectors
vectors:
    vec 0
    vec 1
    vec 2
    vec 3
    vec 4
    vec 5
    vec 6
    vec 7
    vec 8
    vec 9
    vec 10
    vec 11
    vec 12
    vec 13
    vec 14
    vec 15
exc_common:
    mrs     x1, esr_el1
    mrs     x2, elr_el1
    mrs     x3, far_el1
    bl      arm64_exception
6:  wfe
    b       6b
