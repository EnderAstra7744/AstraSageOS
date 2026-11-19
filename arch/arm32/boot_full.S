/* AstraSageOS ARM32 full kernel: entered by QEMU "virt" (-kernel raw binary at 0x40010000) with r0 = 0, r1 = machine, r2 = device tree. */
    .section .text.boot, "ax"
    .arm
    .global _start
_start:
    cpsid   if
    mov     r4, r2                      /* device tree */
    ldr     r0, =vectors
    mcr     p15, 0, r0, c12, c0, 0      /* VBAR */
    mrc     p15, 0, r0, c1, c0, 0       /* SCTLR: vectors in low memory (V = 0), no alignment checks */
    bic     r0, r0, #(1 << 13)
    bic     r0, r0, #2
    mcr     p15, 0, r0, c1, c0, 0
    mrc     p15, 0, r0, c1, c0, 2       /* CPACR: allow VFP (cp10, cp11) */
    orr     r0, r0, #(0xF << 20)
    mcr     p15, 0, r0, c1, c0, 2
    isb
    mov     r0, #0x40000000             /* FPEXC.EN */
    vmsr    fpexc, r0
    ldr     sp, =__stack_top
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    mov     r0, r4
    bl      arm32_main
2:  wfi
    b       2b

    .macro VEC n
    ldr     sp, =__stack_top            /* every exception mode has its own (still unset) stack pointer */
    mov     r0, #\n
    b       exc_common
    .endm
    .balign 32
vectors:
    b       _start                      /* reset */
    b       v_undef
    b       v_svc
    b       v_pabt
    b       v_dabt
    b       v_dabt
    b       v_irq
    b       v_fiq
v_undef: VEC 1
v_svc:   VEC 2
v_pabt:  VEC 3
v_dabt:  VEC 4
v_irq:   VEC 6
v_fiq:   VEC 7
exc_common:
    mov     r1, lr
    mrc     p15, 0, r2, c5, c0, 0       /* DFSR */
    mrc     p15, 0, r3, c6, c0, 0       /* DFAR */
    bl      arm32_exception
3:  wfi
    b       3b
    .ltorg
