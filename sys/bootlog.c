/*
 * Boot log: every step the system really takes while it starts (found hardware, files read, settings applied)
 * is recorded here with a level. The boot splash shows it in the top right corner: green = done, blue = information,
 * yellow = warning, red = error. Nothing in this list is invented: each line is written by the code that did the step.
 */
#include "astra.h"

#define BL_MAX 96
#define BL_LEN 92

static char lines[BL_MAX][BL_LEN];
static u8   lvl[BL_MAX];
static int  count;

void bl_add(int level, const char *fmt, ...)
{
    va_list ap;
    char tmp[BL_LEN];
    va_start(ap, fmt);
    kvsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (count == BL_MAX) {                                  /* full: the oldest line makes room */
        for (int i = 1; i < BL_MAX; i++) { kmemcpy(lines[i - 1], lines[i], BL_LEN); lvl[i - 1] = lvl[i]; }
        count--;
    }
    kmemcpy(lines[count], tmp, BL_LEN);
    lvl[count] = (u8)level;
    count++;
}

int         bl_count(void)       { return count; }
const char *bl_text(int i)       { return i >= 0 && i < count ? lines[i] : ""; }
int         bl_level(int i)      { return i >= 0 && i < count ? lvl[i] : 0; }
