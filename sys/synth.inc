/* the two window chimes, integer maths only; included by every sound driver (x86 sound.c, ARM virtio-snd) */
#ifndef AS_SYNTH_INC
#define AS_SYNTH_INC
#ifndef S16_DEFINED
#define S16_DEFINED
typedef short s16;
#endif
static int sin1024(u32 i)                              /* sine of i/1024 of a turn, scaled to -256..256 */
{
    i &= 1023;
    int neg = i >= 512; if (neg) i -= 512;
    int p = (int)(i * (512 - i));
    int v = 256 * 16 * p / (5 * 262144 - 4 * p);       /* Bhaskara's approximation, error < 0.2 % */
    return neg ? -v : v;
}

/* One chime. rate/ns: sample rate and count; out8 (unsigned 8-bit mono) or out16 (signed 16-bit stereo) is filled. */
static void synth(int kind, int vol, u32 rate, u32 ns, u8 *out8, s16 *out16)
{
    static const u16 hz_open[2] = { 659, 988 }, hz_close[2] = { 988, 659 };
    const u16 *hz = kind ? hz_close : hz_open;
    u32 ph[2] = { 0, 0 }, env[2] = { 0, 0 };
    u32 step[2] = { (u32)hz[0] * 65536u / rate, (u32)hz[1] * 65536u / rate };
    u32 lastv = 0xFFFFFFFFu;
    for (u32 n = 0; n < ns; n++) {
        u32 v = n * 22222u / rate;                     /* the envelope runs on the 22.2 kHz timeline of the original tuning */
        int tick = v != lastv; lastv = v;
        if (v == 3300) env[1] = 1;                     /* the second note starts a hair later */
        if (n == 0) env[0] = 1;
        int mix = 0;
        for (int k = 0; k < 2; k++) {
            if (!env[k]) continue;
            if (tick) { if (v < 3300u * (u32)k + 110) env[k] += 65535 / 110; else env[k] -= env[k] >> 11; }
            if (env[k] > 65535) env[k] = 65535;
            ph[k] += step[k];
            int s = sin1024(ph[k] >> 6) * 3 + sin1024(ph[k] >> 5);        /* a little 2nd harmonic: bell-like */
            mix += (int)(((s >> 2) * (int)(env[k] >> 8)) >> 9);
        }
        mix = mix * vol / 100 * 3 / 4;
        if (out8) { int o = 128 + mix; out8[n] = (u8)(o < 1 ? 1 : o > 255 ? 255 : o); }
        else { int o = mix * 128; if (o > 32767) o = 32767; if (o < -32767) o = -32767; out16[n * 2] = out16[n * 2 + 1] = (s16)o; }
    }
}
#endif
