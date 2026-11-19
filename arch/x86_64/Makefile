# x86_64 port, first stage (Multiboot 1 -> long mode).  make -C arch/x86_64  ->  ../../build/x86_64/as_kernel_x86_64.elf
# GRUB/QEMU: qemu-system-x86_64 -kernel build/x86_64/as_kernel_x86_64.elf -serial stdio   (QEMU loads Multiboot ELF32 only; wrap it with
# GRUB's multiboot, or use the .elf32 below)
B = ../../build/x86_64
all: $(B)/as_kernel_x86_64.elf32
$(B)/boot.o: boot.S
	@mkdir -p $(B)
	gcc -m64 -c $< -o $@
$(B)/main.o: main.c
	gcc -m64 -ffreestanding -fno-builtin -fno-pic -mno-red-zone -mno-sse -mno-mmx -O2 -Wall -Wextra -c $< -o $@
$(B)/as_kernel_x86_64.elf: $(B)/boot.o $(B)/main.o linker.ld
	ld -m elf_x86_64 -T linker.ld -nostdlib -z max-page-size=4096 -o $@ $(B)/boot.o $(B)/main.o
$(B)/as_kernel_x86_64.elf32: $(B)/as_kernel_x86_64.elf
	objcopy -O elf32-i386 $< $@

# ---- the full OS on x86_64: make -C arch/x86_64 full -> ../../build/x64/as_kernel_x64.elf32 (Multiboot, runs with qemu-system-x86_64 -kernel)
R  = ../..
FB = $(R)/build/x64
INCS = -I$(R)/kernel -I$(R)/terminal -I$(R)/font -I$(R)/gui -I$(R)/AstraScript/include
XF = -m64 -mno-red-zone -fno-pic -fno-pie -fno-stack-protector -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -O2 -Wall -Wno-unused-parameter -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast $(INCS)
FCF = $(XF) -std=gnu11
FXF = $(XF) -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-asynchronous-unwind-tables
ASE = $(addprefix AstraScript/src/,builtins.c check.c classes.c collections.c engine.c errors.c fileobj.c functions.c interpreter.c lexer.c memory.c modules.c parser.c strings.c types.c lib_data.c)
FSRC = kernel/kmain.c sys/proc.c sys/png.c sys/bootlog.c kernel/kstring.c kernel/kprintf.c kernel/sysinfo.c \
       terminal/console.c terminal/keyboard.c terminal/serial.c terminal/ast.c terminal/neofetch.c terminal/editor.c \
       fs/fs.c fs/persist.c arch/x86/drivers/ata.c arch/x86/drivers/e1000.c arch/x86/drivers/rtl8139.c sys/net.c sys/crypto.c sys/x509.c sys/tls.c \
       sys/users.c sys/config.c sys/perm.c sys/sha256.c sys/astrascript.c sys/as_host_kernel.c $(ASE) \
       sys/inflate.c sys/lzma.c sys/archive.c sys/aspi.c \
       arch/x86/drivers/video.c arch/x86/drivers/mouse.c arch/x86/drivers/sound.c gui/gfx.c gui/theme.c gui/bmp.c gui/coro.c gui/astragui.c gui/astraui.c gui/app_ast.c gui/app_files.c gui/app_welcome.c gui/app_settings.c gui/app_login.c gui/wallpaper.c gui/app_tasks.c gui/app_script.c gui/app_store.c gui/shell.c
FOBJ = $(patsubst %.c,$(FB)/%.o,$(FSRC)) $(FB)/arch/x86_64/idt.o $(FB)/kernel/native.o $(FB)/compat.o $(FB)/sys/aspi_convertor.o $(FB)/sys/errscreen.o $(FB)/gui/assets.o $(FB)/boot.o
full: $(FB)/as_kernel_x64.elf32
$(FB)/%.o: $(R)/%.c
	@mkdir -p $(dir $@)
	gcc $(FCF) -c $< -o $@
$(FB)/%.o: $(R)/%.cpp
	@mkdir -p $(dir $@)
	g++ $(FXF) -c $< -o $@
$(FB)/gui/assets.o: $(R)/gui/assets.S
	@mkdir -p $(dir $@)
	cd $(R) && gcc -m64 -c gui/assets.S -o build/x64/gui/assets.o
$(FB)/boot.o: boot.S
	@mkdir -p $(FB)
	gcc -m64 -c $< -o $@
$(FB)/as_kernel_x64.elf: $(FOBJ) linker_full.ld
	ld -m elf_x86_64 -T linker_full.ld -nostdlib -z noexecstack -z max-page-size=4096 --build-id=none -o $@ $(FOBJ)
$(FB)/as_kernel_x64.elf32: $(FB)/as_kernel_x64.elf
	objcopy -O elf32-i386 $< $@
$(FB)/compat.o: compat.S
	@mkdir -p $(FB)
	gcc -m64 -c $< -o $@
