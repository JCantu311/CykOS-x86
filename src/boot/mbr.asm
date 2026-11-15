[bits 16]
org 0x7C00
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [boot_drive], dl
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jc disk_error
    mov dl, [boot_drive]
    jmp 0x0000:0x8000
disk_error:
    mov si, error_message
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt
boot_drive: db 0
align 4
dap:
    db 0x10, 0
    dw 16
    dw 0x0000
    dw 0x0800
    dd 1
    dd 0
error_message: db 'CykOS boot read error', 0
times 510-($-$$) db 0
dw 0xAA55
