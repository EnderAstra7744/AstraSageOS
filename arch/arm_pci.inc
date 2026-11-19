/* PCI for the ARM kernels (QEMU virt: the generic PCIe host bridge, ECAM at 0x3f000000, 32-bit memory window from 0x10000000).
 * Used for the "std" video card Limbo (Android) offers on its ARM machine: a Bochs/QEMU VGA (1234:1111) with a linear framebuffer.
 * Without firmware nobody has given the BARs an address, so they are assigned here. Included by arm64/hw.c and arm32/hw.c.
 * Written without an ARM machine at hand: compile-checked only. */
#define PCI_ECAM     0x3f000000UL
#define PCI_MMIO     0x10000000UL
#define PCI_MMIO_END 0x3eff0000UL
static u32 pci_r(u32 dev, u32 off) { return r32(PCI_ECAM + ((u64)dev << 15) + off); }
static void pci_w(u32 dev, u32 off, u32 v) { w32(PCI_ECAM + ((u64)dev << 15) + off, v); }
static struct { u8 dev; u16 ven, id; u32 cls; u32 bar[6]; } pci_dev[12];
static int pci_n = -1;
static u32 pci_next = PCI_MMIO;

static void pci_scan(void)
{
    if (pci_n >= 0) return;
    pci_n = 0;
    for (u32 d = 0; d < 32 && pci_n < 12; d++) {
        u32 id = pci_r(d, 0);
        if ((id & 0xFFFF) == 0xFFFF || id == 0) continue;
        u32 cls = pci_r(d, 8) >> 8;
        int i = pci_n++;
        pci_dev[i].dev = (u8)d; pci_dev[i].ven = (u16)id; pci_dev[i].id = (u16)(id >> 16); pci_dev[i].cls = cls;
        for (int b = 0; b < 6; b++) pci_dev[i].bar[b] = 0;
        if (cls >> 8 == 0x0604 || cls >> 8 == 0x0600) continue;                     /* the host bridge has nothing to map */
        for (int b = 0; b < 6; b++) {
            u32 off = 0x10 + 4u * (u32)b, save = pci_r(d, off);
            pci_w(d, off, 0xFFFFFFFFu);
            u32 sz = pci_r(d, off);
            pci_w(d, off, save);
            if (!sz || sz == 0xFFFFFFFFu || (sz & 1)) continue;                      /* unused or an I/O BAR */
            int is64 = ((sz >> 1) & 3) == 2;
            u32 size = ~(sz & ~0xFu) + 1;
            if (!size) { if (is64) b++; continue; }
            pci_next = (pci_next + size - 1) & ~(size - 1);
            if ((u64)pci_next + size > PCI_MMIO_END) { if (is64) b++; continue; }
            pci_w(d, off, pci_next | (sz & 0xF));
            if (is64) { pci_w(d, off + 4, 0); }
            pci_dev[i].bar[b] = pci_next;
            pci_next += size;
            if (is64) b++;
        }
        pci_w(d, 4, pci_r(d, 4) | 6);                                                /* memory space + bus master */
    }
}

/* the Bochs / QEMU "std" VGA: dispi registers at BAR2 + 0x500, framebuffer = BAR0 */
static int pci_vga_find(u32 *fb, u32 *regs)
{
    pci_scan();
    for (int i = 0; i < pci_n; i++)
        if (pci_dev[i].ven == 0x1234 && pci_dev[i].id == 0x1111 && pci_dev[i].bar[0] && pci_dev[i].bar[2]) { *fb = pci_dev[i].bar[0]; *regs = pci_dev[i].bar[2]; return 1; }
    return 0;
}
static int pci_vga_set(u32 regs, u32 w, u32 h)
{
    volatile u16 *r = (volatile u16 *)(size_t)(regs + 0x500);
    if ((r[0] & 0xFFF0) != 0xB0C0) return 0;
    r[4] = 0; r[1] = (u16)w; r[2] = (u16)h; r[3] = 32; r[6] = (u16)w; r[7] = (u16)h; r[8] = 0; r[9] = 0;
    r[4] = 0x41;                                                                        /* enabled + linear framebuffer */
    return 1;
}
static void pci_report(void)
{
    pci_scan();
    if (!pci_n) { bl_add(BL_INFO, "PCI: no devices answer (this machine has no PCIe bus)"); return; }
    for (int i = 0; i < pci_n; i++)
        bl_add(BL_INFO, "PCI slot %u: %x:%x class %x%s", (u32)pci_dev[i].dev, (u32)pci_dev[i].ven, (u32)pci_dev[i].id, pci_dev[i].cls >> 8,
               pci_dev[i].ven == 0x1234 ? " (VGA)" : pci_dev[i].ven == 0x1af4 ? " (virtio)" : "");
}
