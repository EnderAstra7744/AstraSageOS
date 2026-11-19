# ARM64 port: make -C arch/arm64   (needs clang and ld.lld)  ->  ../../build/arm64/as_kernel_arm64.elf and .bin
# run:  qemu-system-aarch64 -M virt -cpu cortex-a72 -m 256 -kernel build/arm64/as_kernel_arm64.bin \
#         -device ramfb -device virtio-keyboard-device \
#         -drive file=disk.img,if=none,format=raw,id=d -device virtio-blk-device,drive=d -serial stdio
B = ../../build/arm64
CFLAGS = --target=aarch64-none-elf -ffreestanding -fno-builtin -nostdlib -mstrict-align -O2 -Wall -Wextra -I../../font -I../../kernel
all: $(B)/as_kernel_arm64.bin
$(B)/boot.o: boot.S
	@mkdir -p $(B)
	clang $(CFLAGS) -c $< -o $@
$(B)/main.o: main.c
	@mkdir -p $(B)
	clang $(CFLAGS) -c $< -o $@
CORE = kernel/kstring.c kernel/kprintf.c fs/fs.c fs/persist.c
COREO = $(patsubst %.c,$(B)/core_%.o,$(subst /,_,$(CORE)))
$(B)/core_kernel_kstring.o: ../../kernel/kstring.c
	clang $(CFLAGS) -c $< -o $@
$(B)/core_kernel_kprintf.o: ../../kernel/kprintf.c
	clang $(CFLAGS) -c $< -o $@
$(B)/core_fs_fs.o: ../../fs/fs.c
	clang $(CFLAGS) -c $< -o $@
$(B)/core_fs_persist.o: ../../fs/persist.c
	clang $(CFLAGS) -c $< -o $@
$(B)/as_kernel_arm64.elf: $(B)/boot.o $(B)/main.o $(COREO) linker.ld
	ld.lld -T linker.ld -nostdlib -o $@ $(B)/boot.o $(B)/main.o $(COREO)
$(B)/as_kernel_arm64.bin: $(B)/as_kernel_arm64.elf
	llvm-objcopy -O binary $< $@ || objcopy -O binary $< $@

# ---- the full OS (desktop, AST, ASPI, AstraScript ...) on ARM64: make -C arch/arm64 full -> ../../build/a64/as_arm64_gui.bin
# run: qemu-system-aarch64 -M virt -cpu cortex-a72 -m 512 -kernel as_arm64_gui.bin -device ramfb -device virtio-keyboard-device \
#        -device virtio-tablet-device -drive file=disk.img,if=none,format=raw,id=d -device virtio-blk-device,drive=d \
#        -netdev user,id=n -device virtio-net-device,netdev=n -serial stdio
R  = ../..
FB = $(R)/build/a64
INCS = -I$(R)/kernel -I$(R)/terminal -I$(R)/font -I$(R)/gui -I$(R)/AstraScript/include
FCF = --target=aarch64-none-elf -mstrict-align -std=gnu11 -O2 -Wall -Wno-unused-parameter -ffreestanding -fno-builtin -fno-pic -fno-pie -fno-stack-protector -include jmp.h $(INCS)
FXF = --target=aarch64-none-elf -mstrict-align -std=gnu++17 -O2 -ffreestanding -fno-builtin -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-use-cxa-atexit -fno-pic -fno-pie -fno-stack-protector -fno-asynchronous-unwind-tables $(INCS)
ASE = $(addprefix AstraScript/src/,builtins.c check.c classes.c collections.c engine.c errors.c fileobj.c functions.c interpreter.c lexer.c memory.c modules.c parser.c strings.c types.c lib_data.c)
FSRC = sys/proc.c sys/png.c sys/bootlog.c kernel/kstring.c kernel/kprintf.c \
       terminal/console.c terminal/keyboard.c terminal/ast.c terminal/neofetch.c terminal/editor.c \
       fs/fs.c fs/persist.c sys/net.c sys/crypto.c sys/x509.c sys/tls.c \
       sys/users.c sys/config.c sys/perm.c sys/sha256.c sys/astrascript.c sys/as_host_kernel.c $(ASE) \
       kernel/native.c sys/inflate.c sys/lzma.c sys/archive.c sys/aspi.c \
       gui/gfx.c gui/theme.c gui/bmp.c gui/coro.c gui/astragui.c gui/astraui.c gui/app_ast.c gui/app_files.c gui/app_welcome.c gui/app_settings.c gui/app_login.c gui/wallpaper.c gui/app_tasks.c gui/app_script.c gui/app_store.c gui/shell.c
FOBJ = $(patsubst %.c,$(FB)/%.o,$(FSRC)) $(FB)/sys/aspi_convertor.o $(FB)/sys/errscreen.o $(FB)/gui/assets.o $(FB)/hw.o $(FB)/kmain.o $(FB)/boot.o $(FB)/jmp.o
full: $(FB)/as_arm64_gui.bin
$(FB)/%.o: $(R)/%.c
	@mkdir -p $(dir $@)
	clang $(FCF) -c $< -o $@
$(FB)/hw.o: hw.c
	@mkdir -p $(FB)
	clang $(FCF) -c $< -o $@
$(FB)/kmain.o: kmain.c
	@mkdir -p $(FB)
	clang $(FCF) -c $< -o $@
$(FB)/boot.o: boot.S
	@mkdir -p $(FB)
	clang --target=aarch64-none-elf -c $< -o $@
$(FB)/jmp.o: jmp.S
	@mkdir -p $(FB)
	clang --target=aarch64-none-elf -c $< -o $@
$(FB)/sys/aspi_convertor.o: $(R)/sys/aspi_convertor.cpp
	@mkdir -p $(dir $@)
	clang++ $(FXF) -c $< -o $@
$(FB)/sys/errscreen.o: $(R)/sys/errscreen.cpp
	@mkdir -p $(dir $@)
	clang++ $(FXF) -c $< -o $@
$(FB)/gui/assets.o: $(R)/gui/assets.S
	@mkdir -p $(dir $@)
	cd $(R) && clang --target=aarch64-none-elf -c gui/assets.S -o build/a64/gui/assets.o
$(FB)/as_arm64_gui.elf: $(FOBJ) linker_full.ld
	ld.lld -T linker_full.ld -nostdlib -o $@ $(FOBJ)
$(FB)/as_arm64_gui.bin: $(FB)/as_arm64_gui.elf
	llvm-objcopy -O binary $< $@
