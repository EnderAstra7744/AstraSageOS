ENTRY(_start)
SECTIONS
{
    . = 1M;
    .text   : ALIGN(4K) { *(.multiboot) *(.text .text.*) }
    /* the ISA DMA buffer of the sound card must stay below 16 MiB, so it sits right after the code, before the big images */
    .dmabuf : ALIGN(64K) { *(.dmabuf) }
    .rodata : ALIGN(4K) { *(.rodata .rodata.*) }
    .data   : ALIGN(4K) { *(.data .data.*) }
    .bss    : ALIGN(4K) { *(COMMON) *(.bss .bss.*) }
    _end = .;
}
