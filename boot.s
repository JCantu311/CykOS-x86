; --- Multiboot Header ---
section .multiboot
align 4
    dd 0x1BADB002           ; Magic number for Multiboot 1
    dd 0x00                 ; Flags
    dd -(0x1BADB002 + 0x00) ; Checksum (Magic + Flags + Checksum must = 0)

; --- Stack Setup ---
section .bss
align 16
stack_bottom:
    resb 16384              ; Reserve 16 KB for the initial kernel stack
stack_top:

; --- Kernel Entry Point ---
section .text
global _start
extern kernel_main          ; The name of your Rust entry function

_start:
    mov esp, stack_top      ; Set up the stack pointer register

    ; Optional: Push Multiboot details passed by GRUB in registers
    ; push ebx              ; Pointer to Multiboot info structure
    ; push eax              ; Multiboot magic number

    call kernel_main        ; Jump directly into your Rust code

.halt:
    cli                     ; Clear interrupts
    hlt                     ; Halt the CPU if Rust ever returns
    jmp .halt
