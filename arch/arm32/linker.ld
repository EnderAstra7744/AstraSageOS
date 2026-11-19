ENTRY(_start)
SECTIONS
{
    . = 0x40010000;
    .text   : { *(.text.boot) *(.text .text.*) }
    .rodata : ALIGN(4096) { *(.rodata .rodata.*) }
    .data   : ALIGN(4096) { *(.data .data.*) }
    .bss    : ALIGN(4096) { __bss_start = .; *(COMMON) *(.bss .bss.*) . = ALIGN(4); __bss_end = .; }
    . = ALIGN(16); . += 0x10000;
    __stack_top = .;
}
