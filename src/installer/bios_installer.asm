[bits 16]
org 0x7C00
%ifndef OS_IMAGE_LBA
%define OS_IMAGE_LBA 0
%endif
%ifndef OS_IMAGE_BLOCKS
%define OS_IMAGE_BLOCKS 0
%endif
%ifndef KERNEL_ISO_LBA
%define KERNEL_ISO_LBA 0
%endif
%ifndef KERNEL_BLOCKS
%define KERNEL_BLOCKS 0
%endif
%define DISK_CHUNK 4
%define BUFFER_SEG 0x1000
%define LIVE_BOOT_MAGIC 0x534F4B43

start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti
    mov [cd_drive], dl
    mov si, banner
    call puts
    mov si, menu
    call puts
    xor ah, ah
    int 0x16
    push ax
    call putc
    pop ax
    cmp al, '1'
    je .boot_live
    cmp al, '2'
    je .install
    mov si, invalid_menu
    call puts
    jmp halt
.boot_live:
%if KERNEL_ISO_LBA = 0 || KERNEL_BLOCKS = 0 || OS_IMAGE_LBA = 0
    mov si, build_error
    call puts
    jmp halt
%else
    call e820
    mov dword [0x04F0], LIVE_BOOT_MAGIC
    mov dword [0x04F4], OS_IMAGE_LBA
    mov dword [0x04F8], OS_IMAGE_BLOCKS
    mov dword [source_lba], KERNEL_ISO_LBA
    mov dword [remaining], KERNEL_BLOCKS
    mov word [dest_seg], 0x1000
