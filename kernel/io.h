#ifndef AS_IO_H
#define AS_IO_H
#include "astra.h"

#ifdef AS_NO_IO      /* GUI test harness: port I/O is not available in user space */
#ifdef __cplusplus
extern "C" u8 as_test_inb(u16 p);
#else
extern u8 as_test_inb(u16 p);                 /* the harness plays the 8042 keyboard controller */
#endif
static inline void outb(u16 p, u8 v)  { (void)p; (void)v; }
static inline u8   inb(u16 p)         { return as_test_inb(p); }
static inline void outw(u16 p, u16 v) { (void)p; (void)v; }
static inline u16  inw(u16 p)         { (void)p; return 0; }
static inline void outl(u16 p, u32 v) { (void)p; (void)v; }
static inline u32  inl(u16 p)         { (void)p; return 0; }
#elif !defined(__i386__) && !defined(__x86_64__)   /* ARM and others: there is no port space (drivers use MMIO) */
static inline void outb(u16 p, u8 v)  { (void)p; (void)v; }
static inline u8   inb(u16 p)         { (void)p; return 0; }
static inline void outw(u16 p, u16 v) { (void)p; (void)v; }
static inline u16  inw(u16 p)         { (void)p; return 0; }
static inline void outl(u16 p, u32 v) { (void)p; (void)v; }
static inline u32  inl(u16 p)         { (void)p; return 0; }
#else

static inline void outb(u16 port, u8 v)
{
    __asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}

static inline u8 inb(u16 port)
{
    u8 v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void outw(u16 port, u16 v)
{
    __asm__ volatile("outw %0, %1" : : "a"(v), "Nd"(port));
}

static inline u16 inw(u16 port)
{
    u16 v;
    __asm__ volatile("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline void outl(u16 port, u32 v)
{
    __asm__ volatile("outl %0, %1" : : "a"(v), "Nd"(port));
}

static inline u32 inl(u16 port)
{
    u32 v;
    __asm__ volatile("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
#endif   /* AS_NO_IO */

/* spin-wait hint */
#if defined(__i386__) || defined(__x86_64__)
#define cpu_relax() __asm__ volatile("pause")
#elif defined(__aarch64__) || defined(__arm__)
#define cpu_relax() __asm__ volatile("yield")
#else
#define cpu_relax() __asm__ volatile("" ::: "memory")
#endif
#endif
