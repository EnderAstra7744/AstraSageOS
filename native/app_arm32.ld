ENTRY(as_main)
SECTIONS
{
    . = 0x48000000;
    .text   : { *(.text.as_main) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data   : { *(.data .data.*) }
    .init_array : { __init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) KEEP(*(.ctors*)) __init_array_end = .; }
    .bss    : { *(COMMON) *(.bss .bss.*) }
    _app_end = .;
    /DISCARD/ : { *(.eh_frame*) *(.comment) *(.note*) }
}
