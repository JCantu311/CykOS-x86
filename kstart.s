[bits 32]
global init_32bit
extern kernel_main

DATA_SEG equ 0x10 ; Matches gdt_data offset in boot.s

section .kstart
init_32bit:
    ; Secure segment registers
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Initialize stack safely away from the VGA text mode window
    mov esp, 0x70000        

    ; Safely hand over control to your compiled C kernel code
    call kernel_main        

.halt:
    cli
    hlt
    jmp .halt
