[bits 32]
global init_32bit
extern kernel_main

DATA_SEG equ 0x10 ; Matches your data descriptor offset

section .kstart
init_32bit:
    ; 1. Load data segments
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; 2. Initialize stack safely away from VGA memory
    mov esp, 0x70000        

    ; 3. HARDWARE PROBE: Write a bright green 'A' directly to the first character slot
    ; 0xB8000 is the character byte ('A'), 0xB8001 is the attribute byte (0x0A = Green text)
    mov word [0xB8000], 0x0A41

    ; 4. Call the C kernel
    call kernel_main        

.halt:
    cli
    hlt
    jmp .halt
