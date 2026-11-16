[bits 16]
org 0x8000
%define FAT_LBA 2052
%define ROOT_LBA 2116
%define DATA_LBA 2148
%define LOAD_SEG 0x1000
%define SCRATCH 0x7000
start:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7A00
    sti
    mov [drive], dl
    call e820
    mov dword [root_lba], ROOT_LBA
    mov byte [root_sectors], 32
.root_read:
    mov eax, [root_lba]
    mov word [dap_offset], SCRATCH
    mov word [dap_segment], 0
    call read_sector
    jc fail
    mov si, SCRATCH
    mov cx, 16
.find:
    cmp byte [si], 0
    je fail
    cmp byte [si], 0xE5
    je .skip
    test byte [si+11], 0x18
    jnz .skip
    push si
    push cx
    mov di, filename
    mov cx, 11
    repe cmpsb
    pop cx
    pop si
    je .found
.skip:
    add si, 32
    loop .find
    inc dword [root_lba]
    dec byte [root_sectors]
    jnz .root_read
    jmp fail
.found:
    mov ax, [si+26]
    mov [cluster], ax
    mov eax, [si+28]
    test eax, eax
    jz fail
    cmp eax, 0x80000
    ja fail
    add eax, 511
    shr eax, 9
    mov [remaining], eax
    mov word [dest_seg], LOAD_SEG
.cluster:
    cmp dword [remaining], 0
    je .enter_pm
    movzx eax, word [cluster]
    cmp eax, 2
    jb fail
    sub eax, 2
    shl eax, 2
    add eax, DATA_LBA
    mov [sector_lba], eax
    mov byte [cluster_sectors], 4
.read_cluster:
    cmp dword [remaining], 0
    je .enter_pm
    mov word [dap_offset], 0
    mov ax, [dest_seg]
    mov [dap_segment], ax
    mov eax, [sector_lba]
    call read_sector
    jc fail
    inc dword [sector_lba]
    add word [dest_seg], 0x20
    dec dword [remaining]
    dec byte [cluster_sectors]
    jnz .read_cluster
    ; The requested file may end exactly on a cluster boundary. In that case
    ; the remaining-sector count is already zero and the FAT entry is the
    ; normal 0xFFFF end marker, not another cluster to follow.
    cmp dword [remaining], 0
    je .enter_pm
    movzx eax, word [cluster]
    shl eax, 1
    mov edx, eax
    and dx, 511
    mov [fat_offset], dx
    shr eax, 9
    add eax, FAT_LBA
    mov word [dap_offset], SCRATCH
    mov word [dap_segment], 0
    call read_sector
    jc fail
    mov si, [fat_offset]
    mov ax, [SCRATCH+si]
    cmp ax, 0xFFF0
    jae fail
    mov [cluster], ax
    jmp .cluster
.enter_pm:
    cli
    lgdt [gdtr]
    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:pm_entry
fail:
    mov si, fail_text
.print:
    lodsb
    test al, al
    jz .halt
    mov ah, 0x0E
    int 0x10
    jmp .print
.halt:
    cli
    hlt
    jmp .halt
read_sector:
    mov [dap_lba], eax
    mov dword [dap_lba+4], 0
    mov word [dap_count], 1
    mov si, dap
    mov ah, 0x42
    mov dl, [drive]
    int 0x13
    ret
e820:
    mov dword [0x04FC], 0
    xor ebx, ebx
    mov di, 0x0500
.next:
    cmp dword [0x04FC], 20
    jae .done
    mov dword [es:di+20], 1
    mov eax, 0xE820
    mov edx, 0x534D4150
    mov ecx, 24
    int 0x15
    jc .done
    cmp eax, 0x534D4150
    jne .done
    cmp ecx, 20
    jb .done
    add di, 24
    inc dword [0x04FC]
    test ebx, ebx
    jnz .next
.done:
    ret
drive: db 0
root_lba: dd 0
root_sectors: db 0
filename: db 'KERNEL  BIN'
fail_text: db 'CykOS: kernel FAT16 load failed', 0
cluster: dw 0
fat_offset: dw 0
dest_seg: dw 0
remaining: dd 0
sector_lba: dd 0
cluster_sectors: db 0
align 4
dap:
    db 0x10, 0
dap_count: dw 1
dap_offset: dw 0
dap_segment: dw 0
dap_lba: dd 0
    dd 0
align 8
gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdtr:
    dw 23
    dd gdt
[bits 32]
pm_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x140000
    jmp 0x00010000
