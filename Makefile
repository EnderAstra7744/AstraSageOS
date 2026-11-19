# AstraSageOS build. Needs a 32-bit capable gcc (or clang + lld, e.g. Termux) and GNU ld / ld.lld.
#
# Source tree (same layout as the S: drive):
#   arch/x86/  boot.S, linker.ld (the i386 port; arch/arm64 is the ARM64 port, see arch/README.md)
#   kernel/    kmain, strings, printf, hardware info, core headers
#   terminal/  AST: shell, console, keyboard, serial, editor, neofetch
#   fs/        file tree + persistence
#   arch/x86/drivers/  ATA, e1000, rtl8139, video, mouse, sound (x86 hardware)
#   sys/       users, permissions, SHA-256, AstraScript interpreter, tests/, tools/
#   font/      font.ttf, bitmap font and the converter
#   usrs/      example files for users
HOSTCC  = gcc
QEMU    = qemu-system-i386
CC      = gcc
CXX     = g++
LD      = ld

# Auto-detect clang (e.g. Termux on ARM): cross-compile to i386 with clang + lld.
ifneq ($(shell $(CC) --version 2>/dev/null | grep -ci clang),0)
  CC      = clang --target=i386-pc-none-elf
  CXX     = clang++ --target=i386-pc-none-elf
  LD      = ld.lld
  ARCHF   = -mno-sse -mno-mmx
  ASFLAGS =
else
  ARCHF   = -m32 -fno-tree-loop-distribute-patterns -mno-sse -mno-mmx
  ASFLAGS = -m32
endif

INC     = -Ikernel -Iterminal -Ifont -Igui -IAstraScript/include
CFLAGS  = $(ARCHF) -std=gnu11 -O2 -Wall -Wextra -ffreestanding -fno-builtin \
          -fno-pic -fno-pie -fno-stack-protector $(INC)
CXXFLAGS = $(ARCHF) -std=gnu++17 -O2 -Wall -Wextra -ffreestanding -fno-builtin -fno-exceptions -fno-rtti \
          -fno-threadsafe-statics -fno-use-cxa-atexit -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables $(INC)
LDFLAGS = -m elf_i386 -T arch/x86/linker.ld -nostdlib -z noexecstack --build-id=none \
          $(shell $(LD) --help 2>/dev/null | grep -q no-warn-rwx-segments && echo --no-warn-rwx-segments)

