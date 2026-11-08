[bits 16]
global _start
extern kernel_main

section .boot
_start:
    ; 1. Set up standard 16-bit segment environment
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; Initialize stack pointer below MBR

    ; Save the physical boot drive ID passed to us by BIOS in DL register
    mov [BOOT_DRIVE], dl

    ; 2. Reset the disk controller to clear any cached states
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13

    ; 3. LOAD KERNEL FROM DISK (Read 16 sectors into memory address 0x9000)
    mov ax, 0x0000
    mov es, ax              ; ES = 0x0000
    mov bx, 0x9000          ; BX = 0x9000 (Target buffer = ES:BX = 0x0000:0x9000)

    mov ah, 0x02            ; BIOS Read Sectors function code
    mov al, 16              ; Load 16 sectors (8 KB space for kernel)
    mov ch, 0               ; Cylinder 0
    mov dh, 0               ; Head 0
    mov cl, 2               ; Start reading at Sector 2 (Sector 1 is this MBR)
    mov dl, [BOOT_DRIVE]    ; Use saved drive ID
    int 0x13                ; Call BIOS Disk Interrupt
    jc disk_error           ; If carry flag is set, hardware error occurred!

    ; 4. Enable A20 Line
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 5. Load Global Descriptor Table
    lgdt [gdt_descriptor]

    ; 6. Switch to 32-bit Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 7. Far jump to load the 32-bit Code Segment
    jmp CODE_SEG:init_32bit

disk_error:
    ; Hardware indicator: Write a bright red 'E' to top left of screen on failure
    mov word [0xb8000], 0x0C45
.error_loop:
    cli
    hlt
    jmp .error_loop

[bits 32]
init_32bit:
    ; Load 32-bit Data tracking state registers
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov esp, 0x90000        ; Large stable stack region

    call kernel_main        ; Jump directly to your C kernel!

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

BOOT_DRIVE: db 0            ; Memory location to save drive index

section .mbr_signature
    dw 0xAA55
