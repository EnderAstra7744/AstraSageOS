/* The smallest native AstraSageOS program: prints text and, on a desktop, opens a window and reacts to keys. */
#include "as_api.h"

static void itoa10(int v, char *o)
{
    char t[12]; int n = 0, neg = v < 0;
    unsigned u = neg ? (unsigned)-v : (unsigned)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (neg) *o++ = '-';
    while (n) *o++ = t[--n];
    *o = 0;
}

int as_main(const as_api *api, const char *args)
{
    api->puts("Hello from a native C program!\n");
    char *h = (char *)api->heap;
    h[0] = 'o'; h[1] = 'k'; h[2] = 0;                       /* the heap is usable */
    api->puts("heap: "); api->puts(h); api->puts("\n");
    if (!api->win_open("Native C", 320, 200)) { api->puts("(no desktop: window skipped)\n"); return 0; }
    api->win_clear(0x102030);
    api->win_text(16, 16, "Native C program", 0xFFFFFF, 2);
    api->win_text(16, 48, args && *args ? args : "no arguments", 0xA0FFA0, 1);
    api->win_rect(16, 80, 120, 40, 0x30A030);
    api->win_show();
    for (int i = 0; i < 400; i++) {
        int k = api->win_wait(50);
        if (k == -1) return 0;
        if (k > 0 && k < 1000) {
            char b[16]; itoa10(k, b);
            api->win_rect(16, 140, 200, 20, 0x102030);
            api->win_text(16, 140, "key ", 0xFFFFFF, 1);
            api->win_text(48, 140, b, 0xFFFF60, 1);
            api->win_show();
            if (k == 27) break;
        }
    }
    api->win_close();
    return 0;
}
