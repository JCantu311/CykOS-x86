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

    ; Load full kernel (90 KB = 180 sectors) into memory starting at 0x7E00
    ; Chunk 1: 100 sectors (50 KB) from LBA 1 into 0x07E0:0x0000 (physical 0x7E00)
    mov si, dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

    ; Chunk 2: 80 sectors (40 KB) from LBA 101 into 0x1460:0x0000 (physical 0x14600)
    mov word [dap_count], 80
    mov word [dap_seg], 0x1460
    mov word [dap_lba], 101
    mov word [dap_lba + 2], 0
    mov si, dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

.read_ok:           

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
dap_count:
    dw 100                  ; 100 sectors (50 KB)
    dw 0x0000               ; Offset (always 0)
dap_seg:
    dw 0x07E0               ; Segment (0x07E0:0x0000 = physical 0x7E00)
dap_lba:
    dd 1                    ; LBA low 32 bits (Sector 1)
    dd 0                    ; LBA high 32 bits

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

    mov esp, 0x90000        

    ; Enable SSE in CR0 and CR4
    mov eax, cr0
    and ax, 0xFFFB          ; Clear CR0.EM (bit 2) - disable FPU emulation
    or ax, 0x0002           ; Set CR0.MP (bit 1) - monitor coprocessor
    mov cr0, eax

    mov eax, cr4
    or eax, (1 << 9) | (1 << 10) ; Set CR4.OSFXSR (bit 9) and OSXMMEXCPT (bit 10)
    mov cr4, eax

    call kernel_main        

.halt:
    cli
    hlt
    jmp .halt