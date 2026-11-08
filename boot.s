[bits 16]
global _start
extern kernel_main

section .boot
_start:
    ; 1. Clear interrupts and setup standard 16-bit segment space
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; Keep boot stack below MBR address

    ; Save the boot drive identifier number passed to us by the BIOS in DL register
    mov [BOOT_DRIVE], dl

    ; 2. LOAD KERNEL FROM DISK BEFORE WE SWITCH MODES
    mov bx, 0x9000          ; Target destination memory buffer (ES:BX = 0x0000:0x9000)
    mov ah, 0x02            ; BIOS Read Sectors function
    mov al, 16              ; Number of sectors to read (16 sectors * 512 bytes = 8KB)
    mov ch, 0               ; Cylinder 0
    mov dh, 0               ; Head 0
    mov cl, 2               ; Start reading at Sector 2 (Sector 1 is this MBR!)
    mov dl, [BOOT_DRIVE]    ; Use the saved boot drive index
    int 0x13                ; Call BIOS interrupt to execute disk read
    jc disk_error           ; If carry flag is set, the read failed! Jump to trap.

    ; 3. Enable A20 Line
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 4. Load Global Descriptor Table
    lgdt [gdt_descriptor]

    ; 5. Switch to 32-bit Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 6. Far jump to clear prefetch queue and execute 32-bit segment code
    jmp CODE_SEG:init_32bit

disk_error:
    ; Trap the CPU if we failed to read the drive sectors
    hlt
    jmp disk_error

[bits 32]
init_32bit:
    ; Set up 32-bit Data segment tracking states
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x90000        ; Establish a large 32-bit stack

    call kernel_main        ; Execute your C environment!

.halt:
    cli
    hlt
    jmp .halt

; --- Global Descriptor Table Layout ---
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

BOOT_DRIVE: db 0            ; Variable to store the physical drive ID

; Place the MBR boot signature at the exact end of Sector 1
section .mbr_signature
    dw 0xAA55
