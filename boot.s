[bits 16]
global _start
extern kernel_main

section .boot
_start:
    ; 1. Clear interrupts and initialize a flat 16-bit real-mode environment
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00          ; Set stack pointer safely below the MBR block

    ; Save the physical boot drive ID passed to us by BIOS in DL register
    mov [BOOT_DRIVE], dl

    ; 2. Reset the disk controller to clear any cached states
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13

    ; 3. LOAD KERNEL FROM DISK (Read 16 sectors into memory address 0x9000)
    mov ax, 0x0000
    mov es, ax              
    mov bx, 0x9000          ; Target buffer address pointer = 0x0000:0x9000

    mov ah, 0x02            ; BIOS Read Sectors function code
    mov al, 16              ; Load 16 sectors (8 KB execution space for kernel)
    mov ch, 0               ; Cylinder 0
    mov dh, 0               ; Head 0
    mov cl, 2               ; Start reading at Sector 2 (Sector 1 is this MBR)
    mov dl, [BOOT_DRIVE]    ; Use saved drive ID
    int 0x13                ; Call BIOS Disk Interrupt
    jc disk_error           ; If carry flag is set, hardware reading error occurred!

    ; 4. Enable the A20 Line via the Fast A20 Gate
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 5. Load the Global Descriptor Table
    lgdt [gdt_descriptor]

    ; 6. Switch to 32-bit Protected Mode via Control Register 0
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 7. Far jump to flush the prefetch queue and load the 32-bit Code Segment
    jmp CODE_SEG:init_32bit

disk_error:
    ; Fallback visual indicator: Write a bright red 'E' to the top-left VGA window slot
    mov ax, 0xB800
    mov es, ax
    mov word [es:0x0000], 0x0C45
.error_loop:
    cli
    hlt
    jmp .error_loop


[bits 32]
section .kernel_entry
init_32bit:
    ; Load 32-bit segment registers with our standard Data Descriptor
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Initialize stack safely in conventional memory away from VGA buffer windows
    mov esp, 0x70000        

    ; Jump directly to the C kernel anchor function
    call kernel_main        

.halt:
    cli
    hlt
    jmp .halt

; --- Global Descriptor Table Layout ---
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

BOOT_DRIVE: db 0            ; Storage container for drive index tracking

section .mbr_signature
    dw 0xAA55
