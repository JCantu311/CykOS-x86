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

    ; Read 16 sectors sequentially into memory address 0x9000
    mov ax, 0x0000
    mov es, ax              
    mov bx, 0x9000          
    
    mov ah, 0x02            
    mov al, 32
    mov ch, 0               
    mov dh, 0               
    mov cl, 2               
    mov dl, [BOOT_DRIVE]    
    int 0x13                
    jc disk_error           

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