ASENGINE = $(addprefix AstraScript/src/,builtins.c check.c classes.c collections.c engine.c errors.c fileobj.c functions.c interpreter.c lexer.c memory.c modules.c parser.c strings.c types.c lib_data.c)
AstraScript/src/lib_data.c: $(wildcard AstraScript/include/lib/*.ash) AstraScript/tools/embed_lib.py
	python3 AstraScript/tools/embed_lib.py

SRC  = kernel/kmain.c arch/x86/idt.c sys/proc.c sys/png.c sys/bootlog.c kernel/kstring.c kernel/kprintf.c kernel/sysinfo.c \
       terminal/console.c terminal/keyboard.c terminal/serial.c terminal/ast.c terminal/neofetch.c terminal/editor.c \
       fs/fs.c fs/persist.c arch/x86/drivers/ata.c arch/x86/drivers/e1000.c arch/x86/drivers/rtl8139.c sys/net.c sys/crypto.c sys/x509.c sys/tls.c \
       sys/users.c sys/config.c sys/perm.c sys/sha256.c sys/astrascript.c sys/as_host_kernel.c $(ASENGINE) \
       kernel/native.c sys/inflate.c sys/lzma.c sys/archive.c sys/aspi.c \
       arch/x86/drivers/video.c arch/x86/drivers/mouse.c arch/x86/drivers/sound.c gui/gfx.c gui/theme.c gui/bmp.c gui/coro.c gui/astragui.c gui/astraui.c gui/app_ast.c gui/app_files.c gui/app_welcome.c gui/app_settings.c gui/app_login.c gui/wallpaper.c gui/app_tasks.c gui/app_script.c gui/app_store.c gui/shell.c
ASM  = gui/assets.S
CXXSRC = sys/aspi_convertor.cpp sys/errscreen.cpp
OBJ  = $(patsubst %.c,build/%.o,$(SRC)) $(patsubst %.cpp,build/%.o,$(CXXSRC)) build/boot/boot.o build/gui/assets.o
HDR  = kernel/astra.h kernel/io.h terminal/logo.h font/font8x16.h gui/gui.h
KERNEL = build/as_kernel.elf

all: $(KERNEL)

build/%.o: %.c $(HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

# C++ (aspi_convertor.cpp): freestanding - no libstdc++, no exceptions, no RTTI, no global constructors
build/%.o: %.cpp $(HDR)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

assets/roots.bin:
	python3 sys/tools/mkroots.py $@

# packages are no longer part of the OS image: they live in the GitHub repo (EnderAstra7744/aspi-package-sage) and are
# downloaded by "aspi install". "make repo" writes the folder to upload: dist/aspi-package-sage/
NS_VER  := $(shell python3 -c "import json;print(json.load(open('packages/NetSurf/ASPIManifest.json'))['version'])")
FG_VER  := $(shell python3 -c "import json;print(json.load(open('packages/FroggyCode/ASPIManifest.json'))['version'])")
repo: assets/netsurf.elf assets/netsurf-arm64.elf assets/netsurf-arm32.elf assets/froggycode.elf assets/froggycode-arm64.elf assets/froggycode-arm32.elf sys/tools/mkpkg.py $(wildcard packages/*/*) $(wildcard packages/*/src/*)
	@mkdir -p dist/aspi-package-sage
	python3 sys/tools/mkpkg.py packages/NetSurf dist/aspi-package-sage/NetSurf-$(NS_VER).aspi assets/netsurf.elf
	python3 sys/tools/mkpkg.py packages/NetSurf dist/aspi-package-sage/NetSurf-$(NS_VER)-arm64.aspi assets/netsurf-arm64.elf
	python3 sys/tools/mkpkg.py packages/NetSurf dist/aspi-package-sage/NetSurf-$(NS_VER)-arm32.aspi assets/netsurf-arm32.elf
	python3 sys/tools/mkpkg.py packages/FroggyCode dist/aspi-package-sage/FroggyCode-$(FG_VER).aspi assets/froggycode.elf
	python3 sys/tools/mkpkg.py packages/FroggyCode dist/aspi-package-sage/FroggyCode-$(FG_VER)-arm64.aspi assets/froggycode-arm64.elf
	python3 sys/tools/mkpkg.py packages/FroggyCode dist/aspi-package-sage/FroggyCode-$(FG_VER)-arm32.aspi assets/froggycode-arm32.elf
	cp sys/tools/repo_README.md dist/aspi-package-sage/README.md

# FroggyCode 2.0: native C program (own libc), built into the OS image
assets/froggycode.elf: $(wildcard native/froggy/*) native/as_api.h native/libc/libc.c native/libc/build.sh $(wildcard native/libc/include/*.h)
	@mkdir -p build
	sh native/libc/build.sh $@ native/froggy/main.c native/froggy/ui.c native/froggy/brain.c

# NetSurf for ARM: built outside the repo (native/netsurf/arm/README.md), the results are kept in assets/
# the same program for the ARM kernels (ARM64: ELF64 aarch64, ARM32: ELF32 hard-float), linked at 0x48000000
assets/froggycode-arm64.elf: $(wildcard native/froggy/*) native/as_api.h native/libc/libc.c native/libc/build_arm.sh $(wildcard native/libc/include/*.h)
	sh native/libc/build_arm.sh arm64 $@ native/froggy/main.c native/froggy/ui.c native/froggy/brain.c
assets/froggycode-arm32.elf: $(wildcard native/froggy/*) native/as_api.h native/libc/libc.c native/libc/build_arm.sh native/libc/rt_arm32.c $(wildcard native/libc/include/*.h)
	sh native/libc/build_arm.sh arm32 $@ native/froggy/main.c native/froggy/ui.c native/froggy/brain.c

# native (C) programs: 32-bit ELF linked at NATIVE_BASE, freestanding
assets/nhello.elf: native/hello.c native/as_api.h native/app.ld
	@mkdir -p build
	$(CC) $(ARCHF) -std=gnu11 -O2 -Wall -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables -c native/hello.c -o build/nhello.o
	$(LD) -m elf_i386 -T native/app.ld -nostdlib -z noexecstack --build-id=none -o $@ build/nhello.o

assets/nlibc.elf: native/libc/libc.c native/libc/test_libc.c native/libc/build.sh $(wildcard native/libc/include/*.h) native/as_api.h
	@mkdir -p build
	sh native/libc/build.sh $@ native/libc/test_libc.c

build/gui/assets.o: gui/assets.S assets/nlibc.elf assets/nhello.elf assets/roots.bin assets/store_netsurf1.bmp assets/store_netsurf2.bmp assets/store_netsurf3.bmp assets/store_froggy1.bmp assets/store_froggy2.bmp assets/store_froggy3.bmp assets/boot_logo.bmp assets/ast_icon.bmp assets/as_logo.bmp assets/froggycode_icon.bmp assets/netsurf_icon.bmp assets/astrascript_logo.bmp $(wildcard assets/wp_*.wpz assets/wpt_*.wpz)
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

build/boot/boot.o: arch/x86/boot.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL): $(OBJ) arch/x86/linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJ)
	@python3 sys/tools/check_end.py $@

# disk size in MiB (the file system needs 1.3 MiB for its tables; the rest holds files)
DISKSZ  = 64
DISK    = astrasage.disk

# Persistent disk image: created once, then reused so files survive reboots.
$(DISK):
	dd if=/dev/zero of=$(DISK) bs=1M count=$(DISKSZ) 2>/dev/null
	@echo "created $(DISK) (empty; AstraSageOS formats it on the first save)"

QFLAGS = -kernel $(KERNEL) -m 128M -nic user,model=e1000,hostfwd=tcp::8765-:8765 -drive file=$(DISK),format=raw,if=ide,index=0 -rtc base=localtime \
         $(shell test -f aspi_repo.img && echo '-drive file=aspi_repo.img,format=raw,if=ide,index=1')

# Run by every run target: refreshes the ASPI repo disk by itself (download if the host is online, else the
# cache; a failure here never stops the OS from starting).
aspi-auto:
	-@python3 sys/tools/aspi_sync.py auto

run: aspi-auto $(KERNEL) $(DISK)
	$(QEMU) $(QFLAGS) -vga std -serial none -append gui

# Same as run-vnc but logs boot stages (1..5) to debug.log; white squares top-left show the same.
run-debug: aspi-auto $(KERNEL) $(DISK)
	$(QEMU) $(QFLAGS) -vga std -serial none -append gui -vnc :0 -debugcon file:debug.log -no-reboot -no-shutdown -d guest_errors,cpu_reset -D qemu.log

# Graphical desktop through VNC (Termux): connect a VNC viewer to localhost:5900.
run-vnc: aspi-auto $(KERNEL) $(DISK)
	$(QEMU) $(QFLAGS) -vga std -serial none -append gui -vnc :0

# Terminal mode (SSH, Termux): the console runs over the serial port, drawn by your
# own terminal - top-left anchored, full 256 colours. Quit with Ctrl+A then X.
run-nographic: aspi-auto $(KERNEL) $(DISK)
	$(QEMU) $(QFLAGS) -vga none -nographic

# Bootable CD image with the real GNU GRUB 2.12 (BIOS + UEFI): BIOS/UEFI -> GRUB menu -> multiboot as_kernel.
# Needs only Python 3 (the GRUB binaries are prebuilt in grub/bin, see grub/BUILDING.txt).
ISO = astrasage.iso
iso: $(KERNEL)
	-$(MAKE) -C arch/x86_64 full
	-$(MAKE) -C arch/arm64 full
	-$(MAKE) -C arch/arm32 full
	-$(MAKE) -C arch/arm64/efi
	python3 sys/tools/grubcheck.py $(KERNEL) grub
	python3 sys/tools/mkgrubiso.py $(KERNEL) $(ISO)

# The older CD without GRUB (own El Torito boot sector, BIOS only).
iso-legacy: $(KERNEL)
	python3 sys/tools/mkiso.py $(KERNEL) astrasage-legacy.iso

# Re-assemble the CD boot sector after editing boot/isoboot.asm (needs nasm).
boot/isoboot.bin: boot/isoboot.asm
	nasm -f bin boot/isoboot.asm -o boot/isoboot.bin

# Boot the ISO in QEMU; the data disk is attached too, so files still persist.
run-iso: iso $(DISK)
	$(QEMU) -m 128M -cdrom $(ISO) -boot d -drive file=$(DISK),format=raw,if=ide,index=0 -rtc base=localtime

# Fill the aspi repo disk on the host (the OS has no network): make aspi-sync PKGS="AsCode AsUtil"
aspi-sync:
	python3 sys/tools/aspi_sync.py fetch $(PKGS)

# Wipe the disk (all users and files!)
reset-disk:
	rm -f $(DISK)

test:
	@mkdir -p build
	g++ -DHOST_TEST -Wall -Wextra -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit $(INC) -c sys/aspi_convertor.cpp -o build/aspi_convertor_host.o
	$(HOSTCC) -DHOST_TEST -Wall -Wextra -std=gnu11 $(INC) -o build/host_test build/aspi_convertor_host.o \
	    terminal/ast.c terminal/neofetch.c terminal/editor.c fs/fs.c fs/persist.c sys/proc.c sys/png.c sys/bootlog.c kernel/kstring.c kernel/kprintf.c \
	    sys/users.c sys/config.c sys/perm.c sys/sha256.c sys/astrascript.c sys/as_host_kernel.c $(ASENGINE) sys/inflate.c sys/lzma.c sys/archive.c sys/aspi.c \
	    sys/tests/hal_stub.c
	python3 sys/tests/run_host_test.py
	python3 sys/tests/run_native64.py
	python3 sys/tests/crypto_vectors.py build/crypto_vectors.txt
	$(HOSTCC) -O2 -Wall -o build/crypto_host sys/tests/crypto_host.c sys/crypto.c
	build/crypto_host build/crypto_vectors.txt
	sh sys/tests/run_tls_test.sh
	python3 sys/tests/aspi_sync_test.py
	python3 sys/tests/convertor_test.py
	python3 toolchain/tests/run_tests.py
	python3 sys/tests/gui_test.py

# Re-create the VGA bitmap font from font/font.ttf (needs Python + Pillow).
font:
	python3 font/ttf2fnt.py 16

clean:
	rm -rf build

.PHONY: arm32 x86_64 arm64 repo aspi-server aspi-sync all run run-vnc run-debug run-nographic run-iso iso iso-legacy reset-disk test font clean

# ARM64 port (clang + ld.lld): build/arm64/as_kernel_arm64.bin, see arch/README.md
arm64:
	$(MAKE) -C arch/arm64

x86_64:
	$(MAKE) -C arch/x86_64
arm32:
	$(MAKE) -C arch/arm32
