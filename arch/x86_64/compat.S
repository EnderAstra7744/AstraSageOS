/* x86_64: runs the 32-bit native programs (NetSurf, FroggyCode, ...) in the CPU's compatibility mode.
 *
 *   native_enter64(entry, api32, args32, stack_top)   64-bit -> far return into 32-bit code, calls as_main(api, args)
 *   thunks:  the program calls an API function (32-bit cdecl, arguments on its stack) -> the thunk jumps back into
 *            64-bit mode, loads the arguments into the System V registers, calls the kernel function on the kernel
 *            stack and returns to the program with the result in EAX.
 * Everything the program touches (code, stack, heap, strings) lives below 4 GiB, which is identity-mapped, so 32-bit
 * addresses simply zero-extend to 64-bit pointers.
 * CS64 / CS32 / DS32 are the GDT selectors (boot.S: 8, 24, 16); the host test overrides them with Linux's. */
#ifndef CS64
#define CS64 8
#define CS32 24
#define DS32 16
#endif
    .data
    .align 8
native_ksp: .quad 0

    .text
    .code64
    .globl native_enter64
native_enter64:
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, native_ksp
    movl %ecx, %eax
    andq $-16, %rax
    subq $20, %rax                      /* esp after the far return = base+16 = 12 mod 16: the i386 ABI wants esp+4 aligned */
    movq %rax, %rsp
    movl %edi, %edi
    movq %rdi, 0(%rsp)                  /* far return target: entry point */
    movq $CS32, 8(%rsp)                 /*                    code segment */
    movl $native_ret32, 16(%rsp)        /* return address of as_main */
    movl %esi, 20(%rsp)                 /* argument 1: api */
    movl %edx, 24(%rsp)                 /* argument 2: args */
    movl $DS32, %eax
    movl %eax, %ds
    movl %eax, %es
    lretq

    .code32
native_ret32:                           /* as_main returned: eax = exit code */
    ljmp $CS64, $native_back64
    .code64
native_back64:
    movq native_ksp, %rsp
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp
    ret

/* ---- API thunks ---- */
    .code32
    .macro THUNK n
athunk\n:
    movl $\n, %eax
    jmp athunk_common
    .endm
    THUNK 0
    THUNK 1
    THUNK 2
    THUNK 3
    THUNK 4
    THUNK 5
    THUNK 6
    THUNK 7
    THUNK 8
    THUNK 9
    THUNK 10
    THUNK 11
    THUNK 12
    THUNK 13
    THUNK 14
    THUNK 15
    THUNK 16
    THUNK 17
    THUNK 18
    THUNK 19
    THUNK 20
    THUNK 21
    THUNK 22
athunk_common:
    ljmp $CS64, $athunk64
    .code64
athunk64:
    movl %esp, %r12d                    /* the program's stack (callee-saved, survives the C call) */
    movl %esi, %r13d                    /* esi/edi are callee-saved in cdecl but not in System V */
    movl %edi, %r14d
    movl %eax, %eax
    movq $native_api_fns, %rdx
    movq (%rdx,%rax,8), %r11
    movq native_ksp, %rsp               /* run on the kernel's own stack, below the frame of native_enter64 */
    subq $512, %rsp
    andq $-16, %rsp
    movl 4(%r12), %edi
    movl 8(%r12), %esi
    movl 12(%r12), %edx
    movl 16(%r12), %ecx
    movl 20(%r12), %r8d
    movl 24(%r12), %r9d
    subq $48, %rsp                      /* arguments 7..12 go on the 64-bit stack (http_request has 11) */
    movl 28(%r12), %eax
    movq %rax, (%rsp)
    movl 32(%r12), %eax
    movq %rax, 8(%rsp)
    movl 36(%r12), %eax
    movq %rax, 16(%rsp)
    movl 40(%r12), %eax
    movq %rax, 24(%rsp)
    movl 44(%r12), %eax
    movq %rax, 32(%rsp)
    movl 48(%r12), %eax
    movq %rax, 40(%rsp)
    call *%r11
    movl %r13d, %esi
    movl %r14d, %edi
    movq %r12, %rsp
    pushq $CS32
    pushq $athunk_ret32
    lretq
    .code32
athunk_ret32:
    ret

    .data
    .align 4
    .globl native_thunks
native_thunks:
    .long athunk0, athunk1, athunk2, athunk3, athunk4, athunk5, athunk6, athunk7, athunk8, athunk9, athunk10
    .long athunk11, athunk12, athunk13, athunk14, athunk15, athunk16, athunk17, athunk18, athunk19, athunk20, athunk21, athunk22
