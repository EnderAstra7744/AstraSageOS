/* COM1 serial console: lets the terminal run in the host terminal (qemu -nographic)
 * with the host's real 256-colour support, anchored at the top-left corner. */
#include "astra.h"
#include "io.h"
#include "gui.h"

#define COM1 0x3F8

/* 1 when a 16550 really answers at COM1. A missing port reads back 0xFF everywhere, which would look
 * like "data waiting" forever and swallow every PS/2 key and mouse byte (qemu -serial none, Limbo). */
static int uart_ok;

void serial_init(void)
{
    outb(COM1 + 7, 0xA5);
    uart_ok = (inb(COM1 + 7) == 0xA5) && (inb(COM1 + 5) != 0xFF);
    if (!uart_ok) return;
    outb(COM1 + 1, 0x00);   /* no interrupts */
    outb(COM1 + 3, 0x80);   /* DLAB on */
    outb(COM1 + 0, 0x01);   /* divisor 1 -> 115200 baud */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);   /* 8N1, DLAB off */
    outb(COM1 + 2, 0xC7);   /* FIFO on */
    outb(COM1 + 4, 0x03);   /* DTR + RTS */
}

static void tx(char c)
{
    if (!uart_ok) return;
    for (int i = 0; i < 100000 && !(inb(COM1 + 5) & 0x20); i++) { }
    outb(COM1, (u8)c);
}

void serial_putc(char c)
{
    if (c == '\n') tx('\r');
    if (c == '\b') { tx('\b'); tx(' '); tx('\b'); return; }
    tx(c);
}

/* Raw byte from COM1, or -1 when nothing is waiting. */
int serial_getb_nb(void)
{
    if (!uart_ok) return -1;
    if (inb(COM1 + 5) & 1) return inb(COM1);
    return -1;
}

int serial_pending(void)
{
    return uart_ok && (inb(COM1 + 5) & 1);
}

static void tx_str(const char *s) { while (*s) tx(*s++); }

/*
 * Asks the host terminal for its size: park the cursor far off-screen, then request
 * a cursor position report (ESC[6n). The terminal answers ESC[<rows>;<cols>R.
 * Without a terminal on the other end (graphical QEMU) nothing answers; that is
 * remembered so the wait only ever happens once.
 */
int hal_term_size(u32 *cols, u32 *rows)
{
    static int probed, ok;
    static u32 c = 80, r = 25;
    if (!uart_ok && !con_gui_active()) { *cols = c; *rows = r; return 0; }

    if (con_gui_active()) {                              /* graphical terminal: the window decides */
        int gc, gr;
        con_gui_size(&gc, &gr);
        *cols = (u32)gc; *rows = (u32)gr;
        return 1;
    }

    if (!probed || ok) {
        tx_str("\033[s\033[999;999H\033[6n\033[u");
        int state = 0;
        u32 a = 0, b = 0;
        int got = 0;
        for (long spins = 0; spins < 3000000L && !got; spins++) {
            int ch = serial_getb_nb();
            if (ch < 0) continue;
            switch (state) {
            case 0: if (ch == 0x1B) state = 1; break;
            case 1: state = (ch == '[') ? 2 : 0; a = b = 0; break;
            case 2:
                if (ch >= '0' && ch <= '9') a = a * 10 + (u32)(ch - '0');
                else if (ch == ';') state = 3;
                else state = 0;
                break;
            case 3:
                if (ch >= '0' && ch <= '9') {
                    b = b * 10 + (u32)(ch - '0');
                } else {
                    if (ch == 'R') got = 1;
                    state = 0;
                }
                break;
            }
        }
        probed = 1;
        ok = got && a >= 5 && b >= 20 && a < 500 && b < 500;
        if (ok) { r = a; c = b; }
    }
    *cols = c; *rows = r;
    return ok;
}

/* 1 when a terminal answers on the serial line (qemu -nographic in a real terminal). */
int serial_terminal_present(void)
{
    u32 c, r;
    return hal_term_size(&c, &r);
}
