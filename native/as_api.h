/*
 * AstraSageOS native (C / C++) program interface, version 1.
 * A native package ships a 32-bit ELF linked at NATIVE_BASE. The kernel copies its segments there, clears .bss,
 * switches to a private stack and calls   int as_main(const as_api *api, const char *args).
 * Everything the program may touch goes through this table: there is no libc and no system call gate (yet) -
 * programs run in kernel mode, without memory protection, so a faulty program can bring the whole OS down.
 */
#ifndef AS_API_H
#define AS_API_H

#if defined(__aarch64__) || defined(__arm__)
#define NATIVE_BASE    0x48000000u     /* ARM (QEMU virt, RAM from 0x40000000): 128 MiB into RAM, above the kernel image (ends near 75 MiB) */
#else
#define NATIVE_BASE    0x05000000u     /* 80 MiB: above the kernel image (ends near 67 MiB) */
#endif
#define AS_API_VERSION 6u

typedef struct as_api {
    unsigned version;
    void    *heap;                     /* free memory for the program */
    unsigned heap_size;
    void   (*puts)(const char *s);                                   /* text console / capture */
    int    (*win_open)(const char *title, int w, int h);             /* 0 when there is no desktop */
    void   (*win_close)(void);
    void   (*win_clear)(unsigned rgb);
    void   (*win_rect)(int x, int y, int w, int h, unsigned rgb);
    void   (*win_pixel)(int x, int y, unsigned rgb);
    void   (*win_text)(int x, int y, const char *s, unsigned rgb, int scale);
    void   (*win_show)(void);
    int    (*win_wait)(int ms);                                      /* key code, 1000 click, 0 timeout, -1 closed */
    void   (*win_mouse)(int *x, int *y);
    int    (*file_read)(const char *path, char *out, int cap);       /* length or <0 */
    int    (*file_put)(const char *path, const char *text, int append);
    int    (*http_get)(const char *url, unsigned char *buf, unsigned cap, unsigned *len, int *status);
    unsigned (*ms)(void);                                            /* monotonic milliseconds */
    unsigned (*epoch)(void);                                         /* seconds since 1970-01-01 UTC (clock is local time, see notes) */
    int    (*win_event)(int *ev, int ms);                            /* ev[4] = type, x, y, code; 1 event, 0 timeout, -1 closed */
    void   (*win_blit)(const unsigned *src, int x, int y, int w, int h, int stride);
    int    (*win_size)(int *w, int *h);
    int    (*http_fetch)(const char *url, unsigned char *buf, unsigned cap, unsigned *hdr_len, unsigned *body_off, unsigned *body_len, int *status);
    /* version 4 */
    int    (*exec)(const char *cmd, char *out, int cap);             /* runs an AST command (as the signed-in user), output captured; <0 = not allowed here */
    int    (*aspi)(const char *args, char *out, int cap);            /* runs "aspi <args>" (list, info Name ...), output captured */
    /* version 5 */
    int    (*file_write)(const char *path, const void *data, unsigned len);   /* binary-safe; 0 ok, -2 larger than a file can be (4096 bytes), <0 other errors */
    /* version 6 */
    int    (*http_request)(const char *method, const char *url, const char *req_headers, const void *body, unsigned body_len,
                           unsigned char *buf, unsigned cap, unsigned *hdr_len, unsigned *body_off, unsigned *body_len_out, int *status);
                                                                     /* any method, extra request headers ("Name: value\r\n"...) and a body; 1 = ok */
} as_api;

/* win_event types */
#define AS_EV_KEY   1
#define AS_EV_MOVE  2
#define AS_EV_DOWN  3
#define AS_EV_UP    4
#define AS_EV_CLOSE 5

#endif
