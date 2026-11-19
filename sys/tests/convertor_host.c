/* Host driver for sys/aspi_convertor.cpp: convertor_host <kind: gz|xz> <in> <out.aspi>; prints the error text and exits 1 on failure. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "astra.h"

static u8 scratch[4u << 20];
u8 *arc_scratch(u32 *cap) { *cap = sizeof scratch; return scratch; }
int aspi_convert(int kind, const u8 *src, u32 len, const u8 **out, u32 *outLen, const char **why);

int main(int argc, char **argv)
{
    if (argc < 4) return 2;
    FILE *f = fopen(argv[2], "rb");
    if (!f) return 2;
    static u8 in[3u << 20];
    u32 n = (u32)fread(in, 1, sizeof in, f);
    fclose(f);
    const u8 *out = 0; u32 ol = 0; const char *why = "";
    int r = aspi_convert(!strcmp(argv[1], "gz") ? ARC_TGZ : ARC_TXZ, in, n, &out, &ol, &why);
    if (r < 0) { printf("ERR %s\n", why); return 1; }
    f = fopen(argv[3], "wb");
    fwrite(out, 1, ol, f);
    fclose(f);
    printf("OK %u\n", ol);
    return 0;
}
