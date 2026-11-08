[bits 16]
global _start
extern kernel_main

section .boot
_start:
    ; 1. Clear interrupts and clear segment registers
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; Set 16-bit stack below bootloader

    ; 2. Enable A20 Line (gives access to full memory beyond 1MB)
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 3. Load the Global Descriptor Table
    lgdt [gdt_descriptor]

    ; 4. Switch to 32-bit Protected Mode (Set PE bit in CR0)
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 5. Far jump to clear the prefetch queue and load 32-bit Code Segment
    jmp CODE_SEG:init_32bit

[bits 32]
init_32bit:
    ; 6. Set up 32-bit Data Segment registers
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 7. Set up a secure 32-bit stack space
    mov esp, 0x90000

    ; 8. Call Rust code
    call kernel_main

    ; Halt if Rust returns
.halt:
    cli
    hlt
    jmp .halt

; --- Minimal GDT Layout ---
gdt_start:
gdt_null:
    dd 0x0, 0x0
gdt_code:
    dw 0xFFFF, 0x0
    db 0x0, 0b10011010, 0b11001111, 0x0
gdt_data:
    dw 0xFFFF, 0x0
    db 0x0, 0b10010010, 0b11001111, 0x0
gdt_end:

gdt_descriptor:
    dw gdt_end - gdt_start - 1
    dd gdt_start

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

; Fill out the exact rest of the MBR sector with the magic boot signature
section .mbr_signature
    dw 0xAA55
