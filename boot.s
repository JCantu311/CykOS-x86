[bits 16]
global _start
extern init_32bit

section .text
_start:
    ; 1. Clear interrupts and force segment boundaries to an absolute zero state
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00          ; Keep stack safely below MBR code execution space

    ; Save the physical boot drive ID passed by BIOS
    mov [BOOT_DRIVE], dl

    ; 2. Reset the disk controller state machine
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13

    ; 3. LOAD KERNEL FROM DISK (Read 16 sectors into memory address 0x9000)
    mov ax, 0x0000
    mov es, ax              
    mov bx, 0x9000          ; Target destination buffer = 0x0000:0x9000

    mov ah, 0x02            ; BIOS Read Sectors routine code
    mov al, 16              ; Load 16 consecutive sectors
    mov ch, 0               ; Cylinder 0
    mov dh, 0               ; Head 0
    mov cl, 2               ; Sector 2 (Sector 1 is this active MBR code block)
    mov dl, [BOOT_DRIVE]    
    int 0x13                
    jc disk_error           

    ; 4. Enable the A20 Line safely via System Control Port A
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 5. Calculate and load the Global Descriptor Table linear pointer dynamically
    mov eax, gdt_start      ; Get base address calculation
    mov [gdt_descriptor + 2], eax
    lgdt [gdt_descriptor]

    ; 6. Switch to 32-bit Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 7. Far jump to flush instruction prefetch pipe lines and commit the code descriptor
    jmp CODE_SEG:init_32bit

disk_error:
    mov ax, 0xB800
    mov es, ax
    mov word [es:0x0000], 0x0C45 ; Visual Indicator: Bright red 'E' on top left
.error_loop:
    cli
    hlt
    jmp .error_loop

; --- Structured Global Descriptor Table ---
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
    dd 0x0                  ; Cleared out: Populated dynamically at runtime above

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

BOOT_DRIVE: db 0            

; Target the absolute hardware MBR signature block constraint offset natively
times 510 - ($ - $$) db 0
dw 0xAA55
