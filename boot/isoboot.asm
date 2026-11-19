; AstraSageOS CD boot sector (El Torito, no emulation).
;
; The BIOS loads this 2048-byte image at 0000:7C00 and jumps to it with DL = boot drive.
; It loads the flat kernel image from the CD (INT 13h extended reads), switches to 32-bit
; protected mode, copies the image to its load address, zeroes the kernel's .bss and
; jumps to as_kernel with the Multiboot handoff (EAX = 0x2BADB002, EBX = info struct).
;
; The parameter block right after the jump is patched by tools/mkiso.py.
bits 16
org 0x7C00

start:
    jmp short main
    nop
params:
p_klba      dd 0            ; first CD sector (2048 bytes) of the kernel image
p_ksect     dd 0            ; number of CD sectors
p_filesz    dd 0            ; bytes of image to copy
p_memsz     dd 0            ; bytes occupied in memory (filesz + .bss)
p_entry     dd 0            ; kernel entry point
p_load      dd 0            ; load address (1 MiB)

main:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [drive], dl

    mov si, msg_load
    call print

    ; ---- A20 gate ----
    mov ax, 0x2401
    int 0x15
    in al, 0x92
    or al, 2
    and al, 0xFE
    out 0x92, al

    ; ---- memory above 1 MiB (KiB) for the Multiboot info ----
    xor cx, cx
    xor dx, dx
    mov ax, 0xE801
    int 0x15
    jc .old
    test ax, ax
    jz .have
    mov cx, ax
    mov dx, bx
.have:
    movzx eax, dx
    shl eax, 6
    movzx ecx, cx
    add eax, ecx
    jmp .store
.old:
    mov ah, 0x88
    int 0x15
    movzx eax, ax
.store:
    mov dword [0x500], 1            ; flags: memory fields valid
    mov dword [0x504], 640          ; mem_lower
    mov [0x508], eax                ; mem_upper

    ; ---- read the kernel image in 16-sector pieces through a buffer at 0x10000 and copy each
    ;      piece to its final place above 1 MiB with INT 15h AH=87h (the kernel with its embedded
    ;      images is far bigger than the memory below 640 KiB) ----
    mov eax, [p_load]
    mov [dest], eax
    mov eax, [p_klba]
    mov [lba], eax
    mov eax, [p_ksect]
    mov [remaining], eax

.next:
    mov eax, [remaining]
    test eax, eax
    jz .loaded
    cmp eax, 16
    jbe .count
    mov eax, 16
.count:
    mov [dap_count], ax
    mov word [dap_seg], 0x1000
    mov ebx, [lba]
    mov [dap_lba], ebx
    mov si, dap
    mov dl, [drive]
    mov ah, 0x42
    int 0x13
    jc fail
    movzx eax, word [dap_count]
    add [lba], eax
    sub [remaining], eax
    mov ebx, eax
    shl ebx, 11                     ; bytes in this piece
    mov eax, [dest]
    mov [dst_b0], ax
    shr eax, 16
    mov [dst_b2], al
    mov eax, ebx
    shr eax, 1                      ; words to copy
    mov cx, ax
    push es
    xor ax, ax
    mov es, ax
    mov si, xgdt
    mov ah, 0x87
    int 0x15
    pop es
    jc fail
    add [dest], ebx
    jmp .next

.loaded:
    ; ---- protected mode ----
    cli
    lgdt [gdtr]
    mov eax, cr0
    or al, 1
    mov cr0, eax
    jmp 0x08:pm

fail:
    mov si, msg_fail
    call print
.hang:
    hlt
    jmp .hang

print:                              ; DS:SI = zero-terminated string
    lodsb
    test al, al
    jz .done
    mov ah, 0x0E
    mov bx, 7
    int 0x10
    jmp print
.done:
    ret

bits 32
pm:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov esp, 0x7000

    cld                             ; the image is already at its load address
    mov edi, [p_load]               ; zero the rest (.bss)
    add edi, [p_filesz]
    add edi, 3
    and edi, ~3
    mov ecx, [p_load]
    add ecx, [p_memsz]
    sub ecx, edi
    shr ecx, 2
    xor eax, eax
    rep stosd

    mov eax, 0x2BADB002
    mov ebx, 0x500
    mov ecx, [p_entry]
    jmp ecx

; ---- data ----
align 4
gdt:
    dq 0
    dq 0x00CF9A000000FFFF           ; 0x08: 32-bit code, base 0, 4 GiB
    dq 0x00CF92000000FFFF           ; 0x10: 32-bit data, base 0, 4 GiB
gdtr:
    dw gdtr - gdt - 1
    dd gdt

drive       db 0
msg_load    db 'Loading AstraSageOS...', 13, 10, 0
msg_fail    db 13, 10, 'AstraSageOS: cannot read the kernel from the CD.', 0

align 4
lba         dd 0
remaining   dd 0
dest        dd 0

; descriptor table for INT 15h AH=87h: [2] source (0x10000), [3] destination (patched)
align 4
xgdt:
    dq 0
    dq 0
    dw 0xFFFF, 0x0000
    db 0x01, 0x93, 0, 0
    dw 0xFFFF
dst_b0:
    dw 0
dst_b2:
    db 0
    db 0x93
    dw 0
    dq 0
    dq 0

align 4
dap:
    db 0x10, 0
dap_count   dw 0
dap_off     dw 0
dap_seg     dw 0
dap_lba     dd 0
            dd 0

times 2048 - ($ - $$) db 0
