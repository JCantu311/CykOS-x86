[bits 16]
global _start
extern kernel_main
extern __bss_start
extern __bss_end

%define E820_COUNT 0x04FC
%define E820_MAP   0x0500
%define E820_LIMIT 20

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
    call collect_memory_map

    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13

    ; Require BIOS Enhanced Disk Drive (EDD) LBA support.
    mov bx, 0x55AA
    mov ah, 0x41
    mov dl, [BOOT_DRIVE]
    int 0x13
    jc disk_error
    cmp bx, 0xAA55
    jne disk_error
    test cx, 1
    jz disk_error

    ; Load kernel from disk using INT 13h AH=42h (Extended LBA Read)
    ; Read in 16 chunks of 32 sectors (16 KB per chunk) = 512 sectors (256 KB total)
    ; Smaller EDD transfers work reliably across legacy BIOS implementations.
    ; This avoids real-mode 64KB segment overflow entirely.
    mov byte [CHUNKS_LEFT], 16 ; 16 chunks * 32 sectors (256 KB)

.load_chunk:
    mov byte [RETRIES], 3
.retry_chunk:
    mov si, dap
    mov ah, 0x42
    mov dl, [BOOT_DRIVE]
    int 0x13
    jnc .chunk_loaded

    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13
    dec byte [RETRIES]
    jnz .retry_chunk
    jmp disk_error

.chunk_loaded:
    ; Advance LBA by 32 sectors
    add dword [dap_lba], 32
    ; Advance segment by 0x0400 (16 KB)
    add word [dap_seg], 0x0400
    dec byte [CHUNKS_LEFT]
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

collect_memory_map:
    mov dword [E820_COUNT], 0
    xor ebx, ebx
    mov di, E820_MAP
.e820_next:
    cmp dword [E820_COUNT], E820_LIMIT
    jae .e820_done
    mov dword [es:di + 20], 1
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    cmp ecx, 20
    jb .e820_done
    add di, 24
    inc dword [E820_COUNT]
    test ebx, ebx
    jnz .e820_next
.e820_done:
    ret

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
RETRIES: db 0
CHUNKS_LEFT: db 0

align 4
dap:
    db 0x10                 ; Packet size = 16 bytes
    db 0x00                 ; Reserved (0)
dap_count:
    dw 32                   ; 32 sectors per chunk (16 KB)
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

    ; Enable the SSE state used by compiler-generated code.
    mov eax, cr0
    and ax, 0xFFFB          ; Clear CR0.EM (bit 2)
    or ax, 0x0002           ; Set CR0.MP (bit 1)
    mov cr0, eax

    mov eax, cr4
    or eax, (1 << 9) | (1 << 10) ; OSFXSR and OSXMMEXCPT
    mov cr4, eax

    call kernel_main        

.halt:
    cli
    hlt
    jmp .halt
