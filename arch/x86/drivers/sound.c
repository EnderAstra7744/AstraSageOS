/* Sound: a Sound Blaster 16 driver (the card QEMU/Limbo/Bochs call "sb16") and an Intel AC97 driver (the card
 * they call "AC97" / "intel-ac97"), both polled - no interrupts. SB16 is tried first, then AC97.
 * It only plays the two short window sounds, which are synthesised here from integer maths (no sample files,
 * no floating point): a rising two-note chime when a window opens, the same notes falling when one closes.
 * Without an sb16 card (or with the volume at 0) nothing is played and nothing breaks. */
#include "astra.h"
#include "io.h"
#include "../../../sys/synth.inc"

static const char *card = "none";
#define SB 0x220
#define RATE 22222u                    /* what time constant 211 really gives: 1e6 / (256 - 211) */
#define NS 8000

static u8 dma_buf[8192] __attribute__((aligned(65536), section(".dmabuf")));   /* below 16 MiB, never crosses a 64 KiB line */
static int present = -1, requested, started;

int snd_present(void) { return present > 0; }
int snd_requested(void) { return requested; }
int snd_started(void) { return started; }
const char *snd_card(void) { return card; }

static int dsp_write(u8 v)
{
    for (int i = 0; i < 100000; i++) if (!(inb(SB + 0xC) & 0x80)) { outb(SB + 0xC, v); return 1; }
    return 0;
}

static int detect(void)
{
    outb(SB + 6, 1);
    for (volatile int i = 0; i < 2000; i++) inb(SB + 6);
    outb(SB + 6, 0);
    for (int i = 0; i < 200000; i++) if ((inb(SB + 0xE) & 0x80) && inb(SB + 0xA) == 0xAA) {
        outb(SB + 4, 0x22); outb(SB + 5, 0xFF);        /* mixer: master volume full - the level is applied to the samples */
        outb(SB + 4, 0x04); outb(SB + 5, 0xFF);        /* voice volume full */
        dsp_write(0xD1);                               /* speaker on */
        return 1;
    }
    return 0;
}

/* ---- Intel AC97 (82801AA, PCI 8086:2415): bus-master DMA from one buffer-descriptor entry, 48 kHz 16-bit stereo ---- */
u32 pci_cfg_r(u32 bus, u32 dev, u32 off);
void pci_cfg_w(u32 bus, u32 dev, u32 off, u32 v);
#define AC_RATE 48000u
#define AC_FRAMES 17280u                                 /* 0.36 s */
static s16 ac_buf[AC_FRAMES * 2] __attribute__((aligned(16)));
static struct { u32 addr; u16 len; u16 flags; } ac_bdl[32] __attribute__((aligned(8)));
static u16 nam, nabm;

static int ac97_detect(void)
{
    for (u32 dev = 0; dev < 32; dev++) {
        u32 id = pci_cfg_r(0, dev, 0);
        if ((id & 0xFFFF) != 0x8086 || (id >> 16) != 0x2415) continue;
        u32 b0 = pci_cfg_r(0, dev, 0x10), b1 = pci_cfg_r(0, dev, 0x14);
        if (!(b0 & 1) || !(b1 & 1)) return 0;
        nam = (u16)(b0 & 0xFFFC); nabm = (u16)(b1 & 0xFFFC);
        pci_cfg_w(0, dev, 4, (pci_cfg_r(0, dev, 4) & 0xFFFF) | 5);          /* I/O space + bus master */
        outl(nabm + 0x2C, 0x02);                                             /* cold reset released */
        int ready = 0;
        for (int i = 0; i < 200000 && !ready; i++) ready = inl(nabm + 0x30) & 0x100;   /* primary codec ready */
        if (!ready) return 0;
        outw(nam + 0x00, 0);                                                 /* mixer reset */
        outw(nam + 0x02, 0x0000);                                            /* master volume: full, unmuted */
        outw(nam + 0x18, 0x0808);                                            /* PCM out volume: 0 dB */
        return 1;
    }
    return 0;
}

static void ac97_start(void)
{
    outb(nabm + 0x1B, 0);                                                    /* stop, then reset the PCM out box */
    outb(nabm + 0x1B, 0x02);
    for (int i = 0; i < 100000 && (inb(nabm + 0x1B) & 2); i++) ;
    ac_bdl[0].addr = (u32)(unsigned long)ac_buf;
    ac_bdl[0].len = (u16)(AC_FRAMES * 2);                                    /* counted in 16-bit samples */
    ac_bdl[0].flags = 0x8000;                                                /* interrupt on completion (unused) */
    outl(nabm + 0x10, (u32)(unsigned long)ac_bdl);
    outb(nabm + 0x15, 0);                                                    /* last valid entry: 0 */
    outb(nabm + 0x1B, 0x01);                                                 /* run */
}


void snd_play(int kind)
{
    requested++;
    int vol = config_get()->volume;
    if (vol <= 0) return;
    if (present < 0) {
        u32 a = (u32)(unsigned long)dma_buf;
        present = 0;
        if (a + sizeof dma_buf <= 0x1000000u && (a & 0xFFFF) + NS <= 0x10000u && detect()) { present = 1; card = "Sound Blaster 16"; }
        else if (ac97_detect()) { present = 2; card = "Intel AC97"; }
    }
    if (present <= 0) return;
    if (present == 2) { synth(kind, vol, AC_RATE, AC_FRAMES, 0, ac_buf); ac97_start(); started++; return; }
    synth(kind, vol, RATE, NS, dma_buf, 0);
    u32 a = (u32)(unsigned long)dma_buf;
    dsp_write(0xD0);                                   /* stop whatever is still playing */
    outb(0x0A, 5);                                     /* mask DMA channel 1 */
    outb(0x0C, 0);
    outb(0x0B, 0x49);                                  /* single transfer, memory -> card, channel 1 */
    outb(0x02, (u8)a); outb(0x02, (u8)(a >> 8));
    outb(0x83, (u8)(a >> 16));
    outb(0x03, (u8)((NS - 1) & 0xFF)); outb(0x03, (u8)((NS - 1) >> 8));
    outb(0x0A, 1);
    dsp_write(0x40); dsp_write(211);                   /* sample rate */
    dsp_write(0x14); dsp_write((u8)((NS - 1) & 0xFF)); dsp_write((u8)((NS - 1) >> 8));   /* 8-bit single-cycle output */
    started++;
}
