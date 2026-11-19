/*
 * Keyboard input: PS/2 (scancode set 1, polled, US layout) plus the serial console.
 *
 * kbd_getkey() returns plain ASCII, Ctrl+letter as 1..26, or a KEY_* code for the
 * cursor keys. Serial escape sequences (ESC [ A, ESC [ 3 ~ ...) are decoded here.
 */
#include "astra.h"
#include "io.h"
#include "gui.h"

static const char normal[58] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b', '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']', '\n', 0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
};
static const char shifted[58] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+', '\b', '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}', '\n', 0,
    'A','S','D','F','G','H','J','K','L',':','"','~', 0, '|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0, ' '
};

static int shift, caps, ctrl, alt, ext;

/* Waits briefly for the next serial byte (used to tell a lone Esc from a sequence). */
static int serial_next(void)
{
    for (int i = 0; i < 400000; i++) {
        int b = serial_getb_nb();
        if (b >= 0) return b;
    }
    return -1;
}

static int csi_key(int num, int fin)
{
    switch (fin) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    case 'H': return KEY_HOME;
    case 'F': return KEY_END;
    case '~':
        switch (num) {
        case 1: case 7: return KEY_HOME;
        case 4: case 8: return KEY_END;
        case 3:         return KEY_DEL;
        case 5:         return KEY_PGUP;
        case 6:         return KEY_PGDN;
        }
    }
    return -1;
}

/* Decodes one serial byte (possibly the start of an escape sequence); -1 = swallow. */
static int serial_decode(int b)
{
    if (b == '\r') return '\n';
    if (b == 0x7F) return '\b';
    if (b != 0x1B) return b;

    int c = serial_next();
    if (c < 0) return 27;                       /* a lone Esc */
    if (c != '[' && c != 'O') return -1;
    int num = 0, fin;
    for (;;) {
        fin = serial_next();
        if (fin < 0) return -1;
        if (fin >= '0' && fin <= '9') { num = num * 10 + (fin - '0'); continue; }
        if (fin == ';') { num = 0; continue; }
        break;
    }
    return csi_key(num, fin);
}

static int ps2_ext_key(u8 sc)
{
    switch (sc) {
    case 0x48: return KEY_UP;    case 0x50: return KEY_DOWN;
    case 0x4B: return KEY_LEFT;  case 0x4D: return KEY_RIGHT;
    case 0x47: return KEY_HOME;  case 0x4F: return KEY_END;
    case 0x49: return KEY_PGUP;  case 0x51: return KEY_PGDN;
    case 0x53: return KEY_DEL;   case 0x1C: return '\n';
    }
    return -1;
}

/*
 * One non-blocking step: a decoded key, or -1 when nothing (complete) is waiting.
 * The 8042 output buffer is shared: bytes flagged "aux" belong to the PS/2 mouse and go to
 * mouse_feed(); everything else is a keyboard scancode.
 */
#if defined(__aarch64__) || defined(__arm__)   /* no PS/2 here: arch/arm64/hw.c and arch/arm32/hw.c reads virtio-input and queues keys */
int hw_input_pending(void);
int hw_input_poll(void);
#define HW_PENDING() hw_input_pending()
#else
#define HW_PENDING() (inb(0x64) & 1)
#endif

int kbd_hw_pending(void)
{
    return serial_pending() || HW_PENDING();
}

int kbd_hw_poll(void)
{
#if defined(__aarch64__) || defined(__arm__)
    { int hk = hw_input_poll(); if (hk >= 0) return hk; }
#endif
    int b = serial_getb_nb();
    if (b >= 0) {
        int k = serial_decode(b);
        if (k >= 0) return k;
        return -1;
    }
    u8 st = inb(0x64);
    if (!(st & 1)) return -1;
    u8 sc = inb(0x60);
    if (st & 0x20) { mouse_feed(sc); return -1; }
    if (sc == 0xE0) { ext = 1; return -1; }

    int release = sc & 0x80;
    sc &= 0x7F;
    if (ext) {
        ext = 0;
        if (sc == 0x1D) { ctrl = !release; return -1; }      /* right Ctrl */
        if (sc == 0x38) { alt = !release; return -1; }       /* right Alt (AltGr) */
        if (release) return -1;
        if (alt) {                                           /* Alt + arrow: window snapping */
            if (sc == 0x4B) return KEY_ALT_LEFT;
            if (sc == 0x4D) return KEY_ALT_RIGHT;
            if (sc == 0x48) return KEY_ALT_UP;
            if (sc == 0x50) return KEY_ALT_DOWN;
        }
        return ps2_ext_key(sc);
    }
    if (sc == 0x2A || sc == 0x36) { shift = !release; return -1; }
    if (sc == 0x1D) { ctrl = !release; return -1; }
    if (sc == 0x38) { alt = !release; return -1; }
    if (release) return -1;
    if (alt && sc == 0x0F) return KEY_ALT_TAB;
    if (sc == 0x3A) { caps = !caps; return -1; }
    if (sc >= sizeof normal) return -1;

    char c = normal[sc];
    if (!c) return -1;
    int is_alpha = (c >= 'a' && c <= 'z');
    if (ctrl && alt && is_alpha) return KEY_HOT + (c - 'a');
    if (ctrl && is_alpha) return c - 'a' + 1;
    int up = is_alpha ? (shift ^ caps) : shift;
    return up ? shifted[sc] : c;
}

/* Text mode: keys come straight from the hardware. The graphical desktop swaps these two hooks
 * so the AST window receives its own keys and the shell yields while it waits. */
int  (*kbd_source)(void) = kbd_hw_poll;
void (*kbd_idle)(void);

int kbd_poll_nb(void) { return kbd_source(); }              /* -1 = no key now */
void kbd_wait_hint(void) { if (kbd_idle) kbd_idle(); }       /* let the desktop run (graphical shell) */

int kbd_getkey(void)
{
    for (;;) {
        int k = kbd_source();
        if (k >= 0) return k;
        if (kbd_idle) kbd_idle();
        else cpu_relax();
    }
}

int kbd_getc(void)
{
    for (;;) {
        int k = kbd_getkey();
        if (k == '\n' || k == '\b' || (k >= 32 && k < 127)) return k;
    }
}
