/* setjmp / longjmp for ARM32 hard-float: r4-r11, sp, lr, d8-d15 (104 bytes; the AstraScript buffers hold 128) */
    .text
    .arm
    .globl as_setjmp
as_setjmp:
    stmia   r0!, {r4-r11, sp, lr}
    vstmia  r0, {d8-d15}
    mov     r0, #0
    bx      lr
    .globl as_longjmp
as_longjmp:
    ldmia   r0!, {r4-r11, sp, lr}
    vldmia  r0, {d8-d15}
    mov     r0, #1
    bx      lr
