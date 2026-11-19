/* AstraSageOS x86_64: Multiboot 1 entry (32-bit), identity page tables for the first 4 GiB (2 MiB pages), SSE on, long mode,
 * then kmain(magic, mbi) in 64-bit code. Same GRUB / QEMU -kernel entry as the i386 kernel. */
    .set MB_MAGIC, 0x1BADB002
    .set MB_FLAGS, 0x00000007
    .section .multiboot, "a"
    .align 4
    .long MB_MAGIC, MB_FLAGS, -(MB_MAGIC + MB_FLAGS)
    .long 0, 0, 0, 0, 0
    .long 0
    .long 1280, 720, 32                  /* linear framebuffer request, like the i386 kernel */

    .section .bss
    .align 4096
pml4: .skip 4096
pdpt: .skip 4096
pd:   .skip 4096 * 4
    .global x64_mbi
x64_mbi: .skip 8
    .align 16
stack_bottom: .skip 262144
stack_top:

    .section .rodata
    .align 8
gdt:
    .quad 0
    .quad 0x00209A0000000000            /* 64-bit code (selector 8) */
    .quad 0x00CF92000000FFFF            /* flat data, 4 GiB (selector 16; also what 32-bit native programs use) */
    .quad 0x00CF9A000000FFFF            /* 32-bit code, compatibility mode (selector 24) */
gdt_end:
gdt_ptr:
    .word gdt_end - gdt - 1
    .long gdt

    .macro DELAY                          /* diagnosis: keep the squares visible for a few seconds */
    movl $150000000, %ecx
6:  decl %ecx
    jnz 6b
    .endm
    .macro MARK32 x, color                /* progress square on the framebuffer (diagnosis: see arch/x86_64/README.md) */
    testl $0x1000, (%edi)
    jz 9f
    movl 88(%edi), %eax
    movl 96(%edi), %ebx
    addl $(\x*4), %eax
    movl $24, %ecx
8:  movl %eax, %ebp
    movl $20, %edx
7:  movl $\color, (%ebp)
    addl $4, %ebp
    decl %edx
    jnz 7b
    addl %ebx, %eax
    decl %ecx
    jnz 8b
9:
    .endm
    .macro MARK64 x, color
    testl $0x1000, (%rdi)
    jz 9f
    movl 88(%rdi), %eax
    movl 96(%rdi), %ebx
    addq $(\x*4), %rax
    movl $24, %ecx
8:  movq %rax, %rbp
    movl $20, %edx
7:  movl $\color, (%rbp)
    addq $4, %rbp
    decl %edx
    jnz 7b
    addq %rbx, %rax
    decl %ecx
    jnz 8b
9:
    .endm

    .section .text
    .code32
    .global _start
_start:
    cli
    movl %eax, %esi                     /* multiboot magic */
    movl %ebx, %edi                     /* multiboot info pointer */
    movl $0x80000000, %eax              /* does the CPU have long mode? (an i386/qemu32 CPU does not: wrmsr EFER would fault) */
    cpuid
    cmpl $0x80000001, %eax
    jb   nolm
    movl $0x80000001, %eax
    cpuid
    btl  $29, %edx
    jc   havelm
nolm:
    MARK32 48, 0xFF0000                 /* red: this CPU/emulator has no 64-bit mode - use the 32-bit entry */
    cli
9:  hlt
    jmp 9b
havelm:
    movl $gdt_ptr, %eax
    lgdt (%eax)                         /* before paging: in long mode LGDT would read a 10-byte descriptor */
    /* only absolute addresses: this 32-bit code is assembled by the 64-bit assembler (symbol references would be rip-relative) */
    movl $stack_top, %esp
    movl $pdpt, %eax
    orl  $3, %eax
    movl $pml4, %ebx
    movl %eax, (%ebx)
    xorl %ecx, %ecx                     /* pdpt[0..3] -> four page directories */
1:  movl %ecx, %eax
    shll $12, %eax
    addl $pd, %eax
    orl  $3, %eax
    movl $pdpt, %ebx
    movl %eax, (%ebx,%ecx,8)
    incl %ecx
    cmpl $4, %ecx
    jne  1b
    xorl %ecx, %ecx                     /* 2048 entries of 2 MiB = 4 GiB */
2:  movl %ecx, %eax
    shll $21, %eax
    orl  $0x83, %eax
    movl $pd, %ebx
    movl %eax, (%ebx,%ecx,8)
    incl %ecx
    cmpl $2048, %ecx
    jne  2b
    movl $pml4, %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl  $0x620, %eax                   /* PAE | OSFXSR | OSXMMEXCPT */
    movl %eax, %cr4
    movl $0xC0000080, %ecx
    rdmsr
    orl  $0x100, %eax                   /* LME */
    wrmsr
    movl %cr0, %eax
    andl $0xFFFFFFFB, %eax              /* EM = 0 */
    orl  $0x80000023, %eax              /* PG | NE | MP | PE */
    movl %eax, %cr0
    ljmp $8, $long_mode
    .code64
long_mode:
    movw $16, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    xorl %eax, %eax
    movw %ax, %fs
    movw %ax, %gs
    movl %edi, %edi                     /* the upper halves of the registers are undefined after the mode switch */
    movl %esi, %esi
    movq $stack_top, %rsp
    movl %edi, %eax
    movq %rax, x64_mbi
    movl %esi, %r12d                    /* the magic must survive the call below: rsi is caller-saved (a C function may clobber it - that gave "magic is ffff8e00"), r12 is callee-saved */
    call err_idt_init                   /* exceptions are caught from here on */
    movl x64_mbi, %esi                  /* kmain(magic, mbi): rdi = magic, rsi = mbi */
    movl %r12d, %edi
    xorl %ebp, %ebp
    call kmain
3:  cli
    hlt
    jmp 3b
