/*
 * PS/2 mouse on the 8042 auxiliary port, polled (the kernel runs with interrupts off).
 * Three-byte packets: flags (bit 3 always set), dx, dy. Bytes arrive through kbd_hw_poll(),
 * which owns the shared output buffer at port 0x60 and hands every aux byte to mouse_feed().
 */
#include "gui.h"
#include "io.h"

#define QMAX 64
static mouse_ev q[QMAX];
static int qh, qt, present, mx, my, bw = 640, bh = 480, pi;
static u8  pkt[4], btn;
static int wheel_mode, wheel_acc;               /* IntelliMouse: four-byte packets, the fourth one carries the wheel */

static int wait_in(void)   { for (long i = 0; i < 200000; i++) if (!(inb(0x64) & 2)) return 1; return 0; }
static int wait_out(void)  { for (long i = 0; i < 200000; i++) if (inb(0x64) & 1) return 1; return 0; }
static void kcmd(u8 c)     { if (wait_in()) outb(0x64, c); }
static void kdata(u8 d)    { if (wait_in()) outb(0x60, d); }
static int  aux_cmd(u8 c)                         /* 1 = acknowledged */
{
    kcmd(0xD4); kdata(c);
    for (int i = 0; i < 4; i++) {
        if (!wait_out()) return 0;
        u8 r = inb(0x60);
        if (r == 0xFA) return 1;
        if (r == 0xFE) return 0;
    }
    return 0;
}

int mouse_init(void)
{
    present = 0; qh = qt = pi = 0;
    for (int i = 0; i < 16 && (inb(0x64) & 1); i++) (void)inb(0x60);     /* flush */
    kcmd(0xA8);                                    /* enable the aux device */
    kcmd(0x20);
    if (!wait_out()) return 0;
    u8 cfg = inb(0x60);
    cfg &= (u8)~0x20;                              /* aux clock on */
    kcmd(0x60); kdata(cfg);
    if (!aux_cmd(0xF6)) return 0;                  /* defaults */
    wheel_mode = 0; wheel_acc = 0;
    {   /* the magic sample-rate sequence 200, 100, 80 turns a wheel mouse into an IntelliMouse (device id 3) */
        int ok = aux_cmd(0xF3) && (kcmd(0xD4), kdata(200), wait_out() && inb(0x60) == 0xFA)
              && aux_cmd(0xF3) && (kcmd(0xD4), kdata(100), wait_out() && inb(0x60) == 0xFA)
              && aux_cmd(0xF3) && (kcmd(0xD4), kdata(80), wait_out() && inb(0x60) == 0xFA)
              && aux_cmd(0xF2);
        if (ok && wait_out() && inb(0x60) == 3) wheel_mode = 1;
    }
    if (!aux_cmd(0xF4)) return 0;                  /* streaming on */
    present = 1;
    return 1;
}

void mouse_set_bounds(int w, int h)
{
    bw = w; bh = h;
    if (mx >= w || mx == 0) mx = w / 2;
    if (my >= h || my == 0) my = h / 2;
}

static void push(void)
{
    int n = (qt + 1) % QMAX;
    if (n == qh) {                                 /* full: merge plain moves into the newest entry, never lose a click */
        int last = (qt + QMAX - 1) % QMAX;
        if (q[last].buttons == btn) { q[last].x = mx; q[last].y = my; }
        return;
    }
    q[qt].x = mx; q[qt].y = my; q[qt].buttons = btn;
    qt = n;
}

void mouse_feed(u8 b)
{
    if (pi == 0 && !(b & 8)) return;               /* out of sync: wait for a flags byte */
    pkt[pi++] = b;
    if (pi < (wheel_mode ? 4 : 3)) return;
    pi = 0;
    int dx = pkt[1] - ((pkt[0] << 4) & 0x100);
    int dy = pkt[2] - ((pkt[0] << 3) & 0x100);
    if (pkt[0] & 0xC0) { pi = 0; return; }                     /* overflow flags are never set by a real mouse or by QEMU: the stream is out of step */
                                                                /* (bytes were lost while the OS was busy) - drop it instead of jumping 255 pixels */
    if (wheel_mode) { int z = pkt[3] & 15; if (z & 8) z -= 16; wheel_acc += z; }
    u8 nb = pkt[0] & 7;
    int ox = mx, oy = my;
    mx += dx; my -= dy;
    if (mx < 0) mx = 0;
    if (my < 0) my = 0;
    if (mx > bw - 1) mx = bw - 1;
    if (my > bh - 1) my = bh - 1;
    if (mx != ox || my != oy || nb != btn) { btn = nb; push(); }
}

int mouse_pop(mouse_ev *ev)
{
    if (qh == qt) return 0;
    *ev = q[qh];
    qh = (qh + 1) % QMAX;
    return 1;
}

int mouse_wheel_take(void) { int z = wheel_acc; wheel_acc = 0; return z; }    /* notches since last call: > 0 = towards the user (scroll down) */

void mouse_position(int *x, int *y) { *x = mx; *y = my; }
int  mouse_present(void) { return present; }
