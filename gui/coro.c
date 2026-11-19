/*
 * Minimal i386 coroutines. The AST shell is an ordinary blocking program ("read a line, run it");
 * the desktop needs to keep drawing and handling the mouse while it waits. So the shell runs on
 * its own stack and coro_yield()s whenever it waits for a key; the desktop loop resumes it when
 * one arrives. Only the callee-saved registers need to be swapped - switches happen at calls.
 */
#include "gui.h"

#if defined(__arm__)
__asm__(
    ".text\n"
    ".arm\n"
    ".globl coro_switch\n"
    "coro_switch:\n"
    "    push {r4-r12, lr}\n"
    "    vpush {d8-d15}\n"
    "    str sp, [r0]\n"
    "    mov sp, r1\n"
    "    vpop {d8-d15}\n"
    "    pop {r4-r12, lr}\n"
    "    bx lr\n"
);
#define CORO_ARM32 1
#elif defined(__aarch64__)
__asm__(
    ".text\n"
    ".globl coro_switch\n"
    "coro_switch:\n"
    "    sub sp, sp, #160\n"
    "    stp x19, x20, [sp, #0]\n"
    "    stp x21, x22, [sp, #16]\n"
    "    stp x23, x24, [sp, #32]\n"
    "    stp x25, x26, [sp, #48]\n"
    "    stp x27, x28, [sp, #64]\n"
    "    stp x29, x30, [sp, #80]\n"
    "    stp d8, d9, [sp, #96]\n"
    "    stp d10, d11, [sp, #112]\n"
    "    stp d12, d13, [sp, #128]\n"
    "    stp d14, d15, [sp, #144]\n"
    "    mov x9, sp\n"
    "    str x9, [x0]\n"                       /* save the current stack pointer */
    "    mov sp, x1\n"                         /* continue on the other stack */
    "    ldp x19, x20, [sp, #0]\n"
    "    ldp x21, x22, [sp, #16]\n"
    "    ldp x23, x24, [sp, #32]\n"
    "    ldp x25, x26, [sp, #48]\n"
    "    ldp x27, x28, [sp, #64]\n"
    "    ldp x29, x30, [sp, #80]\n"
    "    ldp d8, d9, [sp, #96]\n"
    "    ldp d10, d11, [sp, #112]\n"
    "    ldp d12, d13, [sp, #128]\n"
    "    ldp d14, d15, [sp, #144]\n"
    "    add sp, sp, #160\n"
    "    ret\n"
);
#define CORO_ARM64 1
#elif defined(__x86_64__)
__asm__(
    ".text\n"
    ".globl coro_switch\n"
    "coro_switch:\n"
    "    pushq %rbp\n pushq %rbx\n pushq %r12\n pushq %r13\n pushq %r14\n pushq %r15\n"
    "    movq %rsp, (%rdi)\n"
    "    movq %rsi, %rsp\n"
    "    popq %r15\n popq %r14\n popq %r13\n popq %r12\n popq %rbx\n popq %rbp\n"
    "    ret\n"
);
#define CORO_X64 1
#else

__asm__(
    ".text\n"
    ".globl coro_switch\n"
    "coro_switch:\n"
    "    movl 4(%esp), %eax\n"          /* where to save the current stack pointer */
    "    movl 8(%esp), %edx\n"          /* stack pointer to continue on */
    "    pushl %ebp\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    movl %esp, (%eax)\n"
    "    movl %edx, %esp\n"
    "    popl %edi\n"
    "    popl %esi\n"
    "    popl %ebx\n"
    "    popl %ebp\n"
    "    ret\n"
);
#endif
void coro_switch(size_t *save_sp, size_t new_sp);

static coro *cur;

coro *coro_current(void) { return cur; }

static void trampoline(void)
{
    cur->fn();
    cur->done = 1;
    for (;;) coro_yield();                      /* never resumed again */
}

void coro_init(coro *c, void (*fn)(void), u8 *stack, u32 size)
{
    c->fn = fn; c->started = 0; c->done = 0; c->stack = stack; c->stack_size = size;
#ifdef CORO_ARM32
    size_t top = ((size_t)stack + size) & ~(size_t)7;
    size_t *sp = (size_t *)(top - 104);         /* d8..d15, r4..r12, lr: popped by coro_switch */
    for (int i = 0; i < 26; i++) sp[i] = 0;
    sp[25] = (size_t)trampoline;                /* lr: where "bx lr" goes */
    c->sp = (size_t)sp;
#elif defined(CORO_X64)
    size_t top = ((size_t)stack + size) & ~(size_t)15;
    size_t *sp = (size_t *)top;
    *--sp = 0;                                  /* fake return address (keeps the stack aligned for the trampoline) */
    *--sp = (size_t)trampoline;                 /* popped by "ret" in coro_switch */
    for (int i = 0; i < 6; i++) *--sp = 0;      /* rbp rbx r12..r15 */
    c->sp = (size_t)sp;
#elif defined(CORO_ARM64)
    size_t top = ((size_t)stack + size) & ~(size_t)15;
    size_t *sp = (size_t *)(top - 160);         /* x19..x28, x29, x30, d8..d15: popped by coro_switch */
    for (int i = 0; i < 20; i++) sp[i] = 0;
    sp[11] = (size_t)trampoline;                /* x30: where "ret" goes */
    c->sp = (size_t)sp;
#else
    u32 top = ((u32)(size_t)stack + size) & ~15u;
    u32 *sp = (u32 *)(size_t)top;
    *--sp = 0;                                  /* fake return address of the trampoline */
    *--sp = (u32)(size_t)trampoline;            /* popped by "ret" in coro_switch */
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0; /* ebp ebx esi edi */
    c->sp = (u32)(size_t)sp;
#endif
}

void coro_resume(coro *c)
{
    if (c->done) return;
    coro *prev = cur;
    cur = c;
    c->started = 1;
    coro_switch(&c->resume_sp, c->sp);
    cur = prev;
}

void coro_yield(void)
{
    coro *c = cur;
    if (!c) return;
    coro_switch(&c->sp, c->resume_sp);
}
