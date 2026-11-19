/*
 * libnsfb surface for AstraSageOS native programs (registered as the "able" surface):
 * the picture lives in RAM, nsfb_update() copies the changed area into the program's desktop window (api->win_blit)
 * and input comes from api->win_event.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libnsfb.h"
#include "libnsfb_plot.h"
#include "libnsfb_event.h"

#include "nsfb.h"
#include "surface.h"
#include "plot.h"

#include "as_api.h"
extern const as_api *__api;

#define UNUSED(x) ((x) = (x))

struct astra_priv { int open; int qn, qi; struct { int down; int key; } q[6]; };

static int astra_defaults(nsfb_t *nsfb)
{
    nsfb->width = 800;
    nsfb->height = 600;
    nsfb->format = NSFB_FMT_XRGB8888;
    select_plotters(nsfb);
    return 0;
}

static int astra_initialise(nsfb_t *nsfb)
{
    struct astra_priv *p = calloc(1, sizeof *p);
    if (p == NULL) return -1;
    if (__api == NULL || !__api->win_open("NetSurf", nsfb->width, nsfb->height)) { free(p); return -1; }
    int w = 0, h = 0;
    if (__api->win_size(&w, &h) > 0 && w > 0 && h > 0) { nsfb->width = w; nsfb->height = h; }
    select_plotters(nsfb);
    size_t size = ((size_t)nsfb->width * nsfb->height * nsfb->bpp) / 8;
    nsfb->ptr = calloc(1, size);
    if (nsfb->ptr == NULL) { free(p); return -1; }
    nsfb->linelen = (nsfb->width * nsfb->bpp) / 8;
    p->open = 1;
    nsfb->surface_priv = p;
    return 0;
}

static int astra_set_geometry(nsfb_t *nsfb, int width, int height, enum nsfb_format_e format)
{
    if (width > 0) nsfb->width = width;
    if (height > 0) nsfb->height = height;
    if (format != NSFB_FMT_ANY) nsfb->format = format;
    select_plotters(nsfb);
    if (nsfb->ptr != NULL) {
        uint8_t *np = realloc(nsfb->ptr, ((size_t)nsfb->width * nsfb->height * nsfb->bpp) / 8);
        if (np == NULL) return -1;
        nsfb->ptr = np;
    }
    nsfb->linelen = (nsfb->width * nsfb->bpp) / 8;
    return 0;
}

static int astra_finalise(nsfb_t *nsfb)
{
    if (__api != NULL && nsfb->surface_priv != NULL) __api->win_close();
    free(nsfb->surface_priv);
    nsfb->surface_priv = NULL;
    free(nsfb->ptr);
    nsfb->ptr = NULL;
    return 0;
}

static int astra_update(nsfb_t *nsfb, nsfb_bbox_t *box)
{
    int x0 = box->x0, y0 = box->y0, x1 = box->x1, y1 = box->y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > nsfb->width) x1 = nsfb->width;
    if (y1 > nsfb->height) y1 = nsfb->height;
    if (x1 <= x0 || y1 <= y0 || nsfb->ptr == NULL) return 0;
    const unsigned *base = (const unsigned *)(const void *)nsfb->ptr;
    __api->win_blit(base + (size_t)y0 * nsfb->width + x0, x0, y0, x1 - x0, y1 - y0, nsfb->width);
    __api->win_show();
    return 0;
}

static enum nsfb_key_code_e map_key(int c)
{
    switch (c) {
    case 8: case 127: return NSFB_KEY_BACKSPACE;
    case 9: return NSFB_KEY_TAB;
    case 10: case 13: return NSFB_KEY_RETURN;
    case 27: return NSFB_KEY_ESCAPE;
    case 0x101: return NSFB_KEY_UP;
    case 0x102: return NSFB_KEY_DOWN;
    case 0x103: return NSFB_KEY_LEFT;
    case 0x104: return NSFB_KEY_RIGHT;
    case 0x105: return NSFB_KEY_HOME;
    case 0x106: return NSFB_KEY_END;
    case 0x107: return NSFB_KEY_PAGEUP;
    case 0x108: return NSFB_KEY_PAGEDOWN;
    case 0x109: return NSFB_KEY_DELETE;
    }
    if (c >= 32 && c < 127) {
        return (enum nsfb_key_code_e)c;                /* the OS keyboard already delivers the typed character (patched fbtk uses it as is) */
    }
    return NSFB_KEY_UNKNOWN;
}

static void qpush(struct astra_priv *p, int down, int key) { p->q[p->qn].down = down; p->q[p->qn].key = key; p->qn++; }

static bool astra_input(nsfb_t *nsfb, nsfb_event_t *event, int timeout)
{
    struct astra_priv *p = nsfb->surface_priv;
    int ev[4];
    int r;
    if (p == NULL || !p->open) return false;
    if (p->qi < p->qn) {                                /* queued key events (press, release, Ctrl combinations) */
        event->type = p->q[p->qi].down ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP;
        event->value.keycode = (enum nsfb_key_code_e)p->q[p->qi].key;
        if (++p->qi == p->qn) p->qi = p->qn = 0;
        return true;
    }
    r = __api->win_event(ev, timeout < 0 ? 100 : timeout);
    if (r < 0) { event->type = NSFB_EVENT_CONTROL; event->value.controlcode = NSFB_CONTROL_QUIT; p->open = 0; return true; }
    if (r == 0) return false;
    switch (ev[0]) {
    case AS_EV_KEY: {
        int c = ev[3];
        p->qi = p->qn = 0;
        if (c >= 1 && c <= 26 && c != 8 && c != 9 && c != 10 && c != 13) {     /* Ctrl+letter arrives as 1..26 */
            qpush(p, 1, NSFB_KEY_LCTRL); qpush(p, 1, 'a' + c - 1); qpush(p, 0, 'a' + c - 1); qpush(p, 0, NSFB_KEY_LCTRL);
        } else {
            int k = map_key(c);
            if (k == NSFB_KEY_UNKNOWN) return false;
            qpush(p, 1, k); qpush(p, 0, k);
        }
        event->type = NSFB_EVENT_KEY_DOWN;
        event->value.keycode = (enum nsfb_key_code_e)p->q[0].key;
        p->qi = 1;
        return true;
    }
    case AS_EV_MOVE:
        event->type = NSFB_EVENT_MOVE_ABSOLUTE;
        event->value.vector.x = ev[1]; event->value.vector.y = ev[2]; event->value.vector.z = 0;
        return true;
    case AS_EV_DOWN:
    case AS_EV_UP:
        event->type = ev[0] == AS_EV_DOWN ? NSFB_EVENT_KEY_DOWN : NSFB_EVENT_KEY_UP;
        event->value.keycode = (ev[3] & 2) ? NSFB_KEY_MOUSE_3 : NSFB_KEY_MOUSE_1;
        return true;
    case AS_EV_CLOSE:
        event->type = NSFB_EVENT_CONTROL; event->value.controlcode = NSFB_CONTROL_QUIT; p->open = 0;
        return true;
    }
    return false;
}

static const nsfb_surface_rtns_t astra_rtns = {
    .defaults = astra_defaults,
    .initialise = astra_initialise,
    .finalise = astra_finalise,
    .input = astra_input,
    .geometry = astra_set_geometry,
    .update = astra_update,
};

NSFB_SURFACE_DEF(astra, NSFB_SURFACE_ABLE, &astra_rtns)
