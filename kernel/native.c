/*
 * Native programs: loads a 32-bit ELF (linked at NATIVE_BASE, see native/as_api.h), gives it the API table and a
 * private stack, and runs it on the calling coroutine (so win_wait() keeps the desktop alive).
 */
#include "astra.h"
#include "gui.h"
#include "../native/as_api.h"

extern char _end[];

typedef struct { u8 id[16]; u16 type, machine; u32 version, entry, phoff, shoff, flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } elf_hdr;
typedef struct { u32 type, offset, vaddr, paddr, filesz, memsz, flags, align; } elf_ph;
/* ARM64 programs are ELF64 files */
typedef struct { u8 id[16]; u16 type, machine; u32 version; unsigned long long entry, phoff, shoff; u32 flags; u16 ehsize, phentsize, phnum, shentsize, shnum, shstrndx; } elf64_hdr;
typedef struct { u32 type, flags; unsigned long long offset, vaddr, paddr, filesz, memsz, align; } elf64_ph;

#if defined(__aarch64__) || defined(__arm__)
/* RAM end for native programs (below the frame buffer), provided by the architecture's hw.c */
u32 native_ram_top(void);
/* the program's code was written through the data cache: make it visible to instruction fetches */
static void native_sync_icache(u32 a, u32 n)
{
    u32 e = a + n;
#if defined(__aarch64__)
    for (u32 p = a & ~15u; p < e; p += 16) __asm__ volatile("dc cvau, %0" :: "r"((unsigned long long)p) : "memory");
    __asm__ volatile("dsb ish\n ic ialluis\n dsb ish\n isb" ::: "memory");
#else
    for (u32 p = a & ~15u; p < e; p += 16) __asm__ volatile("mcr p15, 0, %0, c7, c10, 1" :: "r"(p) : "memory");
    __asm__ volatile("dsb\n mcr p15, 0, %0, c7, c5, 0\n mcr p15, 0, %0, c7, c5, 6\n dsb\n isb" :: "r"(0) : "memory");
#endif
}
#endif

#define NATIVE_STACK (512u * 1024u)

static int api_win_wait(int ms) { return sgui_wait(ms); }
static void api_file_noop(void) {}
static int api_http_get(const char *url, unsigned char *buf, unsigned cap, unsigned *len, int *status)
{
    return net_http_download(url, buf, cap, len, status);
}

static int api_exec(const char *cmd, char *out, int cap) { return ast_capture_command(cmd, out, cap); }
static int api_aspi(const char *args, char *out, int cap)
{
    out[0] = 0;
    if (!user_name()) return -1;
    con_capture_begin(out, (u32)cap);
    aspi_command(args);
    con_capture_end();
    return 0;
}

static unsigned api_ms(void) { return ui_ms(); }
static unsigned api_epoch(void)
{
    hal_time t;
    hal_datetime(&t);
    return ktime_pack(&t) + 946684800u;                  /* the RTC clock is read as UTC */
}

#if defined(__x86_64__)
/* 64-bit kernel: native programs stay 32-bit i386 ELF files and run in compatibility mode (arch/x86_64/compat.S).
 * The table they see has 32-bit pointers; every entry is a thunk that calls the kernel function below. */
typedef struct { u32 version, heap, heap_size, fn[23]; } as_api32;
extern const u32 native_thunks[23];
int native_enter64(u32 entry, const as_api32 *api, const char *args, u32 stack_top);
const void *const native_api_fns[23] = {
    (void *)kputs,
    (void *)sgui_open, (void *)sgui_close, (void *)sgui_clear, (void *)sgui_rect, (void *)sgui_pixel, (void *)sgui_text,
    (void *)sgui_show, (void *)api_win_wait, (void *)sgui_mouse,
    (void *)ast_file_read, (void *)ast_file_put, (void *)api_http_get, (void *)api_ms, (void *)api_epoch,
    (void *)sgui_event, (void *)sgui_blit, (void *)sgui_size, (void *)net_http_fetch,
    (void *)api_exec, (void *)api_aspi, (void *)ast_file_write, (void *)net_http_request,
};
#else
static const as_api api_table_proto = {
    AS_API_VERSION, 0, 0,
    kputs,
    sgui_open, sgui_close, sgui_clear, sgui_rect, sgui_pixel, sgui_text, sgui_show, api_win_wait, sgui_mouse,
    ast_file_read, ast_file_put, api_http_get, api_ms, api_epoch,
    sgui_event, sgui_blit, sgui_size, net_http_fetch,
    api_exec, api_aspi,
    ast_file_write, net_http_request,
};
#endif

