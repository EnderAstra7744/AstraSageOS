/* as_kernel boot stub: Multiboot 1 header, stack, jump to kmain(magic, mbi). */
    .set MB_MAGIC, 0x1BADB002
    .set MB_FLAGS, 0x00000007            /* align modules, provide memory info, request a graphics mode (bit 2) */
    .set MB_CHECK, -(MB_MAGIC + MB_FLAGS)

    .section .multiboot, "a"
    .align 4
    .long MB_MAGIC
    .long MB_FLAGS
    .long MB_CHECK
    /* address fields: only used with bit 16 (a.out kludge); this is an ELF kernel, so they stay zero */
    .long 0, 0, 0, 0, 0
    /* video request: linear framebuffer 1280x720x32. Without this GRUB hands over VGA text mode only
     * (GRUB's multiboot loader decides by this flag). The kernel accepts whatever direct-RGB mode it gets. */
    .long 0                              /* mode_type 0 = graphics */
    .long 1280, 720, 32

    .section .bss
    .align 16
stack_bottom:
    .skip 131072
stack_top:

    .section .text
    .global _start
    .type _start, @function
_start:
    cli
    movl %eax, %esi                      /* keep the multiboot handoff: x87 setup below may not touch it */
    movl %ebx, %edi
    movl %cr0, %ecx                      /* x87 on: EM=0 TS=0 MP=1 NE=1 */
    andl $~0xC, %ecx
    orl  $0x22, %ecx
    movl %ecx, %cr0
    fninit
    movl $stack_top, %esp
    subl $8, %esp                        /* keep 16-byte alignment at the call */
    pushl %edi                           /* multiboot info pointer */
    pushl %esi                           /* multiboot magic */
    call kmain
1:  cli
    hlt
    jmp 1b
    .size _start, . - _start
