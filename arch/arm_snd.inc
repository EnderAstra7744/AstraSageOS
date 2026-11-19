/* Sound for the ARM kernels: a virtio-sound device (QEMU: -audiodev <backend>,id=a -device virtio-sound-device,audiodev=a).
 * Included by arm64/hw.c and arm32/hw.c (they share vdev/vq_open/put_desc). Polled, no interrupts.
 * The two window chimes come from sys/synth.inc. Written without an ARM machine at hand: compile-checked only. */
#include "../sys/synth.inc"
#define SND_RATE   22050u
#define SND_FRAMES 8192u                         /* 0.37 s */
#define SND_CHUNK  4096u                         /* bytes per tx buffer = one period */
#define SND_NCH    (SND_FRAMES * 4 / SND_CHUNK)  /* 8 chains of 2 descriptors fill the 16-entry queue */
static vdev snd; static int snd_state = -1, snd_req, snd_ok_n;
static u8 snd_cq[8192] __attribute__((aligned(4096))), snd_eq[8192] __attribute__((aligned(4096)));
static u8 snd_tq[8192] __attribute__((aligned(4096))), snd_rq[8192] __attribute__((aligned(4096)));
static struct { u32 w[6]; } snd_cmd __attribute__((aligned(16)));
static volatile u32 snd_resp __attribute__((aligned(16)));
static struct { u32 stream; u8 pcm[SND_CHUNK]; } snd_buf[SND_NCH] __attribute__((aligned(16)));
static volatile struct { u32 status, latency; } snd_st[SND_NCH] __attribute__((aligned(16)));
static u16 snd_cidx, snd_tidx;

static int snd_ctl(u32 words, u32 *w)
{
    for (u32 i = 0; i < words; i++) snd_cmd.w[i] = w[i];
    snd_resp = 0xFFFFFFFFu;
    put_desc(snd_cq, 0, (u64)(size_t)&snd_cmd, words * 4, 1, 1);
    put_desc(snd_cq, 1, (u64)(size_t)&snd_resp, 4, 2, 0);
    u16 *avail = (u16 *)(snd_cq + 256), *used = (u16 *)(snd_cq + 4096);
    u16 want = (u16)(used[1] + 1);
    avail[2 + (snd_cidx % QN)] = 0; snd_cidx++;
    dmb(); avail[1] = snd_cidx; dmb();
    w32(snd.base + 0x50, 0);
    for (u64 t = 0; used[1] != want; t++) { dmb(); if (t > 200000000UL) return -1; }
    return snd_resp == 0 ? 0 : -2;
}

static int snd_detect(void)
{
    for (int s = 0; s < 32; s++) {
        u64 b = VIRTIO + (u64)s * 0x200;
        if (r32(b) != 0x74726976u || r32(b + 8) != 25) continue;
        if (!vdev_open(&snd, b, 0)) continue;
        if (!vq_open(&snd, 0, snd_cq, QN) || !vq_open(&snd, 1, snd_eq, QN) || !vq_open(&snd, 2, snd_tq, QN) || !vq_open(&snd, 3, snd_rq, QN)) continue;
        vdev_go(&snd);
        if (r32(b + 0x100 + 4) < 1) continue;                       /* config: jacks, streams, chmaps - need one stream */
        u32 sp[6] = { 0x0101, 0, 32768, SND_CHUNK, 0, 2 | (5u << 8) | (4u << 16) };   /* SET_PARAMS: stream 0, 2 ch, S16, 22050 Hz */
        u32 pr[2] = { 0x0102, 0 }, st[2] = { 0x0104, 0 };
        if (snd_ctl(6, sp) || snd_ctl(2, pr) || snd_ctl(2, st)) continue;
        return 1;
    }
    return 0;
}

void snd_play(int kind)
{
    snd_req++;
    int vol = config_get()->volume;
    if (vol <= 0) return;
    if (snd_state < 0) snd_state = snd_detect();
    if (snd_state <= 0) return;
    u16 *avail = (u16 *)(snd_tq + 256), *used = (u16 *)(snd_tq + 4096);
    if (used[1] != snd_tidx) return;                                 /* the last chime is still playing: skip this one */
    static s16 pcm[SND_FRAMES * 2];
    synth(kind, vol, SND_RATE, SND_FRAMES, 0, pcm);
    u8 *src = (u8 *)pcm;
    for (u32 c = 0; c < SND_NCH; c++) {
        snd_buf[c].stream = 0;
        for (u32 i = 0; i < SND_CHUNK; i++) snd_buf[c].pcm[i] = src[c * SND_CHUNK + i];
        snd_st[c].status = 0xFFFFFFFFu;
        put_desc(snd_tq, (int)(2 * c), (u64)(size_t)&snd_buf[c], 4 + SND_CHUNK, 1, (u16)(2 * c + 1));
        put_desc(snd_tq, (int)(2 * c + 1), (u64)(size_t)&snd_st[c], 8, 2, 0);
        avail[2 + (snd_tidx % QN)] = (u16)(2 * c); snd_tidx++;
    }
    dmb(); avail[1] = snd_tidx; dmb();
    w32(snd.base + 0x50, 2);                                         /* notify queue 2 (tx) */
    snd_ok_n++;
}
int  snd_present(void) { return snd_state > 0; }
const char *snd_card(void) { return snd_state > 0 ? "virtio-sound" : "none"; }
int  snd_requested(void) { return snd_req; }
int  snd_started(void) { return snd_ok_n; }
