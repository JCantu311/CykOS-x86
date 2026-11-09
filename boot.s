[bits 16]
global _start
extern kernel_main
extern __bss_start
extern __bss_end

section .boot       ; Dedicated to Sector 1 ONLY (MBR)
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

    ; Load kernel from disk using INT 13h AH=42h (Extended LBA Read)
    ; Read in 4 chunks of 64 sectors (32 KB per chunk) = 256 sectors (128 KB total)
    ; Each chunk has offset 0x0000, advancing segment by 0x0800 (32 KB physical)
    ; This avoids real-mode 64KB segment overflow entirely.
    mov bp, 4               ; 4 chunks * 64 sectors = 256 sectors (128 KB)

.load_chunk:
    mov si, dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error

    ; Advance LBA by 64 sectors
    add dword [dap_lba], 64
    ; Advance Segment by 0x0800 (32 KB)
    add word [dap_seg], 0x0800
    dec bp
    jnz .load_chunk

.read_ok:           
    ; Enable A20 line via Fast A20 gate
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Load Global Descriptor Table
    lgdt [gdt_descriptor]

    ; Switch to 32-bit Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    jmp CODE_SEG:init_32bit_protected

disk_error:
    mov ax, 0xB800
    mov es, ax
    mov word [es:0x0000], 0x0C45 ; Red 'E'
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
    dw 64                   ; 64 sectors per chunk (32 KB)
dap_offset:
    dw 0x0000               ; Offset (always 0)
dap_seg:
    dw 0x07E0               ; Initial segment (0x07E0:0x0000 = physical 0x7E00)
dap_lba:
    dd 1                    ; LBA low 32 bits (starts at Sector 1)
    dd 0                    ; LBA high 32 bits

; =========================================================================
; SECTOR 2+: 32-bit Protected Mode Entry Landing Code
; =========================================================================
section .text
[bits 32]
init_32bit_protected:
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x90000        ; 32-bit Kernel Stack

    ; Zero out the .bss section in RAM (vital for clean global state)
    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    cld
    rep stosb

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
