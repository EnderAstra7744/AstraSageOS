# UEFI loaders: BOOTAA64.EFI (ARM64) and BOOTARM.EFI (ARM32) -> ../../../build/{a64,a32}/ ; needs clang + lld-link
all:
	mkdir -p ../../../build/a64 ../../../build/a32
	clang --target=aarch64-unknown-windows -ffreestanding -fno-builtin -fshort-wchar -fno-stack-protector -mgeneral-regs-only -O2 -c efistub.c -o ../../../build/a64/efistub.o
	lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm64 /out:../../../build/a64/BOOTAA64.EFI ../../../build/a64/efistub.o
	clang --target=thumbv7-unknown-windows -ffreestanding -fno-builtin -fshort-wchar -fno-stack-protector -O2 -c efistub.c -o ../../../build/a32/efistub.o
	lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm /out:../../../build/a32/BOOTARM.EFI ../../../build/a32/efistub.o
