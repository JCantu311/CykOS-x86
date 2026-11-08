[bits 16]
global _start
extern init_32bit

section .text
_start:
    ; 1. Clear interrupts and clear segment configurations to absolute zero
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00          ; Keep stack safely below MBR execution window

    ; Save the physical boot drive ID passed to us by the BIOS
    mov [BOOT_DRIVE], dl

    ; 2. Reset the disk controller to clear any cached states
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13

    ; 3. LOAD KERNEL FROM DISK SAFELY (Read 16 sectors individually)
    mov ax, 0x0000
    mov es, ax              ; Target segment = 0x0000
    mov bx, 0x9000          ; Start target buffer address at 0x9000
    
    mov cl, 2               ; Sector 2 (Sector 1 is this MBR)
    mov ch, 0               ; Cylinder 0
    mov dh, 0               ; Head 0

.read_sector_loop:
    mov ah, 0x02            ; BIOS Read Sector function code
    mov al, 1               ; Read exactly 1 sector at a time
    mov dl, [BOOT_DRIVE]    
    int 0x13                ; Call BIOS Disk Interrupt
    jc disk_error           ; If carry flag is set, hardware error occurred!

    ; Advance destination buffer tracking pointer by 1 sector size (512 bytes)
    add bx, 512             
    
    inc cl                  ; Move to the next sequential sector index
    cmp cl, 18              ; Have we successfully read 16 sectors? (Sectors 2 through 17)
    jne .read_sector_loop   ; Loop back if we need more sectors

    ; 4. Enable the A20 Line safely via System Control Port A
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 5. Calculate and load the Global Descriptor Table linear pointer dynamically
    mov eax, gdt_start      
    mov [gdt_descriptor + 2], eax
    lgdt [gdt_descriptor]

    ; 6. Switch to 32-bit Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 7. Far jump to flush the pipeline and load the 32-bit code descriptor
    jmp CODE_SEG:init_32bit

disk_error:
    ; Hardware fallback: Write a bright red 'E' to the top-left VGA slot
    mov ax, 0xB800
    mov es, ax
    mov word [es:0x0000], 0x0C45 
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
    dd 0x0                  ; Populated dynamically at runtime above

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

BOOT_DRIVE: db 0            

; Target the absolute hardware MBR signature block constraint offset natively
times 510 - ($ - $$) db 0
dw 0xAA55
