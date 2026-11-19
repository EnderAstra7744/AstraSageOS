/* test program for the compatibility-mode bridge (arch/x86_64/compat.S): 32-bit, linked at NATIVE_BASE */
#include "../../native/as_api.h"
int as_main(const as_api *api, const char *args)
{
    register unsigned a __asm__("esi") = 0x11223344u;     /* esi/edi must survive API calls (cdecl callee-saved) */
    register unsigned b __asm__("edi") = 0x55667788u;
    api->puts("hello from 32-bit");
    int ok = 1;
    for (int i = 0; i < 3; i++) {
        api->win_text(1, 2, args, 3, 4);                  /* 5 arguments */
        __asm__ volatile("" : "+r"(a), "+r"(b));
    }
    unsigned len = 0, ho = 0, bo = 0, bl = 0; int st = 0;
    int r = api->http_fetch("url", (unsigned char *)api->heap, 4096, &len, &ho, &bo, &st);   /* 7 arguments */
    if (r != 1234) ok = 0;
    if (len != 11 || ho != 22 || bo != 33 || st != 44) ok = 0;
    {
        unsigned q1 = 0, q2 = 0, q3 = 0; int q4 = 0;
        int rr = api->http_request("POST", "u2", "H: v\r\n", "data", 4, (unsigned char *)api->heap, 8192, &q1, &q2, &q3, &q4);   /* 11 arguments */
        if (rr != 99 || q1 != 1 || q2 != 2 || q3 != 3 || q4 != 4) ok = 0;
    }
    (void)bl;
    if (a != 0x11223344u || b != 0x55667788u) ok = 0;
    if (api->ms() != 777u) ok = 0;
    char *h = (char *)api->heap;
    h[0] = 'x';
    return ok ? 42 : -1;
}
