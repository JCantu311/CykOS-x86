[bits 16]
global _start
extern kernel_main

section .boot       ; NEW SECTION: Explicitly dedicated to Sector 1 ONLY
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [BOOT_DRIVE], dl

    cmp dl, 0
    jne .drive_ok
    mov byte [BOOT_DRIVE], 0x80

.drive_ok:
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13

    ; Read 192 sectors (96 KB) sequentially into memory starting at 0x7E00
    ; Read in 6 chunks of 32 sectors to avoid crossing any 64KB physical segment boundaries
    mov cx, 6               ; 6 chunks * 32 sectors = 192 sectors
    mov bx, 0x07E0          ; Starting segment (0x07E0:0x0000 = physical 0x7E00)
    mov dword [dap_lba], 1  ; Start at LBA 1 (immediately after Sector 0 MBR)

.read_loop:
    mov [dap_seg], bx
    mov si, dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

    add dword [dap_lba], 32 ; Advance 32 sectors
    add bx, 0x0400          ; 32 sectors * 512 bytes = 16,384 bytes = 0x400 paragraphs
    loop .read_loop           

    in al, 0x92
    or al, 2
    out 0x92, al

    lgdt [gdt_descriptor]

    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:init_32bit_protected

disk_error:
    mov ax, 0xB800
    mov es, ax
    mov word [es:0x0000], 0x0C45
.error_loop:
    cli
    hlt
    jmp .error_loop

align 4
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

BOOT_DRIVE: db 0            

align 4
dap:
    db 0x10                 ; Packet size = 16 bytes
    db 0x00                 ; Reserved (0)
    dw 32                   ; 32 sectors per chunk
    dw 0x0000               ; Offset (always 0)
dap_seg:
    dw 0x07E0               ; Segment
dap_lba:
    dd 1                    ; LBA low
    dd 0                    ; LBA high            

; =========================================================================
; SECTOR 2+: This code goes into the standard text section
; =========================================================================
section .text       ; This must match what is called on line 18 of linker.ld
[bits 32]
init_32bit_protected:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x70000        

    call kernel_main        

.halt:
    cli
    hlt
    jmp .halt