static int native_busy;

typedef struct { const u8 *img; int node; } rdr;
static int rd(const rdr *r, u32 off, void *dst, u32 n)
{
    if (r->img) { kmemcpy(dst, r->img + off, n); return 0; }
    return fs_pread(r->node, off, dst, n) == (int)n ? 0 : -1;
}

static int native_go(const rdr *rr, u32 len, const char *args, const char **why);

/* returns the program's exit code, or <0 for a load error (message in *why) */
int native_run(const u8 *img, u32 len, const char *args, const char **why)
{
    rdr r = { img, 0 };
    return native_go(&r, len, args, why);
}

/* runs an ELF straight from a file (of any size; its segments are read from the disk, not copied through RAM first) */
int native_run_node(int node, const char *args, const char **why)
{
    rdr r = { 0, node };
    return native_go(&r, fs_size_real(node), args, why);
}

static int native_go(const rdr *rr, u32 len, const char *args, const char **why)
{
#if defined(__x86_64__)
    static as_api32 api;
#else
    static as_api api;
#endif
    static const char *dummy;
    if (!why) why = &dummy;
    *why = 0;
    (void)api_file_noop;
#ifdef AS_NO_IO
    { extern void native_test_map(void); native_test_map(); }
#endif
    if (native_busy) { *why = "a native program is already running"; return -1; }
    /* the program headers of any supported format, in one shape */
    typedef struct { u32 type, offset, vaddr, filesz, memsz; } seg_t;
    seg_t sg[16];
    u32 nseg = 0, entry_addr = 0;
#if defined(__aarch64__)
    elf64_hdr hh;
    if (len < sizeof hh || rd(rr, 0, &hh, sizeof hh) || hh.id[0] != 0x7f || hh.id[1] != 'E' || hh.id[2] != 'L' || hh.id[3] != 'F') { *why = "not an ELF file"; return -1; }
    if (hh.id[4] != 2 || hh.machine != 183 || hh.type != 2) { *why = "not an ARM64 (aarch64) executable: this kernel runs ARM64 native programs"; return -1; }
    if (hh.phnum > 16 || hh.phentsize != sizeof(elf64_ph) || hh.phoff + (u32)hh.phnum * sizeof(elf64_ph) > len) { *why = "damaged ELF header"; return -1; }
    elf64_ph ph[16];
    if (rd(rr, (u32)hh.phoff, ph, hh.phnum * (u32)sizeof(elf64_ph))) { *why = "damaged ELF header"; return -1; }
    for (u32 i = 0; i < hh.phnum; i++) {
        if (ph[i].vaddr >> 32 || ph[i].memsz >> 32 || ph[i].offset >> 32) { *why = "program is not linked at the native base address"; return -1; }
        sg[i] = (seg_t){ ph[i].type, (u32)ph[i].offset, (u32)ph[i].vaddr, (u32)ph[i].filesz, (u32)ph[i].memsz };
    }
    nseg = hh.phnum; entry_addr = (u32)hh.entry;
#else
    elf_hdr hh;
    if (len < sizeof hh || rd(rr, 0, &hh, sizeof hh) || hh.id[0] != 0x7f || hh.id[1] != 'E' || hh.id[2] != 'L' || hh.id[3] != 'F') { *why = "not an ELF file"; return -1; }
#if defined(__arm__)
    if (hh.id[4] != 1 || hh.machine != 40 || hh.type != 2) { *why = "not an ARM32 executable: this kernel runs ARM32 native programs"; return -1; }
#else
    if (hh.id[4] != 1 || hh.machine != 3 || hh.type != 2) { *why = "not a 32-bit i386 executable"; return -1; }
#endif
    if (hh.phnum > 16 || hh.phentsize != sizeof(elf_ph) || hh.phoff + (u32)hh.phnum * sizeof(elf_ph) > len) { *why = "damaged ELF header"; return -1; }
    elf_ph ph[16];
    if (rd(rr, hh.phoff, ph, hh.phnum * (u32)sizeof(elf_ph))) { *why = "damaged ELF header"; return -1; }
    for (u32 i = 0; i < hh.phnum; i++) sg[i] = (seg_t){ ph[i].type, ph[i].offset, ph[i].vaddr, ph[i].filesz, ph[i].memsz };
    nseg = hh.phnum; entry_addr = hh.entry;
#endif

#if defined(__aarch64__) || defined(__arm__)
    u32 top = native_ram_top();
#else
#ifdef AS_NO_IO
    u32 ram = 122u << 20;                                   /* the test harness maps 48 MiB at NATIVE_BASE */
#else
    u32 ram = hal_mem_total_mib() << 20;
#endif
    u32 top = ram ? ram - (2u << 20) : 0;
#endif
    u32 end = NATIVE_BASE;
    for (u32 i = 0; i < nseg; i++) {
        const seg_t *p = &sg[i];
        if (p->type != 1) continue;
        if (p->vaddr < NATIVE_BASE || p->vaddr + p->memsz < p->vaddr) { *why = "program is not linked at the native base address"; return -1; }
        if (p->offset + p->filesz > len || p->filesz > p->memsz) { *why = "damaged ELF segment"; return -1; }
        if (p->vaddr + p->memsz > end) end = p->vaddr + p->memsz;
    }
    end = (end + 4095u) & ~4095u;
    if (!top || top <= NATIVE_BASE || end + NATIVE_STACK + (1u << 20) > top) { *why = "not enough memory (native programs need more RAM: start the machine with more memory)"; return -1; }
#ifndef AS_NO_IO
    if ((u32)(size_t)_end > NATIVE_BASE) { *why = "kernel image overlaps the native area"; return -1; }
#endif
    for (u32 i = 0; i < nseg; i++) {
        const seg_t *p = &sg[i];
        if (p->type != 1) continue;
        if (rd(rr, p->offset, (void *)(size_t)p->vaddr, p->filesz)) { *why = "read error while loading"; return -1; }
        for (u32 z = p->filesz; z < p->memsz; z++) ((u8 *)(size_t)p->vaddr)[z] = 0;
    }
#if defined(__aarch64__) || defined(__arm__)
    native_sync_icache(NATIVE_BASE, end - NATIVE_BASE);
#endif
    u32 stack_top = top & ~15u;
#if defined(__x86_64__)
    api.version = AS_API_VERSION;
    api.heap = end;
    api.heap_size = top - NATIVE_STACK - end;
    for (int i = 0; i < 23; i++) api.fn[i] = native_thunks[i];
    int rc;
    native_busy = 1;
    rc = native_enter64(entry_addr, &api, args ? args : "", stack_top);
    native_busy = 0;
    sgui_close();
    return rc;
#else
    api = api_table_proto;
    api.heap = (void *)(size_t)end;
    api.heap_size = top - NATIVE_STACK - end;
    int (*entry)(const as_api *, const char *) = (int (*)(const as_api *, const char *))(size_t)entry_addr;
    int rc;
    native_busy = 1;
#if defined(__aarch64__)
    {
        register unsigned long long x0 __asm__("x0") = (unsigned long long)(size_t)&api;
        register unsigned long long x1 __asm__("x1") = (unsigned long long)(size_t)(args ? args : "");
        __asm__ volatile(
            "mov x19, sp\n mov sp, %2\n blr %3\n mov sp, x19\n"
            : "+r"(x0) : "r"(x1), "r"((unsigned long long)stack_top), "r"((unsigned long long)(size_t)entry)
            : "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x30",
              "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31", "memory", "cc");
        rc = (int)x0;
    }
#elif defined(__arm__)
    {
        register u32 r0 __asm__("r0") = (u32)(size_t)&api;
        register u32 r1 __asm__("r1") = (u32)(size_t)(args ? args : "");
        __asm__ volatile(
            "mov r4, sp\n mov sp, %2\n blx %3\n mov sp, r4\n"
            : "+r"(r0) : "r"(r1), "r"(stack_top), "r"((u32)(size_t)entry)
            : "r2", "r3", "r4", "r12", "lr", "q0", "q1", "q2", "q3", "memory", "cc");
        rc = (int)r0;
    }
#else
    __asm__ volatile(
        "movl %%esp, %%esi\n"
        "movl %1, %%esp\n"
        "pushl %3\n"
        "pushl %2\n"
        "call *%4\n"
        "movl %%esi, %%esp\n"
        : "=a"(rc)
        : "r"(stack_top), "r"(&api), "r"(args ? args : ""), "r"(entry)
        : "esi", "ecx", "edx", "memory", "cc");
#endif
    native_busy = 0;
    sgui_close();
    return rc;
#endif
}
