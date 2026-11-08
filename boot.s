[bits 16]
global _start
extern init_32bit

section .boot
_start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; Initialize stack below MBR

    mov [BOOT_DRIVE], dl    ; Save boot drive ID

    ; Reset disk controller
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13

    ; Load 16 sectors to memory address 0x9000
    mov ax, 0x0000
    mov es, ax              
    mov bx, 0x9000          

    mov ah, 0x02            ; Read sectors function
    mov al, 16              ; Load 16 sectors
    mov ch, 0               ; Cylinder 0
    mov dh, 0               ; Head 0
    mov cl, 2               ; Sector 2
    mov dl, [BOOT_DRIVE]    
    int 0x13                
    jc disk_error           

    ; Enable A20 Line
    in al, 0x92
    or al, 2
    out 0x92, al

    ; Load GDT
    lgdt [gdt_descriptor]

    ; Switch to Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; Far jump straight to our dedicated 32-bit entry point code
    jmp CODE_SEG:init_32bit

disk_error:
    mov ax, 0xB800
    mov es, ax
    mov word [es:0x0000], 0x0C45 ; Red 'E'
.error_loop:
    cli
    hlt
    jmp .error_loop

; --- Global Descriptor Table ---
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

section .mbr_signature
    dw 0xAA55
