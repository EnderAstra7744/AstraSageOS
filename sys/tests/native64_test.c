/* host test of arch/x86_64/compat.S: runs a real 32-bit program in compatibility mode through the same thunks the kernel uses
 * (Linux selectors: CS64 0x33, CS32 0x23, DS32 0x2b). Build and run: python3 sys/tests/run_native64.py */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include "../../native/as_api.h"

typedef struct { uint32_t version, heap, heap_size, fn[23]; } as_api32;
extern const uint32_t native_thunks[23];
int native_enter64(uint32_t entry, const as_api32 *api, const char *args, uint32_t stack_top);

static int calls_puts, calls_text, calls_fetch;
static void t_puts(const char *s) { if (!strcmp(s, "hello from 32-bit")) calls_puts++; }
static void t_text(int x, int y, const char *s, unsigned rgb, int scale) { if (x == 1 && y == 2 && !strcmp(s, "arg text") && rgb == 3 && scale == 4) calls_text++; }
static int t_fetch(const char *url, unsigned char *buf, unsigned cap, unsigned *a, unsigned *b, unsigned *c, int *d)
{
    if (strcmp(url, "url") || cap != 4096 || !buf) return -1;
    *a = 11; *b = 22; *c = 33; *d = 44; calls_fetch++;
    return 1234;
}
static int calls_req;
static int t_req(const char *m, const char *url, const char *h, const void *body, unsigned bl, unsigned char *buf, unsigned cap, unsigned *a, unsigned *b, unsigned *c, int *d)
{
    if (strcmp(m, "POST") || strcmp(url, "u2") || strcmp(h, "H: v\r\n") || strcmp(body, "data") || bl != 4 || cap != 8192 || !buf) return -1;
    *a = 1; *b = 2; *c = 3; *d = 4; calls_req++;
    return 99;
}
static unsigned t_ms(void) { return 777; }
static void nop(void) {}
const void *const native_api_fns[23] = { (void *)t_puts, nop, nop, nop, nop, nop, (void *)t_text, nop, nop, nop, nop, nop, nop,
    (void *)t_ms, nop, nop, nop, nop, (void *)t_fetch, nop, nop, nop, (void *)t_req };

int main(int argc, char **argv)
{
    if (argc < 2) return 2;
    FILE *f = fopen(argv[1], "rb"); if (!f) return 2;
    static unsigned char img[1 << 20];
    size_t n = fread(img, 1, sizeof img, f); fclose(f);
    unsigned entry = *(unsigned *)(img + 24), phoff = *(unsigned *)(img + 28);
    int phnum = *(unsigned short *)(img + 44);
    void *mem = mmap((void *)NATIVE_BASE, 8u << 20, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (mem != (void *)NATIVE_BASE) { perror("mmap"); return 2; }
    unsigned end = NATIVE_BASE;
    for (int i = 0; i < phnum; i++) {
        unsigned *p = (unsigned *)(img + phoff + i * 32);
        if (p[0] != 1) continue;
        memcpy((void *)(size_t)p[2], img + p[1], p[4]);
        if (p[2] + p[5] > end) end = p[2] + p[5];
    }
    end = (end + 4095) & ~4095u;
    uint32_t stack_top = NATIVE_BASE + (8u << 20) - 64;
    static as_api32 api;
    api.version = AS_API_VERSION; api.heap = end; api.heap_size = 1u << 20;
    for (int i = 0; i < 23; i++) api.fn[i] = native_thunks[i];
    static char args[] = "arg text";
    if ((size_t)args > 0xFFFFFFFFu || (size_t)&api > 0xFFFFFFFFu) { puts("static data above 4 GiB"); return 2; }
    int rc = native_enter64(entry, &api, args, stack_top);
    printf("rc=%d puts=%d text=%d fetch=%d req=%d\n", rc, calls_puts, calls_text, calls_fetch, calls_req);
    return (rc == 42 && calls_puts == 1 && calls_text == 3 && calls_fetch == 1 && calls_req == 1) ? 0 : 1;
}
