# AstraSageOS yol haritası (kararlaştırılan sıra)

Durum: Aşama 1 (ağ + ASPI'nin OS içinden indirmesi), Aşama 2 (aspi komutu, type/commands, AstraScript GUI arayüzü) ve Aşama 3/1 (FroggyCode) YAPILDI (0.9.3); gerisi (AstraStore, AstraBrowser, masaüstü ekrana sığma) bekliyor. Her aşama sonunda çalışan, test edilmiş bir sürüm teslim edilir.

## Aşama 0 – Bekleyen küçük iş
- Masaüstünün ekrana sığması: boşluğun nerede olduğu (yanlarda / alt-üst / pencerenin içinde) öğrenilince
  GRUB ve çekirdek çözünürlük seçimi düzeltilecek.

## Aşama 1 – Ağ altyapısı
1. Python sanal sunucu (paket deposu; GitHub yerine). `aspi_sync.py` bu sunucuya yönlendirilir.
2. Ağ kartı sürücüsü (RTL8139 veya E1000) + PCI taraması.
3. Ethernet, ARP, IPv4, ICMP (`ping`).
4. UDP, TCP.
5. HTTP istemcisi.
6. ASPI'nin paketi OS içinden sunucudan indirmesi.

## Aşama 2 – Paket sistemi
1. `aspi` komutu (doğrudan); `as aspi` takma ad olarak çalışmaya devam eder.
2. `ASPIManifest.json` içinde `type` (app / tool / library / data; yoksa library) ve `commands` alanları.
   Paketlerin uygulama olması zorunlu değil; tür arşivdeki alanla belirlenir.
3. AST, bilinmeyen komutları kurulu paketlerin `commands` listesinde arar (`as froggy` FroggyCode ile gelir).
4. AstraScript'e GUI arayüzü (pencere, piksel çizimi, tuş okuma).

## Aşama 3 – Uygulamalar
1. **FroggyCode** (ASPI paketi; Froggy mascot'u piksel haritalarıyla): dosya verme, konuşma, AST ve Dosya Gezgini'nden
   canlı komut/dosya oluşturma (yıkıcı işlemlerde onay). Kural/şablon tabanlı; dil modeli değil.
2. **AstraStore**: ASPI'nin grafik arayüzü (Yükle / Kaldır / Aç); ilk uygulama FroggyCode.

## Aşama 4 – AstraBrowser
1. **Yol A:** sunucu destekli tarayıcı (sunucu HTTPS/JS işini yapıp sade sayfa verir).
2. **Yol B:** yerel motor (libc katmanı + TLS + NetSurf türü motor; JS isteğe bağlı).

## Yapılmayacaklar / sınırlar
- Mozilla Firefox ve Chromium taşınmaz (Linux/Windows altyapısına bağlılar).
- Çoklu disk (D:, E:) ve USB hibrit ISO istenmedi; istenirse eklenir.

## Derleme altyapısı (yapıldı: çapraz derleme; OS içi derleyici bekliyor)
- Yapıldı: `toolchain/` — `as-cc` (gcc/clang/tcc arka uçları, C ve C++), `as-check.py`, aşama testleri; çıktılar gerçek yükleyiciyle çalıştırılarak denendi.
- Bekleyen (önkoşul): OS içi derleyici (tcc'nin yerel portu) için 4096 baytlık dosya sınırı ve 256 düğümlük dosya sistemi kalkmalı (gerçek blok dosya sistemi).

## Update "AST, File System, New Error System and More" (spec of 2026-11-05) - progress

Done: 15.1 (terminal text reflows on resize; deleting an ASPI app from the desktop uninstalls it, so every list agrees),
15.2 / 15.4 (apps and store downloads run as background jobs, no terminal window), 15.3 (start;/end; mandatory for every
.as file, template for every creation path, legacy str_* / file_* / ast_do builtins ported to Engine 2),
7 (menu language everywhere in the file explorer, GRUB in English, white "Welcome To AstraSageOS" -> "Welcome To GNU/GRUB"),
10 (NetSurf: PNG + JPEG decoding via zlib/libpng/libjpeg-turbo built for the OS; download support, files <= 4096 bytes),
14 (Up/Down history, Ctrl+C/U/W/Z/V/L in AST, Ctrl+C/V/Z/Y in the editor, mouse wheel = Up/Down).
Native programs now load at 80 MiB (NATIVE_BASE 0x05000000): the kernel image grew past 64 MiB.
All 16 items are now done (see the milestones below).

## Milestone: acs, root access, error screens (items 3-4 partly, 6)
- `acs <command>`: asks "[acs] Pls Enter your Password: " (masked); with the right password the command runs with root authority
  (`acs as newdir root:/mydir`, `acs aspi install <system-pkg>`). Files app: password dialog (M_PASS) for deleting root folders / adding files to root.
- `root:/path` prefix in AST reaches the real root outside S:~ (reading is free, changes need acs). System files are never deleted; root folders cannot be copied or renamed.
- ASPI package type `system`: install / remove need acs ("Permission denied" / "Izin Reddedildi").
- Error system in C++ (sys/errscreen.cpp): green Kernel Panic (halts, no reboot loop), yellow </> screen (5 s countdown, restart); CPU exceptions (kernel/idt.c) go to the panic screen.
- `as proc [<FILE>] [--sleep N]` runs /proc triggers (kernel_panic, heavy_load, cpu_fault); `as seal` / `as userapp` + /user/apps/*.asx (compiled+encrypted+sealed) checked at every sign-in; tampering -> yellow screen, item quarantined (/.term/log/quarantine).
- Tests: host 250/250, GUI 179/179, Bochs: panic via real ud2, yellow countdown + restart.
- Still open from this group: tabs (1), as custom colours (2), custom neofetch (3), Files "This PC" redesign (5), then 8, 9, 11, 12, 13, 16.

## Milestone: tabs, `as custom`, custom neofetch, new Explorer
- `as tab [--add/--remove] [NAME]`: up to 6 independent AST sessions (own console, history, state); hidden tabs keep running.
- `as custom [--tab_color/--line_color/--custom_input_color]` and `as custom --neofetch [c1,c2,c3] [PNG] [TXT] [INFO_COLOR]` (PNG/TXT logos under /bin/neofetch/).
- File Explorer: "This PC" overview (S: disk, RAM, Root, Home), vector icons, RAM bar, root-folder badges (root icon + red "!" for system folders).

## Milestone: boot log + password lock (item 8)
- sys/bootlog.c: every real start-up step (Multiboot, memory, CPU, IDT, ATA disk, asfs, settings file, font file, saved login, framebuffer, mouse, boot logo, wallpaper, icons, theme, desktop layout) is recorded with a level; the boot splash prints them top right (green ok / blue info / yellow warning / red error).
- After the first login (setup done) the graphical start asks for the password ("Welcome back" login window with the name filled in; "Other account" switches). Text/serial mode is unchanged.

## Milestone: graphics performance (item 9)
- Already in place: dirty-rectangle tracking, back buffer + partial present, cached icons/wallpaper/window sprites, drag background cache, all-static memory (no heap, nothing to leak).
- New: occlusion culling in wm_draw_all (a window fully behind another opaque one is not drawn); test counter wm_culled_count().
- Not possible here: GPU/OpenGL/Vulkan acceleration and vsync (no GPU driver; linear framebuffer only); idle HLT needs interrupts (PIT/PIC) which the kernel does not set up yet.

## Milestone: picture logo in the terminal
- terminal/console.c: picture layer (con_image_place): pixel pictures anchored to cell rows, scroll with the text, saved per tab, cleared by clear/resize.
- gui/app_ast.c renders them (bilinear scaling, transparent border cut-out for the logo) over blank cells; neofetch uses it in the graphical terminal for the default logo and PNG logos (text/3-colour/txt logos and text mode stay character art).

## Milestone: network managers (item 11)
- Stack already present: e1000, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP, HTTP, TLS 1.2. New: drives/rtl8139.c (Realtek RTL8139; written from the datasheet, NOT yet run in QEMU), nic_* picks the card found.
- New app "Network" (APP_NET, gui/app_net.c): Ethernet (card, MAC, link, address, DHCP / test buttons using the real commands), Wi-Fi and Bluetooth pages. `as net manager` opens it; `as wifi`, `as bluetooth` print adapter status.
- Wi-Fi / Bluetooth: PCI search only (pci_wireless_scan); no drivers (no 802.11 stack, no USB stack). Emulators have no radio.

## Milestone: desktop shell (spec item 12)
- gui/shell.c: black notch at the top centre with the live clock; click opens an animated panel (calendar with month arrows, big clock, weather from `as weather`).
- Tray at the top right: power (sleep / restart / shut down), network, sound (slider, mute, arrow keys). All open and close with animation.
- Network menu shows the real Ethernet state + DHCP button; Wi-Fi and Bluetooth say honestly that no adapter/driver exists (no radio in emulators, no 802.11/USB stack yet). The standalone Network app was removed.
- 9-dot app grid: first and permanent dock icon; lists built-in and ASPI apps over a dimmed desktop; right-click -> Add to desktop.
- Work area now starts below the 28 px top strip; tests updated (215/215).

## Milestone: window sounds (spec item 13)
- drives/sound.c: polled Sound Blaster 16 driver (QEMU/Limbo `-device sb16`, base 0x220, 8-bit DMA ch.1, no IRQ). Without an sb16 card it stays silent.
- Sounds are synthesised in integer maths at boot-free cost (no sample files): open = rising chime E5->B5, close = the same notes falling. Level follows the tray volume (0 = silent).
- wm_create plays the open sound, wm_destroy the close sound. Verified with a fake SB16 model (register sequence + waveform); no sound-card emulation exists in the Bochs build used here, so real playback needs Limbo with "Sound card: SB16".

## Milestone: themes, languages, Settings, account (spec item 16)
- Nine themes: White, Black, Green + Dark blue, Yellow, Red, Blue, Purple, Auro (aurora). The six new ones are derived from a base colour and two accents (gui/theme.c `derive`); welcome and Settings show live mini-desktop previews.
- Languages: English, Türkçe + Español, Русский, 简体中文. Texts come from sys/lang/*.tsv (run `python3 sys/lang/gen_lang.py` -> sys/lang_tab.h); a text without a row falls back to English (most terminal messages are still English in ES/RU/ZH). Fonts: `python3 font/ttf2fnt.py` adds Spanish accents, Cyrillic (DejaVu Sans Mono 13 px) and the Chinese characters used by the .tsv files (Noto Sans CJK SC, 16x16, two cells wide - SIL OFL).
- Settings app (APP_SETTINGS, white + grey gear icon, in the 9-dot menu, cannot be deleted): Appearance, Windows (frame / inside transparency 30..100 %), Language, Sound, Sign-in, Account (change name / password), About.
- Window transparency: gfx_snapshot / gfx_mix_snapshot in astragui.c draw_window; dragged windows keep it through their sprite; culling is off while windows are see-through.
- The Welcome app disappears after the setup (no desktop icon, not in the 9-dot menu, launch does nothing).
- `as account [-change --NAME <PASSWORD> <NEW_NAME> | --PASSWORD <PASSWORD> <NEW_PASSWORD>]`; passwords are masked and never enter the history. A rename also moves the home folder, the saved login and the desktop file shortcuts.

## v0.9 - shortcuts, wallpapers, full-screen login (Done)
- Keyboard: Ctrl+Alt+letter shortcuts (T terminal, F files, S settings, B wallpaper picker, G app grid, P store, K task manager, W close window, D show desktop, L lock), Alt+Tab, Alt+arrows (snap halves, maximise, restore/minimise). Listed in Settings > Shortcuts.
- 12 new wallpapers drawn by sys/tools/make_wallpapers.py (800x450, 8-bit, scaled smoothly): one per theme (picture number = theme number) + 4 more; the setting `wallpaper` (-1 = the theme's own). Picker (Ctrl+Alt+B): dimmed desktop, search field, gliding slanted cards. Settings > Wallpaper.
- The login is a full-screen layer (blurred column, big clock, date, name + password fields, shutdown/reboot) instead of a window; it owns pointer and keyboard.
- An existing account means the setup is done: the welcome app is removed by itself at start.
- Task manager (Ctrl+Alt+K): open windows with End task, memory/storage/uptime (no CPU meter: the kernel has none).
- Linker: the ISA DMA buffer of the sound card sits right after the code (below 16 MiB) however big the images get.
- Still open from the list: block-based filesystem, terminal messages in ES/RU/ZH, AC97 sound, notification centre, trash.

## v0.9.1 - speed and wallpapers
- Lag: shadows, fills, present and glyph drawing rewritten (profiled with callgrind); wallpaper scaling is now fixed-point (no double math); picker cards use a per-card column table.
- Wallpapers: all 11 pictures now come from the user's own images (assets/wallpapers_src, converted by sys/tools/make_wallpapers.py to .wpz + 320x180 thumbnails). My self-drawn pictures were removed.
- Still open: block-based filesystem, ES/RU/ZH terminal messages, AC97, notification center, trash.

## v0.9.6
- Wallpapers: Real-ESRGAN x4 (run in numpy, sys/tools/esrgan_numpy.py) on the small source pictures, then scaled to 1280x720; auro theme uses the galaxy picture.
- Screen is 1280x720 (1920x1080 made emulators on phones crawl).
- File system: 1024 nodes, files up to 256 KiB (arena + contiguous extents), disk format 4 with extent allocation; format 3 disks are read and upgraded on the next save. Editor / `as write` text limit is 16 KiB.
- Trash (S:/usrs/<name>/home/Trash): Delete moves there, Put back / Empty Trash in the explorer.
- Notification centre: bell in the tray (ui_note feeds it).
- Terminal: UTF-8 cells (Cyrillic, Chinese two-cell wide), terminal/`as` messages translated (sys/lang/terminal.tsv: es ru zh tr via TT()).
- Sound: Intel AC97 driver (48 kHz stereo, one BDL entry) after SB16. Untested on real emulated hardware.
- Still open: more `as` messages (net, aspi details) are English only; AC97 not verified in Limbo.

## v0.9.7 - big files, packages on GitHub
- Fix: the kernel image (.bss 86 MB) ended at 96 MiB, past NATIVE_BASE (80 MiB), so NetSurf / FroggyCode said "kernel image overlaps the native area". Max screen buffers are now 1366x768 (image ends ~76 MiB); sys/tools/check_end.py makes the build fail if it grows again.
- Big files: a file over 256 KiB (FS_STORE_MAX) lives on the disk only, in one contiguous extent, up to just under 4 GiB (disk format 4 unchanged: the record's start/secs hold the extent). API: fs_pread / fs_pwrite (offset based), fs_size_real, fs_is_big, fs_copy_data. fs_size()/fs_data() show a big file as empty, so whole-buffer code never reads half a file. Allocator manages up to 1 GiB (1 bit per sector), default disk is 64 MiB. Big files are written in place (not crash-safe like small files: only the table commit is atomic).
- Native programs are loaded straight from their file (native_run_node), segment by segment, not copied through RAM first.
- NetSurf and FroggyCode are no longer built into the OS image. `make repo` writes dist/aspi-package-sage/ (NetSurf-3.11.0.aspi, FroggyCode-2.0.0.aspi, README.md) to upload to github.com/EnderAstra7744/aspi-package-sage; `aspi install` downloads them (native.txt names the program, e.g. bin/netsurf.elf). Offline install needs a repo disk. Icons and store screenshots stay in the image.
- Tests: host test of big files (--bigtest), harness has a RAM disk and links stand-in packages (AS_NO_IO only).

## v0.9.8 - arch/ and loose ends
- arch/x86 now holds boot.S, linker.ld, idt.c and drivers/ (moved from drives/ and kernel/). arch/x86_64 (Multiboot -> long mode) and arch/arm32 (ARMv7 virt) first stages: compile only.
- arch/arm64: shared core (kstring, kprintf, fs, persist) linked in; shell has ls cat mkdir write rm save disk sector; virtio-blk. Not run.
- ASPI: archives downloaded from GitHub are kept in /.aspi/cache; with no network `aspi` offers the cached ones (needs a disk). `as net`, wifi, bluetooth and ASPI messages in 5 languages.
- Big files: extents that are moved or deleted are freed only after the next save commits (a crash before that keeps the old table valid). Overwriting inside a big file is still in place.

## v0.9.9 - ARM64 full kernel
- Shared code made portable: kernel/io.h (no port I/O off x86), gfx.c fill/copy loops, coro.c (aarch64 context switch), AstraScript setjmp (arch/arm64/jmp.S), cpu_relax(), sqrt.
- arch/arm64/hw.c + kmain.c: the desktop OS links for aarch64 (as_arm64_gui.bin, ~14 MB). Not run yet.

## v0.9.10 — x86_64 full kernel (experimental)
- `make -C arch/x86_64 full` → build/x64/as_kernel_x64.elf32: the whole OS (desktop, AST, ASPI, AstraScript, drivers) as a 64-bit kernel (Multiboot → long mode, 4 GiB identity map, SSE, 64-bit IDT → Kernel Panic screen, x86_64 coroutines).
- Not run on real hardware/emulator by the developer (no QEMU in the sandbox); coroutine switch is covered by the 303-check GUI harness on the 64-bit host.
- Native programs (NetSurf, FroggyCode) are 32-bit i386 ELF: they do NOT run on 64-bit kernels until rebuilt.
- The shipped ISO stays 32-bit i386 (tested). ARM32 full port: still stage 1.

## v0.9.11 — one ISO with all kernels
- `make iso` builds the x86_64 and ARM64 kernels too (if they compile) and puts them on the CD: GRUB menu entry "Start AstraSageOS 64-bit (x86_64, experimental)" (/boot/as_kernel_x64.elf) and /arm64/as_arm64_gui.bin (for qemu-system-aarch64 -kernel; ARM64 UEFI boot from the CD is not done).

## v0.9.12 — 32/64 question before the GRUB menu
- grub.cfg asks "32 or 64" before the menu (Enter = last choice, saved with save_env when the medium is writable; a CD is read-only so it is asked every time). 64 selects the x86_64 entry.

## v0.9.14 — 64-bit boot diagnosis
- User test v0.9.13: the 64-bit entry restarted GRUB (silent reset = triple fault). Fixed gfx.c rep stos/movs counts for 64-bit, IDT installed before kmain, progress squares + raw exception dump (arch/x86_64/README.md).

## v0.9.16 — 64-bit needs a 64-bit CPU
- User test v0.9.15: yellow+green squares, then reset = long-mode switch on a CPU without long mode (Limbo i386 machine). boot.S now checks CPUID 0x80000001 (red square + halt if missing); GRUB checks `cpuid -l` after "64" and falls back to 32-bit with an explanation.

## v0.9.17 — x86_64 works
- User confirmed the 64-bit kernel boots to the desktop in Limbo (CPU qemu64; qemu32 has no long mode). The progress squares are replaced by real boot-log lines; `ast info` shows the kernel architecture.

## v0.9.18 — `as arch`, automatic 32/64-bit choice
- New command `as arch` (kernel architecture, running mode, CPU 64-bit support, native-program support). hal_cpu_long_mode() via CPUID.
- GRUB no longer asks 32/64: a 64-bit CPU (`cpuid -l`) selects the 64-bit entry by default, otherwise 32-bit; both stay in the menu.

## v0.9.19 — native programs on x86_64, ARM32 full kernel
- x86_64 kernel runs the 32-bit native programs through compatibility mode (arch/x86_64/compat.S, GDT selector 24 = 32-bit code, selector 16 = flat 4 GiB data); API thunks convert cdecl stack arguments to System V registers (up to 7 args), run on the kernel stack and keep esi/edi. Host test: sys/tests/run_native64.py (heaven's-gate on Linux).
- ARM32 (ARMv7-A, QEMU virt, cortex-a15, hard-float VFP): arch/arm32/{boot_full.S,hw.c,kmain.c,jmp.S,rt.c,linker_full.ld}; MMU with 1 MiB sections, 64-bit division / double<->int64 helpers written in rt.c. Untested on any emulator.
- ARM64 native programs: still unavailable (they are 32-bit x86 ELF files).

## v0.9.20 — ARM64 runs
- User test (Termux QEMU, ELF kernel): ARM64 desktop boots, `as arch` works, virtio disk works after the queue fix.
- Fixes found by that test: UI/network clocks used x86 PIT ports (now the ARM generic timer via ui_ms_hook); virtio-blk queue of 8 entries broke the legacy ring layout (now 16).
- ARM kernels ship as ELF (QEMU's arm64 raw-Image loader rejected the 13 MB .bin in Limbo); run scripts in the zip.
- Still open: ARM32 first run, ARM sound / interrupts / second disk, native programs on ARM, ARM boot from the ISO (needs GRUB arm64-efi), NetSurf inside the 64-bit x86 kernel (compat mode, host-tested only).

## v0.9.21 — ARM: second disk
- ARM64/ARM32: up to two virtio-blk disks; the second one is drive 1 (ASPI repository disk), like the second IDE disk on x86. Add `-drive file=repo.img,if=none,format=raw,id=r -device virtio-blk-device,drive=r`.
- User test: ARM32 boots and works (confirmed); x86_64 had been confirmed earlier.

## v0.9.22
- The CD boots by itself on ARM64 UEFI machines: arch/arm64/efi/efistub.c (BOOTAA64.EFI) loads AS_ARM64.BIN to 0x40080000 and passes the DTB. Not run on real UEFI yet (no emulator in the build sandbox).

## v0.9.24
- Native programs on ARM64 and ARM32: kernel/native.c loads ELF64 (aarch64) / ELF32 (ARM) programs linked at 0x48000000, syncs the instruction cache and runs them on their own stack.
- native/libc ported to ARM (portable math, setjmp, memcpy; no long double on ARM64); build_arm.sh builds FroggyCode for ARM; ASPI picks name-arm64.elf / name-arm32.elf. NetSurf has no ARM build (its build tree is not in the repo).
- Never run on an ARM kernel here (no emulator): only compiled/linked and the math checked on the host.

## v0.9.25
- NetSurf for ARM64 and ARM32 (netsurf-arm64.elf / netsurf-arm32.elf in the NetSurf package): current NetSurf master from GitHub, own libnsfb surface (window, mouse, keyboard) and own http/https fetcher over the kernel's network stack; recipe in native/netsurf/arm/README.md.
- Not run on an ARM kernel yet. No PNG/JPEG (only BMP/GIF), no JavaScript, GET only.

## v0.9.26 - NetSurf (ARM) without the old limits
POST/multipart, cookies and HTTP auth (as_api v6 http_request), PNG and JPEG, JavaScript (Duktape with nsgenbind-generated DOM bindings), POSIX regex engine, dynamic page buffer, Ctrl shortcuts. Both ARM64 and ARM32 builds. Still untested on a real ARM kernel.
- Sound on ARM: arch/arm_snd.inc, a virtio-sound driver (QEMU `-audiodev ... -device virtio-sound-device,audiodev=a`) shared by arm64/arm32; the chimes now come from sys/synth.inc (also used by the x86 driver). Compile-checked only.
- NetSurf (ARM) now renders SVG (libsvgtiny with expat; XML_POOR_ENTROPY: the hash salt is not random).
- AS_VERSION is now 0.9.26 (it still said 0.9.7); tests and boot CD readme updated.
- Limbo + ARM stays open: it needs the exact device set Limbo gives the ARM machine (the kernel wants ramfb + virtio-mmio; Limbo most likely uses PCI devices).

## v0.9.27 - fixes from the Limbo test
- x86_64: "Multiboot magic is ffff8e00" / 0 MiB RAM: boot.S kept the magic in rsi across `call err_idt_init` (caller-saved) - now r12. RAM size also reads the Multiboot memory map and, failing that, the CMOS registers.
- aspi update / upgrade added. GitHub list: NetSurf was silently skipped (archive > 2 MiB); limit 3 MiB, per-processor archives (`Name-1.0-arm64.aspi`, `-arm32`, `-x86`) so a package no longer carries every CPU, and a skipped archive is reported.
- Screen tearing: all damaged rectangles are composed first and copied in one burst; the pointer's old+new box is one copy.
- ARM (Limbo "std" video): PCI ECAM scan + BAR assignment, Bochs VGA framebuffer (arch/arm_pci.inc), PCI device list in the boot log. No USB / PCI-virtio input yet.

## v0.9.28 - shell elements step aside
- The notch, the tray icons and the dock slide out of the screen (animated) while a window touches them and slide back when it leaves (shell.c dodge_*, astraui.c ddodge, astragui.c wm_window_hits). Windows may now be dragged over the dock strip.
- The ISO's arm64/ and arm32/ folders also carry the flat .bin kernels, always built fresh.

## v0.9.29 - neofetch as in asfetch
- The default neofetch logo is the ASCII art of asfetch again (the picture only appears for a PNG chosen with `as custom --neofetch`).
- Palette rows use the standard terminal colours in the graphical terminal (SGR 160-175, an AstraSage extension); the VGA text console can only show its 16 programmed colours, which are shared with the logo's greens.
- CPU line: "(N cores, 64-bit)", the model name is shortened to keep it visible.
