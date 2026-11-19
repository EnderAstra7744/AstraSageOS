# ARM32 port, first stage.  make -C arch/arm32 (clang + ld.lld)
# run: qemu-system-arm -M virt -cpu cortex-a15 -m 128 -kernel build/arm32/as_kernel_arm32.bin -serial stdio
B = ../../build/arm32
CFLAGS = --target=armv7a-none-eabi -mfpu=none -marm -ffreestanding -fno-builtin -nostdlib -mno-unaligned-access -O2 -Wall -Wextra
all: $(B)/as_kernel_arm32.bin
$(B)/boot.o: boot.S
	@mkdir -p $(B)
	clang $(CFLAGS) -c $< -o $@
$(B)/main.o: main.c
	clang $(CFLAGS) -c $< -o $@
$(B)/as_kernel_arm32.elf: $(B)/boot.o $(B)/main.o linker.ld
	ld.lld -T linker.ld -nostdlib -o $@ $(B)/boot.o $(B)/main.o
$(B)/as_kernel_arm32.bin: $(B)/as_kernel_arm32.elf
	llvm-objcopy -O binary $< $@

# ---- the full OS (desktop, AST, ASPI, AstraScript ...) on ARM32: make -C arch/arm32 full -> ../../build/a32/as_arm32_gui.bin
# run: qemu-system-arm -M virt -cpu cortex-a15 -m 512 -kernel as_arm32_gui.bin -device ramfb -device virtio-keyboard-device \
#        -device virtio-tablet-device -drive file=disk.img,if=none,format=raw,id=d -device virtio-blk-device,drive=d \
#        -netdev user,id=n -device virtio-net-device,netdev=n -serial stdio
R  = ../..
FB = $(R)/build/a32
INCS = -I$(R)/kernel -I$(R)/terminal -I$(R)/font -I$(R)/gui -I$(R)/AstraScript/include
FCF = --target=armv7a-none-eabi -mcpu=cortex-a15 -mfpu=vfpv4-d16 -mfloat-abi=hard -marm -mno-unaligned-access -std=gnu11 -O2 -Wall -Wno-unused-parameter -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -include $(CURDIR)/jmp.h $(INCS)
FXF = --target=armv7a-none-eabi -mcpu=cortex-a15 -mfpu=vfpv4-d16 -mfloat-abi=hard -marm -mno-unaligned-access -std=gnu++17 -O2 -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables $(INCS)
ASE = $(addprefix AstraScript/src/,builtins.c check.c classes.c collections.c engine.c errors.c fileobj.c functions.c interpreter.c lexer.c memory.c modules.c parser.c strings.c types.c lib_data.c)
FSRC = sys/proc.c sys/png.c sys/bootlog.c kernel/kstring.c kernel/kprintf.c \
       terminal/console.c terminal/keyboard.c terminal/ast.c terminal/neofetch.c terminal/editor.c \
       fs/fs.c fs/persist.c sys/net.c sys/crypto.c sys/x509.c sys/tls.c \
       sys/users.c sys/config.c sys/perm.c sys/sha256.c sys/astrascript.c sys/as_host_kernel.c $(ASE) \
       kernel/native.c sys/inflate.c sys/lzma.c sys/archive.c sys/aspi.c \
       gui/gfx.c gui/theme.c gui/bmp.c gui/coro.c gui/astragui.c gui/astraui.c gui/app_ast.c gui/app_files.c gui/app_welcome.c gui/app_settings.c gui/app_login.c gui/wallpaper.c gui/app_tasks.c gui/app_script.c gui/app_store.c gui/shell.c
FOBJ = $(patsubst %.c,$(FB)/%.o,$(FSRC)) $(FB)/sys/aspi_convertor.o $(FB)/sys/errscreen.o $(FB)/gui/assets.o $(FB)/hw.o $(FB)/kmain.o $(FB)/boot.o $(FB)/jmp.o $(FB)/rt.o
full: $(FB)/as_arm32_gui.bin
$(FB)/%.o: $(R)/%.c
	@mkdir -p $(dir $@)
	clang $(FCF) -c $< -o $@
$(FB)/hw.o: hw.c
	@mkdir -p $(FB)
	clang $(FCF) -c $< -o $@
$(FB)/kmain.o: kmain.c
	@mkdir -p $(FB)
	clang $(FCF) -c $< -o $@
$(FB)/boot.o: boot_full.S
	@mkdir -p $(FB)
	clang --target=armv7a-none-eabi -mcpu=cortex-a15 -mfpu=vfpv4-d16 -mfloat-abi=hard -marm -c $< -o $@
$(FB)/jmp.o: jmp.S
	@mkdir -p $(FB)
	clang --target=armv7a-none-eabi -mcpu=cortex-a15 -mfpu=vfpv4-d16 -mfloat-abi=hard -marm -c $< -o $@
$(FB)/sys/aspi_convertor.o: $(R)/sys/aspi_convertor.cpp
	@mkdir -p $(dir $@)
	clang++ $(FXF) -c $< -o $@
$(FB)/sys/errscreen.o: $(R)/sys/errscreen.cpp
	@mkdir -p $(dir $@)
	clang++ $(FXF) -c $< -o $@
$(FB)/gui/assets.o: $(R)/gui/assets.S
	@mkdir -p $(dir $@)
	cd $(R) && clang --target=armv7a-none-eabi -c gui/assets.S -o build/a32/gui/assets.o
$(FB)/as_arm32_gui.elf: $(FOBJ) linker_full.ld
	ld.lld -T linker_full.ld -nostdlib -o $@ $(FOBJ)
$(FB)/as_arm32_gui.bin: $(FB)/as_arm32_gui.elf
	llvm-objcopy -O binary $< $@
$(FB)/rt.o: rt.c
	@mkdir -p $(FB)
	clang $(FCF) -c $< -o $@
