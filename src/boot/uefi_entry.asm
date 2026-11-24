[bits 32]

section .multiboot align=8
global multiboot2_header
multiboot2_header:
    dd 0xE85250D6                 ; Multiboot2 header magic
    dd 0                          ; i386 protected-mode entry
    dd multiboot2_header_end - multiboot2_header
    dd -(0xE85250D6 + (multiboot2_header_end - multiboot2_header))
    dw 0, 0
    dd 8
multiboot2_header_end:

section .text.uefi_entry
global multiboot_entry
extern kernel_main
extern __bss_start
extern __bss_end

%define MULTIBOOT2_BOOT_MAGIC 0x36D76289
%define MULTIBOOT_INFO_ADDR   0x000004E0
%define MULTIBOOT_DISK_MAGIC  0x554B5943
%define MULTIBOOT_MODE_ADDR   0x000004EC
%define MULTIBOOT_MODE_MAGIC  0x49464555
%define E820_COUNT_ADDR       0x000004FC
%define E820_MAP_ADDR         0x00000500
%define E820_LIMIT            20

multiboot_entry:
    cli
    cld
    mov dword [MULTIBOOT_MODE_ADDR], MULTIBOOT_MODE_MAGIC
    mov [multiboot_info_pointer], ebx

    ; GRUB enters in 32-bit protected mode with paging disabled. Install a
    ; private flat GDT before changing data segments or calling kernel code.
    lgdt [mb_gdt_descriptor]
    jmp 0x08:.segments_ready
.segments_ready:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov esp, 0x00400000

    call mb_copy_memory_map
    call mb_find_image_module

    ; Match the CPU setup performed by the BIOS entry path.
    mov eax, cr0
    and eax, 0xFFFFFFFB
    or eax, 0x00000002
    mov cr0, eax
    mov eax, cr4
    or eax, (1 << 9) | (1 << 10)
    mov cr4, eax
    call kernel_main
.halt:
    cli
    hlt
    jmp .halt

mb_copy_memory_map:
    pushad
    mov dword [E820_COUNT_ADDR], 0
    mov esi, [multiboot_info_pointer]
    test esi, esi
    jz .done
    mov eax, [esi]
    cmp eax, 16
    jb .done
    add eax, esi
    jc .done
    mov edx, eax                    ; end of Multiboot2 information
    add esi, 8
.tag_loop:
    cmp esi, edx
    jae .done
    mov eax, [esi + 4]
    cmp eax, 8
    jb .done
    mov ecx, esi
    add ecx, eax
    jc .done
    cmp ecx, edx
    ja .done
    cmp dword [esi], 0
    je .done
    cmp dword [esi], 6              ; memory-map tag
    jne .next_tag
    cmp eax, 16
    jb .next_tag
    mov ebp, [esi + 8]              ; entry size
    cmp ebp, 24
    jb .next_tag
    lea edi, [esi + 16]
    mov ebx, ecx                    ; end of this tag
.entry_loop:
    mov eax, edi
    add eax, ebp
    jc .map_done
    cmp eax, ebx
    ja .map_done
    cmp dword [E820_COUNT_ADDR], E820_LIMIT
    jae .map_done
    mov eax, [E820_COUNT_ADDR]
    imul eax, eax, 24
    add eax, E820_MAP_ADDR
    mov ecx, [edi]
    mov [eax], ecx
    mov ecx, [edi + 4]
    mov [eax + 4], ecx
    mov ecx, [edi + 8]
    mov [eax + 8], ecx
    mov ecx, [edi + 12]
    mov [eax + 12], ecx
    mov ecx, [edi + 16]
    mov [eax + 16], ecx
    mov dword [eax + 20], 1        ; Multiboot entries are enabled.
    inc dword [E820_COUNT_ADDR]
    add edi, ebp
    jmp .entry_loop
.map_done:
    mov esi, ebx
    add esi, 7
    and esi, 0xFFFFFFF8
    jmp .tag_loop
.next_tag:
    mov esi, ecx
    add esi, 7
    and esi, 0xFFFFFFF8
    jmp .tag_loop
.done:
    popad
    ret

mb_find_image_module:
    pushad
    mov dword [MULTIBOOT_INFO_ADDR], 0
    mov dword [MULTIBOOT_INFO_ADDR + 4], 0
    mov dword [MULTIBOOT_INFO_ADDR + 8], 0
    mov esi, [multiboot_info_pointer]
    test esi, esi
    jz .done
    mov eax, [esi]
    add eax, esi
    jc .done
    mov edx, eax
    add esi, 8
.tag_loop:
    cmp esi, edx
    jae .done
    mov eax, [esi + 4]
    cmp eax, 8
    jb .done
    mov ecx, esi
    add ecx, eax
    jc .done
    cmp ecx, edx
    ja .done
    cmp dword [esi], 0
    je .done
    cmp dword [esi], 3              ; Multiboot module tag
    jne .next_tag
    cmp eax, 16
    jb .next_tag
    mov eax, [esi + 8]               ; module start
    mov ebx, [esi + 12]              ; module end
    cmp ebx, eax
    jbe .next_tag
    mov dword [MULTIBOOT_INFO_ADDR], MULTIBOOT_DISK_MAGIC
    mov [MULTIBOOT_INFO_ADDR + 4], eax
    sub ebx, eax
    mov [MULTIBOOT_INFO_ADDR + 8], ebx
    jmp .done
.next_tag:
    mov esi, ecx
    add esi, 7
    and esi, 0xFFFFFFF8
    jmp .tag_loop
.done:
    popad
    ret

align 8
mb_gdt:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
mb_gdt_end:
mb_gdt_descriptor:
    dw mb_gdt_end - mb_gdt - 1
    dd mb_gdt

global multiboot_info_pointer
multiboot_info_pointer: dd 0
