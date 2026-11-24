OUTPUT_FORMAT(elf32-i386)
ENTRY(multiboot_entry)
INCLUDE config/memory_layout.ld

__shell_image_base = SHELL_IMAGE_BASE;
__shell_image_limit = SHELL_IMAGE_END;

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    /* GRUB's x86_64 UEFI build enters a Multiboot2 i386 kernel in 32-bit
       protected mode. Keep the image above low firmware memory. */
    . = 0x00100000;
    _kernel_start = .;

    .multiboot ALIGN(8) : { KEEP(*(.multiboot)) } :text
    .text : {
        KEEP(*(.text.uefi_entry))
        *(.text*)
        . = ALIGN(4);
        *(.rodata*)
        *(.rdata*)
        . = ALIGN(4);
        *(.data*)
        *(.data.rel.ro*)
        *(.data.rel.ro.local*)
        *(.got*)
        *(.got.plt*)
    } :text

    . = ALIGN(4096);
    __bss_start = .;
    .bss (NOLOAD) : {
        *(.bss*)
        *(COMMON)
        . = ALIGN(4096);
    } :data
    __bss_end = .;
    _kernel_end = .;

    ASSERT(_kernel_end < 0x00180000,
        "UEFI kernel overlaps the fixed user application image region")

    /DISCARD/ : {
        *(.gcc_except_table*)
        *(.eh_frame*)
        *(.note*)
        *(.comment)
        *(.debug*)
    }
}
