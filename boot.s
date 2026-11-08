[bits 16]
global _start
extern kernel_main

section .text
_start:
    ; 1. Clear interrupts and initialize a pure flat real-mode segment layout
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00          ; Initialize stack pointer safely below MBR code base

    ; Save the physical boot drive ID passed by the BIOS
    mov [BOOT_DRIVE], dl

    ; 2. Reset the disk controller
    xor ax, ax
    mov dl, [BOOT_DRIVE]
    int 0x13

    ; 3. SAFE LOOP SECTOR READ: Load 16 sectors one by one to avoid geometry limits
    mov ax, 0x0000
    mov es, ax              
    mov bx, 0x9000          ; Target memory destination address base = 0x9000
    
    mov cl, 2               ; Start at Sector 2 (Sector 1 is this MBR)
    mov ch, 0               ; Cylinder 0
    mov dh, 0               ; Head 0

.read_sector_loop:
    mov ah, 0x02            ; BIOS Read Sectors routine
    mov al, 1               ; Read exactly 1 sector at a time
    mov dl, [BOOT_DRIVE]    
    int 0x13                
    jc disk_error           ; If carry flag triggers, fall back to hardware visual check

    add bx, 512             ; Advance destination memory offset by exactly 1 sector size
    
    inc cl                  ; Move to the next physical disk sector pointer index
    cmp cl, 18              ; Have we completed reading 16 full sectors? (Sectors 2 through 17)
    jne .read_sector_loop   

    ; 4. Enable the A20 Line safely via System Control Port A
    in al, 0x92
    or al, 2
    out 0x92, al

    ; 5. Compute the Global Descriptor Table linear offset position at runtime
    mov eax, gdt_start      
    mov [gdt_descriptor + 2], eax
    lgdt [gdt_descriptor]

    ; 6. Switch to 32-bit Protected Mode
    mov eax, cr0
    or eax, 1
    mov cr0, eax

    ; 7. Far jump directly to our internal 32-bit code section label
    jmp CODE_SEG:init_32bit_protected

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
    dd 0x0                  ; Populated dynamically at runtime above

CODE_SEG equ gdt_code - gdt_start
DATA_SEG equ gdt_data - gdt_start

BOOT_DRIVE: db 0            

; Force the physical 512-byte MBR boundary constraints natively inside the file
times 510 - ($ - $$) db 0
dw 0xAA55

; =========================================================================
; SECTOR 2 LANDING GEAR: This block physically follows byte 512 in the binary
; =========================================================================
[bits 32]
init_32bit_protected:
    ; Secure data segments with our data descriptor matching the GDT setup
    mov ax, DATA_SEG
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Initialize stack safely in unmapped conventional memory away from VGA buffer windows
    mov esp, 0x70000        

    ; HARDWARE PROBE: Write a bright green 'A' to confirm successful 32-bit entry
    mov word [0xB8000], 0x0A41

    ; Jump directly to your compiled C kernel code entry point function!
    call kernel_main        

.halt:
    cli
    hlt
    jmp .halt
