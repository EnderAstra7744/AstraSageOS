# arch/ - one directory per CPU architecture

| Directory | State |
|---|---|
| `x86/` | The complete OS (i386, Multiboot): `boot.S`, `linker.ld`, `idt.c`, `drivers/` (ATA, e1000, rtl8139, video, mouse, sound). Tested (host tests + GUI harness), used in Limbo/QEMU. |
| `arm64/` | **Two kernels.** (1) `make arm64` = the small shell kernel below (this one has been run and works). (2) `make -C arch/arm64 full` = **the whole OS on ARM64** (`as_arm64_gui.bin`: desktop, AST, files, ASPI, AstraScript, TLS/network) - links, not run yet. First kernel: QEMU `virt` (aarch64): boot (EL2->EL1), PL011 serial, ramfb screen with the OS font, virtio-input keyboard, virtio-blk disk, timer, PSCI, and the shared core (kernel/kstring.c, kprintf.c, fs/fs.c, fs/persist.c: file system incl. big files and saving to the disk) behind a small AST shell (ls cat mkdir write rm save disk sector ...). **Compiles and links; has not been run** (no ARM64 emulator in the build sandbox). No MMU, no interrupts, no network / mouse / desktop yet. |
| `x86_64/` | First stage: Multiboot -> long mode (identity-mapped 1 GiB), serial + VGA text. Assembles/links; not run. |
| `arm32/` | First stage: ARMv7 `virt`, PL011 hello + echo. Compiles; not run. |

The shared core is 64-bit clean: the host tests (`make test`) build fs, persist, aspi, archive, users, AST... as 64-bit x86_64 code and pass.

Build the ARM64 kernel: `make arm64` (clang + ld.lld) -> `build/arm64/as_kernel_arm64.bin`.
Run (x86_64: `make x86_64`, arm32: `make arm32`, see their Makefiles):
`qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256 -kernel build/arm64/as_kernel_arm64.bin -device ramfb -device virtio-keyboard-device -serial stdio`

## What is still needed before the whole OS runs on ARM64
1. MMU: `mmu` command exists (identity map, caches) and is off by default until confirmed on real QEMU; GICv2/v3 + timer interrupts still missing.
2. virtio-net (ASPI / GitHub, TLS), virtio-gpu or ramfb for gui/ (gfx, astragui, apps are plain C but use fixed 32-bit buffers), virtio-tablet mouse.
3. The remaining x86-only pieces: ATA/e1000/rtl8139/video/mouse/sound drivers (have arm64 equivalents to write), native programs (`native/`) rebuilt for aarch64 (the loader is 32-bit ELF only), the 32-bit asm in kernel/native.c.

## ARM64 full kernel (as_arm64_gui.bin)
`make -C arch/arm64 full`, then
`qemu-system-aarch64 -M virt -cpu cortex-a72 -m 512 -kernel build/a64/as_arm64_gui.bin -device ramfb -device virtio-keyboard-device -device virtio-tablet-device -drive file=disk.img,if=none,format=raw,id=d -device virtio-blk-device,drive=d -netdev user,id=n -device virtio-net-device,netdev=n -serial stdio`
`arch/arm64/hw.c` is its machine layer: MMU, PL011, device tree (RAM size), ramfb screen, virtio-input keyboard + mouse/tablet, virtio-blk, virtio-net, PL031 clock, PSCI power.
Missing: sound, native (i386 ELF) programs such as NetSurf/FroggyCode, second disk (repo disk), interrupts (everything is polled).

## x86_64 full kernel (experimental)
`make -C arch/x86_64 full` then:
`qemu-system-x86_64 -kernel build/x64/as_kernel_x64.elf32 -m 256 -drive file=disk.img,format=raw -vga std -display none -vnc :0`
Untested (no emulator in the build sandbox). Native 32-bit programs are unavailable on this kernel.

## v0.9.19
- x86_64: 32-bit native programs (NetSurf, FroggyCode ...) now run on the 64-bit kernel in the CPU's compatibility mode (arch/x86_64/compat.S: far jumps between 32/64-bit code segments, API thunks). Tested on a Linux x86_64 host with a real 32-bit program (`python3 sys/tests/run_native64.py`, part of `make test`); not yet run inside the kernel itself.
- ARM32: full kernel (`make -C arch/arm32 full`, run: `qemu-system-arm -M virt -cpu cortex-a15 -m 512 -kernel as_arm32_gui.bin -device ramfb -device virtio-keyboard-device -device virtio-tablet-device -serial stdio`). Compiles and links; never run (no ARM emulator in the build sandbox).
