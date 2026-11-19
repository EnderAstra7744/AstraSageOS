/* AstraSageOS ARM32: ARMv7-A, QEMU "virt" (-M virt -cpu cortex-a15 -kernel). Loaded at 0x40010000 (zImage-less raw binary). */
    .section .text.boot, "ax"
    .arm
    .global _start
_start:
    cpsid   if
    ldr     sp, =__stack_top
    ldr     r0, =__bss_start
    ldr     r1, =__bss_end
    mov     r2, #0
1:  cmp     r0, r1
    strlo   r2, [r0], #4
    blo     1b
    bl      arm32_main
2:  wfi
    b       2b
