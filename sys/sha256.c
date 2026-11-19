/* SHA-256 (FIPS 180-4), small and dependency free. */
#include "astra.h"

static const u32 K[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void block(u32 h[8], const u8 *p)
{
    u32 w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (u32)p[i*4] << 24 | (u32)p[i*4+1] << 16 | (u32)p[i*4+2] << 8 | p[i*4+3];
    for (int i = 16; i < 64; i++) {
        u32 s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        u32 s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    u32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; i++) {
        u32 S1 = ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25);
        u32 ch = (e & f) ^ (~e & g);
        u32 t1 = hh + S1 + ch + K[i] + w[i];
        u32 S0 = ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22);
        u32 mj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

void sha256(const void *data, size_t len, u8 out[32])
{
    u32 h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
    const u8 *p = data;
    size_t n = len;
    while (n >= 64) { block(h, p); p += 64; n -= 64; }

    u8 tail[128];
    kmemset(tail, 0, sizeof tail);
    kmemcpy(tail, p, n);
    tail[n] = 0x80;
    size_t tl = (n < 56) ? 64 : 128;
    u32 bits_hi = (u32)(len >> 29), bits_lo = (u32)len << 3;   /* len < 2^29 here */
    tail[tl-8] = (u8)(bits_hi >> 24); tail[tl-7] = (u8)(bits_hi >> 16);
    tail[tl-6] = (u8)(bits_hi >> 8);  tail[tl-5] = (u8)bits_hi;
    tail[tl-4] = (u8)(bits_lo >> 24); tail[tl-3] = (u8)(bits_lo >> 16);
    tail[tl-2] = (u8)(bits_lo >> 8);  tail[tl-1] = (u8)bits_lo;
    block(h, tail);
    if (tl == 128) block(h, tail + 64);

    for (int i = 0; i < 8; i++) {
        out[i*4] = (u8)(h[i] >> 24); out[i*4+1] = (u8)(h[i] >> 16);
        out[i*4+2] = (u8)(h[i] >> 8); out[i*4+3] = (u8)h[i];
    }
}

void sha256_hex(const void *data, size_t len, char out[65])
{
    u8 d[32];
    sha256(data, len, d);
    for (int i = 0; i < 32; i++) {
        out[i*2]   = "0123456789abcdef"[d[i] >> 4];
        out[i*2+1] = "0123456789abcdef"[d[i] & 15];
    }
    out[64] = 0;
}
