/* as_kernel entry point, called from boot.S with the Multiboot handoff. */
#include "astra.h"
#include "gui.h"

void sysinfo_init(u32 mbi_flags, u32 mem_upper, u32 mmap_len, u32 mmap_addr);

struct mb_info { u32 flags, mem_lower, mem_upper, boot_dev, cmdline, mods_count, mods_addr, syms[4], mmap_length, mmap_addr; };

/*
 * Graphics is used when a framebuffer exists and somebody can see it. A terminal answering on the
 * serial line (qemu -nographic) means the user works in text; kernel command line "gui" or "text"
 * overrides the guess. Without graphics the original VGA/serial console runs unchanged.
 */
static const char *gui_reason;

static int want_gui(u32 magic, const void *mbi)
{
    if (!video_init(magic, mbi)) { gui_reason = video_reason(); return 0; }
    if (video_cmdline_has("text")) { gui_reason = "kernel command line says text"; return 0; }
    if (!video_cmdline_has("gui") && serial_terminal_present()) {
        gui_reason = "a terminal is attached to the serial port (add 'gui' to the kernel command line to force graphics)";
        return 0;
    }
    return 1;
}

void kmain(u32 magic, const struct mb_info *mbi)
{
    int ok = (magic == 0x2BADB002u);
    if (ok) sysinfo_init(mbi->flags, mbi->mem_upper, mbi->mmap_length, mbi->mmap_addr);
    else sysinfo_init(0, 0, 0, 0);
    if (ok) bl_add(BL_OK, "Multiboot handoff accepted (magic 0x2BADB002)");
    else bl_add(BL_ERR, "Multiboot magic is %x, expected 2badb002", magic);
#if defined(__x86_64__)
    bl_add(BL_OK, "64-bit CPU mode (long mode) checked with CPUID");
    bl_add(BL_OK, "64-bit: GDT loaded, paging on (first 4 GiB identity-mapped), long mode active");
#endif
    bl_add(hal_mem_total_mib() >= 128 ? BL_OK : BL_WARN, "Memory: %u MiB reported by the boot loader", hal_mem_total_mib());
    bl_add(BL_INFO, "CPU: %s", hal_cpu_name());
    con_init();
    bl_add(BL_OK, "Text console ready");
    err_idt_init();                         /* CPU exceptions show the Kernel Panic screen */
    bl_add(BL_OK, "IDT loaded: CPU exception handlers (Kernel Panic screen)");
#if defined(__x86_64__)
    bl_add(BL_OK, "64-bit kernel: 32-bit native programs (NetSurf, FroggyCode) run in compatibility mode");
#endif
    ast_system_init();
    int gui = want_gui(magic, mbi);
    if (gui) bl_add(BL_OK, "Framebuffer: %ux%u, %u bits per pixel", video_get()->w, video_get()->h, video_get()->bpp);
    else bl_add(BL_INFO, "Graphics not used: %s", gui_reason ? gui_reason : "unknown");
    if (!gui && video_get()) {              /* the card was already switched to graphics for the probe: undo it,
                                               otherwise the text console would be invisible (black screen) */
        video_text_mode();
        con_init();
    }
    video_stage(1);
    if (gui) {
        if (mouse_init()) bl_add(BL_OK, "PS/2 mouse initialised");
        else bl_add(BL_WARN, "PS/2 mouse not found: keyboard only");
        video_stage(2);
        ui_boot_splash();
        video_stage(3);
        if (ui_start()) {
            video_stage(4);
            ui_boot_splash_finish();
            if (!ui_boot_lock())           /* after the first login the password is asked at every start */
                ui_first_apps();           /* Welcome app on first start, otherwise per saved policy */
            video_stage(5);
            ui_run();                       /* never returns */
        }
        video_text_mode();                  /* graphics failed after the mode switch: back to text */
        con_init();
        gui_reason = "graphics start failed";
    }
    if (gui_reason) kprintf("\033[38;5;245mGraphics: not used (%s).\033[0m\n", gui_reason);
    ast_session();                   /* text console; never returns */
}