.load_kernel:
    cmp dword [remaining], 0
    je .kernel_loaded
    mov word [dap_count], 1
    mov word [dap_offset], 0
    mov ax, [dest_seg]
    mov [dap_segment], ax
    mov eax, [source_lba]
    mov [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov si, dap
    mov dl, [cd_drive]
    mov ah, 0x42
    int 0x13
    jc .kernel_read_error
    inc dword [source_lba]
    add word [dest_seg], 0x80
    dec dword [remaining]
    jmp .load_kernel
.kernel_read_error:
    mov si, source_error
    call puts
    jmp halt
.kernel_loaded:
    cli
    lgdt [gdtr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:live_pm_entry
%endif
.install:
    mov si, warning
    call puts
    mov byte [drive_count], 0
    mov byte [probe_drive], 0x80
.probe:
    ; Never offer the boot medium itself as a destructive target.
    mov al, [probe_drive]
    cmp al, [cd_drive]
    je .next_drive
    mov dl, [probe_drive]
    mov ah, 0x41
    mov bx, 0x55AA
    int 0x13
    jc .next_drive
    cmp bx, 0xAA55
    jne .next_drive
    test cx, 1
    jz .next_drive
    xor bx, bx
    mov bl, [drive_count]
    cmp bl, 4
    jae .next_drive
    mov al, [probe_drive]
    mov [drives+bx], al
    mov al, [drive_count]
    add al, '0'
    call putc
    mov al, ':'
    call putc
    mov al, ' '
    call putc
    mov al, [probe_drive]
    call hex8
    inc byte [drive_count]
    mov si, drive_found
    call puts
.next_drive:
    inc byte [probe_drive]
    cmp byte [probe_drive], 0x84
    jb .probe
    cmp byte [drive_count], 0
    jne .select
    mov si, no_drives
    call puts
    jmp halt
.select:
    mov si, choose_drive
    call puts
    xor ah, ah
    int 0x16
    push ax
    call putc
    pop ax
    cmp al, '0'
    jb .bad_choice
    cmp al, '3'
    ja .bad_choice
    sub al, '0'
    cmp al, [drive_count]
    jae .bad_choice
    xor ah, ah
    mov bx, ax
    mov al, [drives+bx]
    mov [target_drive], al
    mov word [edd_params], 30
    mov si, edd_params
    mov dl, [target_drive]
    mov ah, 0x48
    int 0x13
    jc .disk_info_error
    cmp word [edd_params+24], 512
    jne .sector_size_error
%if OS_IMAGE_BLOCKS != 0
    cmp dword [edd_params+20], 0
    jne .capacity_ok
    cmp dword [edd_params+16], OS_IMAGE_BLOCKS*4
    jb .disk_too_small
.capacity_ok:
%endif
    mov al, [target_drive]
    call hex8
    mov si, selected_disk
    call puts
    mov si, confirm_text
    call puts
    mov si, erase_phrase
    mov cx, 5
.confirm_phrase:
    xor ah, ah
    int 0x16
    call putc
    cmp al, [si]
    jne .cancelled
    inc si
    loop .confirm_phrase
    xor ah, ah
    int 0x16
    cmp al, 13
    jne .cancelled
%if OS_IMAGE_LBA = 0 || OS_IMAGE_BLOCKS = 0
    mov si, build_error
    call puts
    jmp halt
%else
    mov si, writing
    call puts
    mov dword [source_lba], OS_IMAGE_LBA
    mov dword [target_lba], 0
    mov dword [remaining], OS_IMAGE_BLOCKS
.copy:
    cmp dword [remaining], 0
    je .finished
    mov word [dap_count], 1
    mov word [dap_offset], 0
    mov word [dap_segment], BUFFER_SEG
    mov eax, [source_lba]
    mov [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov si, dap
    mov dl, [cd_drive]
    mov ah, 0x42
    int 0x13
    jc .read_error
    mov word [dap_count], DISK_CHUNK
    mov word [dap_offset], 0
    mov word [dap_segment], BUFFER_SEG
    mov eax, [target_lba]
    mov [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov si, dap
    mov dl, [target_drive]
    mov ah, 0x43
    mov al, 0
    int 0x13
    jc .write_error
    add dword [source_lba], 1
    add dword [target_lba], 4
    sub dword [remaining], 1
    add dword [progress], 1
    cmp dword [progress], 1024
    jb .copy
    mov dword [progress], 0
    mov al, '.'
    call putc
    jmp .copy
.finished:
    mov si, success
    call puts
    jmp halt
.read_error:
    mov si, source_error
    call puts
    jmp halt
.write_error:
    mov si, target_error
    call puts
    jmp halt
%endif
.bad_choice:
    mov si, invalid_choice
    call puts
    jmp halt
.disk_info_error:
    mov si, disk_info_error
    call puts
    jmp halt
.sector_size_error:
    mov si, sector_size_error
    call puts
    jmp halt
.disk_too_small:
    mov si, disk_too_small
    call puts
    jmp halt
.cancelled:
    mov si, cancelled
    call puts
halt:
    cli
    hlt
    jmp halt

puts:
    lodsb
    test al, al
    jz .done
    call putc
    jmp puts
.done:
    ret
putc:
    mov ah, 0x0E
    mov bx, 0x0007
    int 0x10
    ret
hex8:
    push ax
    shr al, 4
    call hex_digit
    pop ax
    and al, 0x0F
    call hex_digit
    ret
hex_digit:
    cmp al, 10
    jb .number
    add al, 'A' - 10
    jmp putc
.number:
    add al, '0'
    jmp putc

cd_drive: db 0
probe_drive: db 0
drive_count: db 0
target_drive: db 0
drives: times 4 db 0
source_lba: dd 0
target_lba: dd 0
remaining: dd 0
progress: dd 0
dest_seg: dw 0
banner: db 13, 10, 'CykOS BIOS CD Installer', 13, 10, 0
menu: db '1) Run CykOS live from CD', 13, 10, '2) Install CykOS to a hard disk', 13, 10, 'Choose 1 or 2: ', 0
invalid_menu: db 13, 10, 'Invalid choice. Reboot to try again.', 13, 10, 0
warning: db 'WARNING: installation replaces the selected disk partition table and image area.', 13, 10, 0
drive_found: db ' BIOS hard disk', 13, 10, 0
selected_disk: db ' selected. Image starts at sector 0; extra sectors are left unchanged.', 13, 10, 0
no_drives: db 'No BIOS hard disks detected.', 13, 10, 0
choose_drive: db 'Select disk number (0-3): ', 0
confirm_text: db 13, 10, 'Type ERASE then Enter to replace its partition table and image area: ', 0
erase_phrase: db 'ERASE'
writing: db 13, 10, 'Writing CykOS image; do not power off...', 13, 10, 0
success: db 13, 10, 'Install complete. Remove CD and reboot.', 13, 10, 0
source_error: db 13, 10, 'Could not read installer image from CD.', 13, 10, 0
target_error: db 13, 10, 'Disk write failed; installation is incomplete.', 13, 10, 0
invalid_choice: db 13, 10, 'Invalid disk choice. Reboot to try again.', 13, 10, 0
cancelled: db 13, 10, 'Installation cancelled.', 13, 10, 0
build_error: db 13, 10, 'Installer image metadata was not generated.', 13, 10, 0
disk_info_error: db 13, 10, 'Could not read BIOS disk capacity; refusing to write.', 13, 10, 0
sector_size_error: db 13, 10, 'Disk must expose 512-byte sectors.', 13, 10, 0
disk_too_small: db 13, 10, 'Selected disk is smaller than the CykOS image.', 13, 10, 0
align 4
dap:
    db 0x10, 0
dap_count: dw 1
dap_offset: dw 0
dap_segment: dw BUFFER_SEG
dap_lba: dq 0
edd_params:
    dw 30, 0
    times 26 db 0
gdtr:
    dw gdt_end - gdt - 1
    dd gdt
gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt_end:
e820:
    mov dword [0x04FC], 0
    xor ebx, ebx
    mov di, 0x0500
.e820_next:
    cmp dword [0x04FC], 20
    jae .e820_done
    mov dword [es:di+20], 1
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    cmp ecx, 20
    jb .e820_done
    add di, 24
    inc dword [0x04FC]
    test ebx, ebx
    jnz .e820_next
.e820_done:
    ret
[bits 32]
live_pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x400000
    jmp 0x00010000
[bits 16]
times 8192-($-$$) db 0
