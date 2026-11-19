/* AstraSageOS ARM32 full kernel: same start-up as kernel/kmain.c, on the QEMU virt machine (desktop on ramfb). */
#include "../../kernel/astra.h"
#include "../../gui/gui.h"

void hw_mmu_enable(void);
void hw_early(u32 dtb);
void hw_pci_report(void);

void arm32_main(u32 dtb)
{
    hw_mmu_enable();
    hw_early(dtb);
    bl_add(BL_OK, "MMU on: identity map, caches enabled");
    bl_add(BL_OK, "Device tree: %u MiB of RAM", hal_mem_total_mib());
    bl_add(BL_INFO, "CPU: %s", hal_cpu_name());
    con_init();
    bl_add(BL_OK, "Text console ready");
    ast_system_init();
    int gui = video_init(0, 0);
    hw_pci_report();
    if (gui) bl_add(BL_OK, "Framebuffer: %ux%u, %u bits per pixel (%s)", video_get()->w, video_get()->h, video_get()->bpp, video_get()->how);
    else bl_add(BL_WARN, "Graphics not used: %s", video_reason());
    if (gui) {
        if (mouse_init()) bl_add(BL_OK, "virtio-input found (keyboard / pointer)");
        else bl_add(BL_WARN, "no virtio-input device (USB or PS/2 input as Limbo offers it is not supported yet): keyboard and pointer will not work");
        ui_boot_splash();
        if (ui_start()) {
            ui_boot_splash_finish();
            if (!ui_boot_lock()) ui_first_apps();
            ui_run();                       /* never returns */
        }
        bl_add(BL_ERR, "graphics start failed");
    }
    kprintf("AstraSageOS ARM32: no screen, text console on the serial line.\n");
    ast_session();
}
